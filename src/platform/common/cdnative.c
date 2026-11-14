/* Native disc reads (cd_native=1, decisions D93, docs/native-plan.md stage
 * 6). libcd.c keeps the CD model the game sees: Setloc, the head, when a
 * CdRead completes (its latency in ticks) and the callback, and the XA
 * stream's position, mode and filter, all on the game thread. This file only
 * replaces how the bytes are fetched.
 *
 * CdRead: the LBA range is cut where files end (psx_cd_locate: the manifest's
 * table, or the image's ISO directory) and each piece is one read of a file
 * at an offset, on a worker thread with its own file handles: straight into
 * the game's buffer from an extracted data file, or through a scratch buffer
 * of whole raw sectors from the image (or an XA file), 2048 bytes kept of
 * each. Whole sectors are written, as libcd.c's per-sector copy does
 * (docs/porting-plan.md §4.4), with the same zeros past a file's end and for
 * sectors no file holds, so the buffer gets the same bytes. libcd.c waits
 * for the read where it completed the old one (psx_cdn_read_finish).
 *
 * The XA stream: from where a ReadS starts, chunks of CHUNK sectors are read
 * ahead of the head in order, NCHUNK of them in a ring (chunk k of the
 * stream in slot k % NCHUNK, so the slot a new chunk needs holds one the head
 * has left). A chunk keeps each sector's subheader, which is all
 * psx_xa_check_filter looks at, and the whole sector only for those the
 * stream's filter plays (one channel of the interleave). So the game thread
 * learns "doesn't play" from the subheader, and gets the bytes of the
 * sectors that play; data sectors are never read. A sector a chunk can't
 * answer (not read yet, the filter changed) is read on the game thread
 * through psx_cd_read_raw, so every answer is the one psx_xa_accept gives.
 * With mixer=3 the worker also decodes the kept sectors to PCM, chained from
 * the stream's start; the mixer uses the PCM only if the history matches
 * (psx_xa_decoded), and a sector handed over with PCM pins its slot until
 * psx_cdn_xa_release. This replaces the Vita's xa_ahead.c windows (D64, D78).
 *
 * The files are the disc's and never written, so nothing read can go stale;
 * the worker reads only tables fixed once the disc is open. Plain C plus
 * sceIo and sceKernel threads on the Vita, POSIX pread and threads on the
 * linux32 host. */
#include "psx_cd.h"
#include "psx_cdimage.h"
#include "psx_cdnative.h"
#include "psx_pbp.h"
#include "psx_spu.h"
#include "psx_xa.h"

#include <libcd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __vita__
#include <psp2/io/fcntl.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/threadmgr.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#endif

#define CHUNK 32     /* stream sectors per chunk: ~13 vblanks at double speed */
#define NCHUNK 4     /* reach: 128 sectors, as xa_ahead.c's four windows */
#define KEEP 8       /* sectors a chunk keeps: the music interleaves 8 channels, 4 of 32 play */
#define SCRATCH 128  /* raw sectors per read through the scratch buffer (301 KB at 2352) */
#define MAX_STRIDE 2448
#define NFD 24       /* the worker's open files: the disc has 16 */

int g_psx_cd_native;
volatile int g_psx_cdn_busy;

/* ---- the platform: files, a thread, two semaphores ---- */

#ifdef __vita__
#define PRIORITY 0x88 /* core 2, below the render thread (0x80): from the digital copy the
                      * stream's chunks are 50 ms decodes, and on core 1 at 0x90 they held up the
                      * walk the game thread joins every tick (D158's stutter) */
static SceUID s_work, s_done, s_thread;
static int fs_open(const char *path) {
  SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
  return fd < 0 ? -1 : (int)fd;
}
static int fs_pread(int fd, void *buf, int n, long off) {
  int got = (int)sceIoPread(fd, buf, (SceSize)n, (SceOff)off);
  return got < 0 ? 0 : got;
}
static void sem_signal(SceUID s) { sceKernelSignalSema(s, 1); }
static void sem_take(SceUID s) { sceKernelWaitSema(s, 1, NULL); }
#else
static sem_t s_work_sem, s_done_sem;
static sem_t *const s_work = &s_work_sem, *const s_done = &s_done_sem;
static pthread_t s_thread;
static int fs_open(const char *path) { return open(path, O_RDONLY); }
static int fs_pread(int fd, void *buf, int n, long off) {
  int got = 0;
  while (got < n) {
    ssize_t r = pread(fd, (char *)buf + got, (size_t)(n - got), (off_t)off + got);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0)
      break;
    got += (int)r;
  }
  return got;
}
static void sem_signal(sem_t *s) { sem_post(s); }
static void sem_take(sem_t *s) {
  while (sem_wait(s) != 0 && errno == EINTR)
    ;
}
#endif

static int s_started;
static unsigned char *s_scratch; /* the worker's */

/* the worker's file handles, by path */
static struct {
  char path[512];
  int fd;
} s_fd[NFD];
static int s_nfd;

static int fd_for(const char *path) {
  int i;
  for (i = 0; i < s_nfd; i++)
    if (!strcmp(s_fd[i].path, path))
      return s_fd[i].fd;
  if (s_nfd == NFD)
    return fs_open(path); /* not expected: a leak per call beats failing */
  snprintf(s_fd[s_nfd].path, sizeof s_fd[s_nfd].path, "%s", path);
  s_fd[s_nfd].fd = fs_open(path);
  if (s_fd[s_nfd].fd < 0)
    fprintf(stderr, "libcd: cd_native: cannot open %s\n", path);
  return s_fd[s_nfd++].fd;
}

static PsxCdnStats s_st; /* the worker's fields and the game thread's, each one writer */

static int timed_pread(int fd, void *buf, int n, long off, unsigned *calls, unsigned *kb,
                       unsigned long long *us) {
  unsigned long long t0 = psx_cd_now();
  int got = fd >= 0 ? fs_pread(fd, buf, n, off) : 0;
  (*calls)++;
  *kb += (unsigned)got / 1024;
  *us += psx_cd_now() - t0;
  return got;
}

/* ---- the digital copy (D157, D158) ---- */

int g_psx_cdn_helper = 1;

/* The worker's own handle on the copy and the helper thread's, made at their
 * first read; psx_cdimage_close drops them (psx_cdn_pbp_drop, after the
 * worker is quiet), so a clone is always of the copy that is open. Each
 * thread publishes its handle's counts after a read (s_snap), so the game
 * thread's stats never touch a handle another thread owns. */
static PsxPbp *s_pbp, *s_pbp2;
static PsxPbpStats s_snap[2];  /* the worker's and the helper's, each one writer */
static PsxPbpStats s_pbp_gone; /* the counts of handles dropped since */
static int s_pbp_failed;
/* the gate (D158): open while the game thread's handle may be cloned; shut by
 * psx_cdn_quiesce before that handle goes, and read by the worker after it
 * raised g_psx_cdn_busy, so a worker that wakes late sees it shut */
static volatile int s_pbp_gate = 1;

static void add_stats(PsxPbpStats *sum, const PsxPbpStats *st) {
  sum->blocks += st->blocks, sum->hits += st->hits, sum->reads += st->reads;
  sum->read_bytes += st->read_bytes, sum->read_us += st->read_us, sum->decode_us += st->decode_us;
}

static PsxPbp *clone_of_open(PsxPbp **slot) {
  PsxPbp *game;
  if (!__atomic_load_n(&s_pbp_gate, __ATOMIC_SEQ_CST) || !(game = psx_cdimage_pbp()))
    return NULL;
  if (!*slot && psx_pbp_clone(game, slot) != 0 && !s_pbp_failed++)
    fprintf(stderr, "libcd: cd_native: no handle of its own on %s: its reads give zeros\n", psx_pbp_path(game));
  return *slot;
}

void psx_cdn_quiesce(void) {
  __atomic_store_n(&s_pbp_gate, 0, __ATOMIC_SEQ_CST);
  if (!s_started)
    return;
  psx_cdn_read_drain();
  while (__atomic_load_n(&g_psx_cdn_busy, __ATOMIC_SEQ_CST)) { /* a stream chunk in progress: let it finish */
#ifdef __vita__
    sceKernelDelayThread(1000);
#else
    usleep(1000);
#endif
  }
}

void psx_cdn_pbp_drop(void) {
  PsxPbp **slots[2] = {&s_pbp, &s_pbp2};
  int i;
  for (i = 0; i < 2; i++) {
    if (*slots[i]) {
      psx_pbp_stats(*slots[i], &s_snap[i]);
      psx_pbp_close(*slots[i]);
      *slots[i] = NULL;
    }
    add_stats(&s_pbp_gone, &s_snap[i]);
    memset(&s_snap[i], 0, sizeof s_snap[i]);
  }
}

void psx_cdn_pbp_opened(void) { __atomic_store_n(&s_pbp_gate, 1, __ATOMIC_SEQ_CST); }

void psx_cdn_pbp_stats(PsxPbpStats *out) {
  *out = s_pbp_gone; /* the snapshots are read across threads: approximate while it runs */
  add_stats(out, &s_snap[0]);
  add_stats(out, &s_snap[1]);
}

int psx_cdn_pbp_report(char *out, size_t cap) {
  PsxPbpStats g, w;
  if (!psx_cdimage_pbp())
    return 0;
  psx_cdimage_pbp_stats(&g);
  psx_cdn_pbp_stats(&w);
  snprintf(out, cap,
           "cd: digital copy: %u blocks decoded in %u ms (%u from the cache), %u file reads of %u KB in %u ms; "
           "the worker %u blocks in %u ms, %u reads of %u KB in %u ms",
           g.blocks, (unsigned)(g.decode_us / 1000), g.hits, g.reads, (unsigned)(g.read_bytes >> 10),
           (unsigned)(g.read_us / 1000), w.blocks, (unsigned)(w.decode_us / 1000), w.reads,
           (unsigned)(w.read_bytes >> 10), (unsigned)(w.read_us / 1000));
  return 1;
}

/* The helper thread (D158): the second half of a long read of the copy is
 * decoded on another core while the worker decodes the first. Its own
 * semaphores and its own clone; it writes only its own part of s_scratch. */
#ifdef __vita__
static SceUID s_help_work, s_help_done, s_help_thread;
#else
static sem_t s_help_work_sem, s_help_done_sem;
static sem_t *const s_help_work = &s_help_work_sem, *const s_help_done = &s_help_done_sem;
static pthread_t s_help_thread;
#endif
static int s_help_started;
static struct {
  int lba, count;
  unsigned char *dst;
  int ok;
} s_help;

#ifdef __vita__
static int helper(SceSize args, void *argp) {
  (void)args, (void)argp;
#else
static void *helper(void *arg) {
  (void)arg;
#endif
  for (;;) {
    PsxPbp *p;
    sem_take(s_help_work);
    p = clone_of_open(&s_pbp2);
    s_help.ok = p && psx_pbp_read_sectors(p, s_help.lba, s_help.count, s_help.dst) == 0;
    if (p)
      psx_pbp_stats(p, &s_snap[1]);
    sem_signal(s_help_done);
  }
#ifdef __vita__
  return 0;
#else
  return NULL;
#endif
}

static int helper_start(void) {
  if (s_help_started)
    return 0;
#ifdef __vita__
  s_help_work = sceKernelCreateSema("spyro_cd_help", 0, 0, 1, NULL);
  s_help_done = sceKernelCreateSema("spyro_cd_helped", 0, 0, 1, NULL);
  /* core 1, below the parse thread (0x98) and the presenter (0x9A), which are idle during a load */
  s_help_thread = sceKernelCreateThread("spyro_cd_help", helper, 0x9C, 0x4000, 0, SCE_KERNEL_CPU_MASK_USER_1, NULL);
  if (s_help_work < 0 || s_help_done < 0 || s_help_thread < 0 || sceKernelStartThread(s_help_thread, 0, NULL) < 0) {
    if (s_help_thread >= 0)
      sceKernelDeleteThread(s_help_thread);
    if (s_help_done >= 0)
      sceKernelDeleteSema(s_help_done);
    if (s_help_work >= 0)
      sceKernelDeleteSema(s_help_work);
    return -1;
  }
#else
  if (sem_init(s_help_work, 0, 0))
    return -1;
  if (sem_init(s_help_done, 0, 0)) {
    sem_destroy(s_help_work);
    return -1;
  }
  if (pthread_create(&s_help_thread, NULL, helper, NULL)) {
    sem_destroy(s_help_work);
    sem_destroy(s_help_done);
    return -1;
  }
#endif
  s_help_started = 1;
  return 0;
}

/* m raw sectors from loc's sector first_rel on into s_scratch: an image's
 * by offset, the digital copy's through its decoder, split at a block
 * boundary with the helper when it is long enough. Returns the bytes. */
static int read_raw(const PsxCdLoc *loc, int first_rel, int m, unsigned *calls, unsigned *kb,
                    unsigned long long *us) {
  if (loc->pbp) {
    unsigned long long t0 = psx_cd_now();
    PsxPbp *p = clone_of_open(&s_pbp);
    int first = (int)loc->offset + first_rel, got = 0, mine = m, split = 0;
    /* a CdRead's piece (up to 8 blocks), not the stream's 2-block chunks: those
     * would lose the cached boundary block between one chunk and the next */
    if (p && s_help_started && m >= 4 * PSX_PBP_BLOCK_SECTORS) {
      int mid = (first + m / 2) & ~(PSX_PBP_BLOCK_SECTORS - 1); /* the helper's half starts on a block */
      if (mid > first && mid < first + m) {
        s_help.lba = mid;
        s_help.count = first + m - mid;
        s_help.dst = s_scratch + (size_t)(mid - first) * PSX_PBP_SECTOR;
        mine = mid - first;
        split = 1;
        sem_signal(s_help_work);
      }
    }
    if (p && psx_pbp_read_sectors(p, first, mine, s_scratch) == 0)
      got = mine * PSX_PBP_SECTOR;
    if (p)
      psx_pbp_stats(p, &s_snap[0]);
    if (split) {
      sem_take(s_help_done);
      got = got == mine * PSX_PBP_SECTOR && s_help.ok ? m * PSX_PBP_SECTOR : 0;
    }
    (*calls)++;
    *kb += (unsigned)got / 1024;
    *us += psx_cd_now() - t0;
    return got;
  }
  return timed_pread(fd_for(loc->path), s_scratch, m * loc->stride,
                     loc->offset + (long)first_rel * loc->stride, calls, kb, us);
}

/* ---- CdRead ---- */

enum { J_IDLE, J_WANTED, J_DONE };
static struct {
  volatile int state;
  int lba, count;
  unsigned char *dst;
  int outside, first_outside; /* sectors no file holds */
} s_job;

static void read_range(int lba, int count, unsigned char *dst) {
  while (count > 0) {
    PsxCdLoc loc;
    int n, fd;
    if (psx_cd_locate(lba, &loc) != 0) { /* zeros, as psx_cd_read_raw's */
      if (!s_job.outside++)
        s_job.first_outside = lba;
      memset(dst, 0, 2048);
      lba++, dst += 2048, count--;
      continue;
    }
    n = count < loc.left ? count : loc.left;
    if (loc.stride == 2048) { /* an extracted data file: its bytes are the sectors' */
      int want = n * 2048;
      fd = fd_for(loc.path);
      int got = timed_pread(fd, dst, want, loc.offset, &s_st.reads, &s_st.kbytes, &s_st.us);
      memset(dst + got, 0, (size_t)(want - got));
    } else {
      int done = 0;
      while (done < n) {
        int m = n - done < SCRATCH ? n - done : SCRATCH, k;
        int got = read_raw(&loc, done, m, &s_st.reads, &s_st.kbytes, &s_st.us);
        for (k = 0; k < m; k++) {
          unsigned char *out = dst + (size_t)(done + k) * 2048;
          long have = (long)got - (long)k * loc.stride - loc.data_skip;
          if (loc.data_limit >= 0) {
            long left = loc.data_limit - (long)(done + k) * 2048;
            if (have > left)
              have = left;
          }
          if (have < 0)
            have = 0;
          if (have > 2048)
            have = 2048;
          memcpy(out, s_scratch + (size_t)k * loc.stride + loc.data_skip, (size_t)have);
          memset(out + have, 0, (size_t)(2048 - have));
        }
        done += m;
      }
    }
    lba += n, dst += (size_t)n * 2048, count -= n;
  }
}

void psx_cdn_read_post(int lba, int count, void *dst) {
  psx_cdn_read_drain(); /* one CdRead at a time, as the drive */
  s_job.lba = lba;
  s_job.count = count;
  s_job.dst = (unsigned char *)dst;
  s_job.outside = 0;
  __atomic_store_n(&s_job.state, J_WANTED, __ATOMIC_RELEASE);
  sem_signal(s_work);
}

static void job_wait(void) {
  if (__atomic_load_n(&s_job.state, __ATOMIC_ACQUIRE) == J_IDLE)
    return;
  sem_take(s_done); /* the worker posts it once per job */
  if (s_job.outside)
    fprintf(stderr, "libcd: read of LBA %d outside any file (%d sectors)\n", s_job.first_outside,
            s_job.outside);
  __atomic_store_n(&s_job.state, J_IDLE, __ATOMIC_RELEASE);
}

int psx_cdn_read_done(void) { return __atomic_load_n(&s_job.state, __ATOMIC_ACQUIRE) != J_WANTED; }
void psx_cdn_read_drain(void) {
  if (s_started)
    job_wait();
}

void psx_cdn_read_finish(int lba, int count, void *dst) {
  if (__atomic_load_n(&s_job.state, __ATOMIC_ACQUIRE) == J_IDLE || s_job.lba != lba ||
      s_job.count != count || s_job.dst != dst)
    psx_cdn_read_post(lba, count, dst);
  job_wait();
}

/* ---- the XA stream ---- */

enum { C_FREE, C_WANTED, C_READY };
typedef struct {
  volatile int state; /* the game thread: FREE/READY -> WANTED; the worker: WANTED -> READY */
  int lba;            /* first sector */
  int start;          /* lba is where the stream started: the decoder's history is zero */
  int mode, file, chan, predecode; /* the stream's filter when asked for; mixer=3 */
  unsigned char known[CHUNK];      /* READY: the sector was read */
  unsigned char sub[CHUNK][4];     /* its subheader (a data sector's made up, none: zeros) */
  signed char keep[CHUNK];         /* index into raw and pcm, or -1 */
  int nkeep;
  volatile int pins;
  unsigned char raw[KEEP][PSX_CD_RAW_SECTOR];
  struct PsxXaPcm pcm[KEEP];
} Chunk;

static Chunk *s_chunk;
static int s_base; /* the game thread's: where the stream started */

/* the worker's: where its last decoded chunk ended, and the history there */
static int s_chain_lba = -1, s_chain_file, s_chain_chan;
static int s_chain_hist[2][2];

static void decode(Chunk *c, int all_kept) {
  int k, hist[2][2];
  unsigned long long t0 = psx_cd_now();
  if (!c->start && c->lba == s_chain_lba && c->file == s_chain_file && c->chan == s_chain_chan)
    memcpy(hist, s_chain_hist, sizeof hist);
  else
    memset(hist, 0, sizeof hist); /* the stream's start (psx_xa_reset), or a guess */
  for (k = 0; k < c->nkeep; k++) {
    psx_xa_predecode(c->raw[k], (const int(*)[2])hist, &c->pcm[k]);
    memcpy(hist, c->pcm[k].exit, sizeof hist);
  }
  s_chain_lba = all_kept ? c->lba + CHUNK : -1;
  s_chain_file = c->file;
  s_chain_chan = c->chan;
  memcpy(s_chain_hist, hist, sizeof hist);
  s_st.decoded += (unsigned)c->nkeep;
  s_st.decode_us += psx_cd_now() - t0;
}

static void fill(Chunk *c) {
  int i = 0, all_kept = 1;
  memset(c->known, 0, sizeof c->known);
  memset(c->keep, -1, sizeof c->keep);
  c->nkeep = 0;
  s_st.chunks++;
  while (i < CHUNK) {
    PsxCdLoc loc;
    int m, k, got;
    if (psx_cd_locate(c->lba + i, &loc) != 0) { /* psx_cd_read_raw fails: never plays */
      memset(c->sub[i], 0, 4);
      c->known[i++] = 1;
      continue;
    }
    m = CHUNK - i < loc.left ? CHUNK - i : loc.left;
    if (!loc.xa || loc.stride > MAX_STRIDE) {
      for (k = 0; k < m; k++, i++) { /* data: the made-up subheader isn't audio */
        static const unsigned char data_sub[4] = {0, 0, 0x08, 0};
        memcpy(c->sub[i], data_sub, 4);
        c->known[i] = !loc.xa;
      }
      continue;
    }
    got = read_raw(&loc, 0, m, &s_st.chunk_reads, &s_st.chunk_kb, &s_st.chunk_us);
    for (k = 0; k < m; k++, i++) {
      const unsigned char *src = s_scratch + (size_t)k * loc.stride + loc.raw_skip;
      if ((long)k * loc.stride + loc.raw_skip + PSX_CD_RAW_SECTOR > got)
        continue; /* not read whole: the game thread's */
      memcpy(c->sub[i], src, 4);
      c->known[i] = 1;
      if (!psx_xa_check_filter(src, c->mode, c->file, c->chan))
        continue;
      if (c->nkeep == KEEP) {
        all_kept = 0;
        continue;
      }
      memcpy(c->raw[c->nkeep], src, PSX_CD_RAW_SECTOR);
      c->keep[i] = (signed char)c->nkeep++;
    }
  }
  if (c->predecode)
    decode(c, all_kept);
}

/* The wanted chunk the head reaches first. */
static Chunk *next_wanted(void) {
  Chunk *pick = NULL;
  int i;
  for (i = 0; i < NCHUNK; i++) {
    Chunk *c = &s_chunk[i];
    if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) == C_WANTED && (!pick || c->lba < pick->lba))
      pick = c;
  }
  return pick;
}

#ifdef __vita__
static int worker(SceSize args, void *argp) {
  (void)args, (void)argp;
#else
static void *worker(void *arg) {
  (void)arg;
#endif
  for (;;) {
    Chunk *c;
    __atomic_store_n(&g_psx_cdn_busy, 0, __ATOMIC_SEQ_CST);
    sem_take(s_work);
    __atomic_store_n(&g_psx_cdn_busy, 1, __ATOMIC_SEQ_CST); /* before any look at the gate (clone_of_open) */
    for (;;) { /* a CdRead first: the game thread may be waiting for it */
      if (__atomic_load_n(&s_job.state, __ATOMIC_ACQUIRE) == J_WANTED) {
        unsigned long long t0 = psx_cd_now();
        read_range(s_job.lba, s_job.count, s_job.dst);
        s_st.job_us += psx_cd_now() - t0;
        __atomic_store_n(&s_job.state, J_DONE, __ATOMIC_RELEASE);
        sem_signal(s_done);
      }
      if (!(c = next_wanted()))
        break;
      fill(c);
      __atomic_store_n(&c->state, C_READY, __ATOMIC_RELEASE);
      if (__atomic_load_n(&s_job.state, __ATOMIC_ACQUIRE) == J_WANTED)
        s_st.behind_chunk++; /* (or posted just after it: an upper bound) */
    }
  }
#ifdef __vita__
  return 0;
#else
  return NULL;
#endif
}

int psx_cdn_start(void) {
  if (s_started) {
    g_psx_cd_native = 1;
    return 0;
  }
  s_chunk = calloc(NCHUNK, sizeof *s_chunk);
  /* a scratch buffer in memory the CPU doesn't cache was tried (D158): no gain on the Vita */
  s_scratch = malloc((size_t)SCRATCH * MAX_STRIDE);
  if (!s_chunk || !s_scratch)
    goto fail;
  if (g_psx_cdn_helper && psx_cdimage_pbp() && helper_start() != 0)
    fprintf(stderr, "libcd: cd_native: no helper thread; the copy decoded on the worker alone\n");
#ifdef __vita__
  s_work = sceKernelCreateSema("spyro_cd_work", 0, 0, 1 << 20, NULL);
  s_done = sceKernelCreateSema("spyro_cd_done", 0, 0, 1, NULL);
  s_thread = sceKernelCreateThread("spyro_cd", worker, PRIORITY, 0x4000, 0,
                                   SCE_KERNEL_CPU_MASK_USER_2, NULL);
  if (s_work < 0 || s_done < 0 || s_thread < 0 || sceKernelStartThread(s_thread, 0, NULL) < 0)
    goto fail;
#else
  if (sem_init(s_work, 0, 0) || sem_init(s_done, 0, 0) ||
      pthread_create(&s_thread, NULL, worker, NULL))
    goto fail;
#endif
  s_started = 1;
  g_psx_cd_native = 1;
  return 0;
fail:
  fprintf(stderr, "libcd: cd_native: no worker thread; the sector reads as before\n");
  return -1;
}

int psx_cdn_thread(void) {
#ifdef __vita__
  return s_started ? (int)s_thread : 0;
#else
  return 0;
#endif
}

static int same_filter(const Chunk *c, const PsxCdStream *st) {
  return c->mode == st->mode && c->file == st->file && c->chan == st->chan &&
         c->predecode == g_psx_spu_native;
}

/* Chunks k .. k+NCHUNK-1 of the stream asked for. Slot k % NCHUNK holds
 * chunk k or one the head has left (k - NCHUNK or before, another stream's),
 * so a slot is taken unless the worker is reading it or the mixer still
 * holds its PCM; the next sector asks again. */
static void ask(int k) {
  const PsxCdStream *st = psx_cd_stream();
  int j;
  for (j = k; j < k + NCHUNK; j++) {
    Chunk *c = &s_chunk[j % NCHUNK];
    int lba = s_base + j * CHUNK, state = __atomic_load_n(&c->state, __ATOMIC_ACQUIRE);
    if (state != C_FREE && c->lba == lba && same_filter(c, st))
      continue;
    if (state == C_WANTED) {
      s_st.busy++;
      continue;
    }
    if (__atomic_load_n(&c->pins, __ATOMIC_ACQUIRE) > 0) {
      s_st.pinned++;
      continue;
    }
    c->lba = lba;
    c->start = j == 0;
    c->mode = st->mode;
    c->file = st->file;
    c->chan = st->chan;
    c->predecode = g_psx_spu_native;
    __atomic_store_n(&c->state, C_WANTED, __ATOMIC_RELEASE);
    sem_signal(s_work);
  }
}

void psx_cdn_stream_start(int lba) {
  if (!s_started)
    return;
  s_base = lba;
  if (psx_cd_stream()->mode & CdlModeRT)
    ask(0);
}

int psx_cdn_xa_accept(int lba, unsigned char *raw, const struct PsxXaPcm **pcm) {
  const PsxCdStream *st = psx_cd_stream();
  Chunk *c;
  int k, i;
  if (pcm)
    *pcm = NULL;
  if (!(st->mode & CdlModeRT))
    return 0;
  if (!s_started)
    goto here;
  if (lba < s_base)
    s_base = lba; /* not seen start (a state loaded mid-stream) */
  k = (lba - s_base) / CHUNK;
  c = &s_chunk[k % NCHUNK];
  i = lba - c->lba;
  if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) == C_READY && i >= 0 && i < CHUNK &&
      c->known[i]) {
    if (!psx_xa_check_filter(c->sub[i], st->mode, st->file, st->chan)) {
      s_st.hits++;
      ask(k);
      return 0;
    }
    if (c->keep[i] >= 0) {
      memcpy(raw, c->raw[(int)c->keep[i]], PSX_CD_RAW_SECTOR);
      if (pcm && c->predecode) {
        __atomic_add_fetch(&c->pins, 1, __ATOMIC_ACQ_REL);
        *pcm = &c->pcm[(int)c->keep[i]];
      }
      s_st.hits++;
      ask(k);
      return 1;
    }
  }
  s_st.misses++; /* the same bytes, read here */
  ask(k);
here:
  if (psx_cd_read_raw(lba, raw) != 0)
    return 0;
  return psx_xa_check(raw);
}

void psx_cdn_xa_release(const struct PsxXaPcm *pcm) {
  int i;
  for (i = 0; s_chunk && i < NCHUNK; i++) {
    Chunk *c = &s_chunk[i];
    if (pcm >= c->pcm && pcm < c->pcm + KEEP) {
      __atomic_sub_fetch(&c->pins, 1, __ATOMIC_ACQ_REL);
      return;
    }
  }
}

void psx_cdn_stats(PsxCdnStats *out) { *out = s_st; }
