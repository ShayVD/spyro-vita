/* PS4 plan P2 spike (D180, docs/ps4-plan.md): does the PS4 put what the
 * low-image model (D173, D174) needs below 2 GB?
 *
 * The model: the game's pointer fields are 32 bits (P32, src/compat/p32.h)
 * and the decomp's (int) casts sign-extend, so the executable's image, the
 * 2 MB arena (a .bss array) and the game thread's stack (a .bss array too)
 * must all lie below 0x80000000. This program is not the game: it reports
 * where those land, the process's whole memory map, where malloc and mmap
 * put things, and what sceKernelMapFlexibleMemory / MapDirectMemory do when
 * asked for 0x80000000 (the plan's fallback question). Results go to
 * /data/spyro-spike/spike.log, to stdout (GoldHEN's klog) and, in short, to
 * the screen as system notifications. It contains no game data.
 *
 * Built by tools/ps4/spike/build.sh with OpenOrbis (docs/ps4-build.md). */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <orbis/libkernel.h>

#define LOW_LIMIT 0x80000000ull
#define ARENA_SIZE (2u << 20) /* the game's arena (arena.c) */
#define STACK_SIZE (1u << 20)
#define LOG_DIR "/data/spyro-spike"
#define LOG_PATH LOG_DIR "/spike.log"
#ifndef SPIKE_COMMIT
#define SPIKE_COMMIT "unknown"
#endif
/* sceKernelMap*Memory flags: fixed address, and never replace a mapping */
#define MAP_FIXED_FLAG 0x10
#define MAP_NO_OVERWRITE_FLAG 0x80
#define FIXED_FLAGS (MAP_FIXED_FLAG | MAP_NO_OVERWRITE_FLAG)
#define SPIKE_VERSION "p2-spike 1, commit " SPIKE_COMMIT

/* link.x defines __text_start (the image's first byte after its headers);
 * build.sh defines __bss_start and _end with --defsym from .bss, the last
 * section: lld doesn't define them under link.x with -pie, and weak
 * references won't do (create-fself turns them into imports and fails).
 * The image's true base is the eboot's first segment (modules() below). */
extern char __text_start[];
extern char __bss_start[];
extern char _end[];

static unsigned char g_arena[ARENA_SIZE] __attribute__((aligned(64)));
static unsigned char g_sce_stack[STACK_SIZE] __attribute__((aligned(16384)));
static unsigned char g_posix_stack[STACK_SIZE] __attribute__((aligned(16384)));
static int g_data_word = 0x5350; /* .data */
static const char g_rodata[] = "rodata";

/* a decomp-style 32-bit pointer field (P32 = __ptr32 __uptr on x86-64 clang) */
struct P32Probe {
  int tag;
  unsigned char *__ptr32 __uptr p;
  int after;
};
_Static_assert(sizeof(struct P32Probe) == 12, "P32 field is 4 bytes");

static FILE *s_log;
static int s_fail;        /* something the low-image model needs is >= 2 GB */
static int s_unmeasured;  /* something it needs couldn't be measured */
static char s_summary[900];

static void out(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void out(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vprintf(fmt, ap);
  va_end(ap);
  if (s_log) {
    va_start(ap, fmt);
    vfprintf(s_log, fmt, ap);
    va_end(ap);
    fflush(s_log);
  }
}

static void notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void notify(const char *fmt, ...) {
  OrbisNotificationRequest req;
  va_list ap;
  memset(&req, 0, sizeof req);
  req.type = NotificationRequest;
  req.targetId = -1;
  req.useIconImageUri = 1;
  strcpy(req.iconUri, "cxml://psnotification/tex_icon_system");
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, ap);
  va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
  out("notify: %s\n", req.message);
}

static void summary(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void summary(const char *fmt, ...) {
  size_t n = strlen(s_summary);
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(s_summary + n, sizeof s_summary - n, fmt, ap);
  va_end(ap);
}

/* one address the model needs: [p, p + size) must be below 2 GB */
static void need(const char *what, const void *p, size_t size) {
  uint64_t a = (uint64_t)(uintptr_t)p, e = a + size;
  int low = a != 0 && e <= LOW_LIMIT;
  if (!a)
    s_unmeasured++;
  else if (!low)
    s_fail++;
  out("  %-34s 0x%012llx .. 0x%012llx  %s\n", what, (unsigned long long)a,
      (unsigned long long)e, !a ? "UNMEASURED" : low ? "below 2 GB" : "ABOVE 2 GB");
}

static void info(const char *what, const void *p) {
  out("  %-34s 0x%012llx\n", what, (unsigned long long)(uintptr_t)p);
}

/* --- threads on .bss stacks ------------------------------------------- */
struct ThreadReport {
  void *local;   /* address of a local on the thread's stack */
  int ok;
};

static void *thread_body(void *arg) {
  struct ThreadReport *r = arg;
  volatile int local = 42;
  r->local = (void *)&local;
  r->ok = local == 42;
  return NULL;
}

static void sce_thread(void) {
  OrbisPthreadAttr attr;
  OrbisPthread th;
  struct ThreadReport r = {0};
  int e1, e2 = -1, e3 = -1;
  e1 = scePthreadAttrInit(&attr);
  if (!e1)
    e2 = scePthreadAttrSetstack(&attr, g_sce_stack, sizeof g_sce_stack);
  if (!e1 && !e2)
    e3 = scePthreadCreate(&th, &attr, thread_body, &r, "spike_sce");
  if (!e3)
    scePthreadJoin(th, NULL);
  out("scePthreadAttrSetstack: init 0x%x setstack 0x%x create 0x%x\n", e1, e2, e3);
  need("scePthread .bss stack array", g_sce_stack, sizeof g_sce_stack);
  need("  its thread's local", r.local, sizeof(int));
  if (r.local && ((unsigned char *)r.local < g_sce_stack ||
                  (unsigned char *)r.local >= g_sce_stack + sizeof g_sce_stack)) {
    out("  the thread's local is NOT inside the .bss stack\n");
    s_fail++;
  }
  summary("sce stack: %s\n", e3 ? "FAILED" : r.local ? "ok" : "?");
}

static void posix_thread(void) {
  pthread_attr_t attr;
  pthread_t th;
  struct ThreadReport r = {0};
  int e1, e2 = -1, e3 = -1;
  e1 = pthread_attr_init(&attr);
  if (!e1)
    e2 = pthread_attr_setstack(&attr, g_posix_stack, sizeof g_posix_stack);
  if (!e1 && !e2)
    e3 = pthread_create(&th, &attr, thread_body, &r);
  if (!e3)
    pthread_join(th, NULL);
  out("pthread_attr_setstack: init %d setstack %d create %d\n", e1, e2, e3);
  info("pthread .bss stack array", g_posix_stack);
  info("  its thread's local", r.local);
  /* informative: the game can use either API */
}

/* --- the memory map ----------------------------------------------------- */
static void memory_map(void) {
  OrbisKernelVirtualQueryInfo q;
  const char *addr = NULL;
  int n = 0;
  out("\nsceKernelVirtualQuery map (start .. end prot flags name):\n");
  while (n < 400) {
    memset(&q, 0, sizeof q);
    if (sceKernelVirtualQuery(addr, 1 /* find next */, &q, sizeof q) != 0)
      break;
    out("  0x%012llx .. 0x%012llx  prot 0x%02x  %s%s%s%s  %.32s\n",
        (unsigned long long)(uintptr_t)q.unk01, (unsigned long long)(uintptr_t)q.unk02,
        (unsigned)q.unk04, q.isFlexibleMemory ? "flex " : "", q.isDirectMemory ? "direct " : "",
        q.isStack ? "stack " : "", q.isCommitted ? "committed" : "", q.name);
    if ((const char *)q.unk02 <= addr)
      break;
    addr = q.unk02;
    n++;
  }
  out("  (%d regions)\n", n);
}

static void modules(void) {
  OrbisKernelModule h[64];
  size_t count = 0, i;
  unsigned j;
  out("\nmodules (sceKernelGetModuleList):\n");
  if (sceKernelGetModuleList(h, 64, &count) != 0) {
    out("  sceKernelGetModuleList failed\n");
    return;
  }
  for (i = 0; i < count && i < 64; i++) {
    OrbisKernelModuleInfo mi;
    memset(&mi, 0, sizeof mi);
    mi.size = sizeof mi;
    if (sceKernelGetModuleInfo(h[i], &mi) != 0)
      continue;
    out("  %-28.28s", mi.name);
    for (j = 0; j < mi.segmentCount && j < 4; j++)
      out("  0x%012llx+0x%x", (unsigned long long)(uintptr_t)mi.segmentInfo[j].address,
          mi.segmentInfo[j].size);
    out("\n");
  }
}

/* --- mapping at 0x80000000 ---------------------------------------------- */
static void flexible(void) {
  static const uint64_t want[] = {0x80000000ull, 0x40000000ull};
  size_t len = 0x10000;
  unsigned i;
  int fixed;
  size_t avail = 0;
  out("\nflexible memory: sceKernelAvailableFlexibleMemorySize -> 0x%x, %zu bytes\n",
      sceKernelAvailableFlexibleMemorySize((size_t)&avail), avail);
  for (i = 0; i < sizeof want / sizeof want[0]; i++)
    for (fixed = 0; fixed < 2; fixed++) {
      void *p = (void *)(uintptr_t)want[i];
      int r = sceKernelMapFlexibleMemory(&p, len, ORBIS_KERNEL_PROT_CPU_RW, fixed ? FIXED_FLAGS : 0);
      out("  MapFlexibleMemory(0x%llx, 0x%zx, RW, %s) -> 0x%08x, addr 0x%012llx\n",
          (unsigned long long)want[i], len, fixed ? "FIXED|NO_OVERWRITE" : "0", (unsigned)r,
          (unsigned long long)(uintptr_t)p);
      if (i == 0)
        summary("flex@2G%s: 0x%x -> %llx\n", fixed ? " fixed" : "", (unsigned)r,
                (unsigned long long)(uintptr_t)p);
      if (r == 0) {
        volatile unsigned *w = p;
        *w = 0x1234;
        out("    write/read back %s\n", *w == 0x1234 ? "ok" : "MISMATCH");
        munmap(p, len);
      }
    }
}

static void direct(void) {
  size_t len = 0x10000;
  off_t phys = 0;
  int r = sceKernelAllocateDirectMemory(0, (off_t)sceKernelGetDirectMemorySize(), len, len,
                                        0 /* WB_ONION */, &phys);
  out("\ndirect memory: size 0x%zx, AllocateDirectMemory(0x%zx) -> 0x%08x, phys 0x%llx\n",
      sceKernelGetDirectMemorySize(), len, (unsigned)r, (unsigned long long)phys);
  if (r == 0) {
    int fixed;
    for (fixed = 0; fixed < 2; fixed++) {
      void *p = (void *)(uintptr_t)0x80000000ull;
      int m = sceKernelMapDirectMemory(&p, len, ORBIS_KERNEL_PROT_CPU_RW, fixed ? FIXED_FLAGS : 0,
                                       phys, len);
      out("  MapDirectMemory(0x80000000, 0x%zx, RW, %s) -> 0x%08x, addr 0x%012llx\n", len,
          fixed ? "FIXED|NO_OVERWRITE" : "0", (unsigned)m, (unsigned long long)(uintptr_t)p);
      if (m == 0)
        munmap(p, len);
    }
    sceKernelReleaseDirectMemory(phys, len);
  }
}

/* --- host allocations (they must never reach a P32 field) --------------- */
static void host_memory(void) {
  void *small = malloc(64), *big = malloc(4u << 20);
  void *m = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  int local;
  out("\nhost memory (informative: the game must not hand these to P32 fields):\n");
  info("malloc(64)", small);
  info("malloc(4 MB)", big);
  info("mmap(64 KB)", m == MAP_FAILED ? NULL : m);
  info("main thread's local", &local);
  free(small);
  free(big);
  if (m != MAP_FAILED)
    munmap(m, 0x10000);
}

/* --- __ptr32 on the target ----------------------------------------------- */
static void p32_probe(void) {
  static struct P32Probe probe; /* .bss */
  unsigned char *back;
  probe.tag = 0x11;
  probe.p = g_arena + 0x1234;
  probe.after = 0x22;
  back = probe.p;
  out("\n__ptr32 probe: stored %p, read back %p, raw field 0x%08x, neighbours %x %x: %s\n",
      (void *)(g_arena + 0x1234), (void *)back, *(unsigned *)((char *)&probe + 4), probe.tag,
      probe.after, back == g_arena + 0x1234 && probe.after == 0x22 ? "ok" : "WRONG");
  if (back != g_arena + 0x1234)
    s_fail++;
  summary("ptr32: %s\n", back == g_arena + 0x1234 ? "ok" : "WRONG");
}

int main(void) {
  int local;
  mkdir(LOG_DIR, 0777);
  s_log = fopen(LOG_PATH, "w");
  setvbuf(stdout, NULL, _IONBF, 0);
  notify("Spyro PS4 spike: measuring memory layout...");
  out("%s\nlog: %s%s\n", SPIKE_VERSION, LOG_PATH,
      s_log ? "" : " (COULD NOT OPEN: errno set)");
  if (!s_log)
    out("fopen errno %d\n", errno);
  {
    OrbisKernelSwVersion v;
    memset(&v, 0, sizeof v);
    v.Size = sizeof v;
    if (sceKernelGetSystemSwVersion(&v) == 0)
      out("firmware: %.28s (0x%08x)\n", v.VersionString, v.Version);
    else
      out("firmware: sceKernelGetSystemSwVersion failed\n");
  }

  out("\nimage (link.x / lld symbols):\n");
  need("__text_start (image start)", __text_start, 1);
  info(".rodata string", g_rodata);
  need(".data word", &g_data_word, sizeof g_data_word);
  info("__bss_start", __bss_start);
  need("_end (image end)", _end, 1);
  need(".bss 2 MB arena array", g_arena, sizeof g_arena);
  info("main()", (void *)main);
  info("main thread's local", &local);

  out("\nthreads:\n");
  sce_thread();
  posix_thread();
  p32_probe();
  host_memory();
  /* the map before the mapping experiments, in case one of them crashes */
  modules();
  memory_map();
  flexible();
  direct();

  out("\nVERDICT: %s (%d above 2 GB, %d unmeasured)\n",
      s_fail ? "LOW-IMAGE MODEL FAILS" : s_unmeasured ? "INCOMPLETE" : "LOW-IMAGE MODEL HOLDS",
      s_fail, s_unmeasured);
  notify("Image 0x%llx..0x%llx\nArena 0x%llx\nStack 0x%llx",
         (unsigned long long)(uintptr_t)__text_start, (unsigned long long)(uintptr_t)_end,
         (unsigned long long)(uintptr_t)g_arena, (unsigned long long)(uintptr_t)g_sce_stack);
  notify("%s", s_summary);
  notify("%s\nLog: %s\nClose the app with the PS button.",
         s_fail ? "LOW-IMAGE MODEL FAILS" : s_unmeasured ? "INCOMPLETE" : "LOW-IMAGE MODEL HOLDS",
         s_log ? LOG_PATH : "(log not written)");
  if (s_log)
    fclose(s_log);
  for (;;)
    sceKernelUsleep(1000000);
}
