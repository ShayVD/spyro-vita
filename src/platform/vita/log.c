/* The Vita log, ux0:data/spyro/spyro.log, written straight through sceIoWrite
 * (decisions D7: newlib's own fopen'd log stayed empty in Vita3K for kh1).
 * printf/puts and stdout/stderr go there too, and PsyX_Log* and host_log
 * (the linux32 files the Vita shares: gpu_soft.c, png.c). */
#include "vita.h"

#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static SceUID s_fd = -1;

static void put(const char *buf, int n) {
  if (s_fd >= 0 && n > 0)
    sceIoWrite(s_fd, buf, n);
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
