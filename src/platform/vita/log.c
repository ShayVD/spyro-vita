/* The Vita log, ux0:data/spyro/spyro.log, written through sceIoWrite
 * (decisions D7: newlib's own fopen'd log stayed empty in Vita3K for kh1).
 * printf/puts and stdout/stderr go there too, and PsyX_Log* and host_log
 * (the linux32 files the Vita shares: gpu_soft.c, png.c).
 * From vita_log_start on (after config.txt), lines go into a ring that a
 * thread on cores 1-2 writes out (D75): a report is ~15 lines every 300
 * vblanks, and each sceIoWrite to the memory card on the game thread was a
 * likely cause of the 60-150 ms "worst vblank" in nearly every window on
 * hardware (J6). A full ring makes the caller wait for room, so no line is
 * lost or reordered; sceKernelExitProcess and the watchdog's crash dump
 * flush it first. log_sync=1 writes every line straight through, as
 * before. */
#include "vita.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define RING (128 << 10)
#define WRITER_PRIORITY 0xb8 /* behind the game thread (0xa0): it only has to keep up */

static SceUID s_fd = -1;
static char s_ring[RING];
static volatile uint32_t s_head, s_tail; /* bytes put, bytes written (wrap; differences) */
static SceUID s_lock = -1, s_sema = -1;
static int s_async;

static void put(const char *buf, int n) {
  if (s_fd < 0 || n <= 0)
    return;
  if (!s_async || n > RING) {
    sceIoWrite(s_fd, buf, n);
    return;
  }
  sceKernelLockMutex(s_lock, 1, NULL);
  while (RING - (s_head - s_tail) < (uint32_t)n) { /* full: wait for the writer */
    sceKernelUnlockMutex(s_lock, 1);
    sceKernelSignalSema(s_sema, 1);
    sceKernelDelayThread(1000);
    sceKernelLockMutex(s_lock, 1, NULL);
  }
  {
    uint32_t at = s_head % RING, first = RING - at < (uint32_t)n ? RING - at : (uint32_t)n;
    memcpy(s_ring + at, buf, first);
    memcpy(s_ring, buf + first, n - first);
    __atomic_store_n(&s_head, s_head + n, __ATOMIC_RELEASE);
  }
  sceKernelUnlockMutex(s_lock, 1);
  sceKernelSignalSema(s_sema, 1); /* past its max of 1 it just fails: the writer is due anyway */
}

static int writer_main(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  for (;;) {
    uint32_t tail = s_tail, head = __atomic_load_n(&s_head, __ATOMIC_ACQUIRE);
    if (head == tail) {
      SceUInt timeout = 100000;
      sceKernelWaitSema(s_sema, 1, &timeout);
      continue;
    }
    { /* only this thread moves s_tail; put() only reads it under the lock */
      uint32_t at = tail % RING, n = head - tail;
      if (n > RING - at)
        n = RING - at;
      sceIoWrite(s_fd, s_ring + at, n);
      sceKernelLockMutex(s_lock, 1, NULL);
      __atomic_store_n(&s_tail, tail + n, __ATOMIC_RELEASE);
      sceKernelUnlockMutex(s_lock, 1);
    }
  }
  return 0;
}

/* After config.txt: the writer thread, unless log_sync=1. */
void vita_log_start(int sync) {
  SceUID th;
  if (sync || s_fd < 0 || s_async)
    return;
  s_lock = sceKernelCreateMutex("spyro_log", 0, 0, NULL);
  s_sema = sceKernelCreateSema("spyro_log", 0, 0, 1, NULL);
  th = sceKernelCreateThread("spyro_log", writer_main, WRITER_PRIORITY, 16 << 10, 0,
                             SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2, NULL);
  if (s_lock < 0 || s_sema < 0 || th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("log: written on the calling thread (no writer thread: %08x)", (unsigned)th);
    return;
  }
  s_async = 1;
  vita_log("log: written by a thread on cores 1-2, priority 0x%x (log_sync=0, D75)",
           WRITER_PRIORITY);
}

/* Everything put so far in the file (exit, the watchdog's crash dump). Any
 * thread but the writer. Gives up after a second (a stuck card). */
void vita_log_flush(void) {
  int i;
  for (i = 0; s_async && i < 1000; i++) {
    if (__atomic_load_n(&s_tail, __ATOMIC_ACQUIRE) == __atomic_load_n(&s_head, __ATOMIC_ACQUIRE))
      return;
    sceKernelSignalSema(s_sema, 1);
    sceKernelDelayThread(1000);
  }
}

/* -Wl,--wrap=sceKernelExitProcess: every exit flushes the log first */
int __real_sceKernelExitProcess(int res);
int __wrap_sceKernelExitProcess(int res) {
  vita_log_flush();
  return __real_sceKernelExitProcess(res);
}

/* newlib's stdout/stderr as funopen FILEs on the log, for the shared code's
 * fprintf(stderr, ...) (libcd, libspu, libmcrd). */
static int cookie_write(void *cookie, const char *buf, int n) {
  (void)cookie;
  put(buf, n);
  return n;
}

void vita_log_open(void) {
  FILE *f;
  sceIoMkdir(VITA_DATA_DIR, 0777);
  /* the last run's log kept beside it: the menu's RESTART NOW starts the
   * app again (D132), and so does a player after a crash */
  sceIoRemove(VITA_DATA_DIR "/spyro-prev.log");
  if (sceIoRename(VITA_DATA_DIR "/spyro.log", VITA_DATA_DIR "/spyro-prev.log") < 0)
    sceIoRemove(VITA_DATA_DIR "/spyro.log");
  s_fd = sceIoOpen(VITA_DATA_DIR "/spyro.log", SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0666);
  f = funopen(NULL, NULL, cookie_write, NULL, NULL);
  if (f) {
    setvbuf(f, NULL, _IONBF, 0);
    stdout = f;
    stderr = f;
  }
}

static void emit(const char *level, const char *fmt, va_list ap) {
  char buf[1024];
  int n = snprintf(buf, sizeof buf, "[%6d] %s ", vita_vblank_count(), level);
  int m = vsnprintf(buf + n, sizeof buf - n - 1, fmt, ap);
  if (m < 0)
    return;
  n += m;
  if (n > (int)sizeof buf - 2)
    n = sizeof buf - 2;
  while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
    n--;
  buf[n++] = '\n';
  put(buf, n);
}

#define LOG_FN(name, level)                                                    \
  void name(const char *fmt, ...) {                                            \
    va_list ap;                                                                \
    va_start(ap, fmt);                                                         \
    emit(level, fmt, ap);                                                      \
    va_end(ap);                                                                \
  }
LOG_FN(vita_log, "vita")
LOG_FN(host_log, "host")
LOG_FN(PsyX_Log, "psyx")
LOG_FN(PsyX_Log_Info, "info")
LOG_FN(PsyX_Log_Warning, "WARN")
LOG_FN(PsyX_Log_Error, "ERR ")
LOG_FN(PsyX_Log_Success, "ok  ")

void host_log_flush(void) {}

/* The game's own printf (and gcc's printf -> puts) into the log (kh1's
 * host/vita/main.c: newlib's stdout did not reach the file there). */
int vprintf(const char *fmt, va_list ap) {
  char buf[1024];
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  if (n > (int)sizeof buf - 1)
    n = sizeof buf - 1;
  put(buf, n);
  return n;
}

int printf(const char *fmt, ...) {
  va_list ap;
  int n;
  va_start(ap, fmt);
  n = vprintf(fmt, ap);
  va_end(ap);
  return n;
}

int puts(const char *s) {
  int n = (int)strlen(s);
  put(s, n);
  put("\n", 1);
  return n + 1;
}
