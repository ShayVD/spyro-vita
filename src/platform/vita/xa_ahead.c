/* XA music read ahead of the stream on core 1 (config.txt xa_ahead=1,
 * decisions D64). The CD model stays on the game thread (libcd.c): the head
 * passes 2.5 sectors a vblank at double speed, and each one the stream plays
 * was a file-system read there (an fseek and fread of 2,336 bytes from the
 * extracted files, or a 32-sector window of the disc image now and then).
 *
 * Here a thread reads windows of WIN sectors ahead of the head, each with one
 * sceIoPread on its own file handle, and the game thread takes the sectors
 * from them. A sector not read ahead yet (the stream just started or jumped)
 * is read on the game thread as before, so every sector holds the same bytes
 * whichever way it came, and psx_xa_check decides what plays exactly as
 * psx_xa_accept did. Only the time the game thread spends in the file system
 * changes. Files are the disc's and never written, so a window never goes
 * stale.
 *
 * With the native mix (mixer=3, D84) the thread also decodes the sectors
 * the stream will play to PCM (psx_xa_predecode), each from the history the
 * one before it in the stream leaves: the stream's filter as it was when the
 * window was asked for, and the history chained on from the window before
 * when this one follows it. The mixer uses that PCM only if the history it
 * was decoded from is the stream's then (psx_xa_decoded), so a wrong guess
 * costs a decode on the mixer, not a different sample. A sector handed over
 * with its PCM pins its window until the mixer is done with it
 * (vita_xa_release): want() doesn't reuse a pinned window. */
#include "vita.h"
#include "watchdog.h"
#include "psx_cd.h"
#include "psx_spu.h"
#include "psx_xa.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libcd.h>

#define WIN 32     /* sectors per window: ~13 vblanks of double-speed stream */
#define NWIN 4
#define MAX_STRIDE 2448
#define PRIORITY 0x90 /* behind the mixer on core 1: reads mostly wait */
#define MAX_PCM 8     /* sectors decoded per window: Spyro's music interleaves 8 channels, 4 of 32 play */

enum { W_FREE, W_WANTED, W_READY };

typedef struct {
  volatile int state; /* the game thread: FREE/READY -> WANTED; the reader: WANTED -> READY */
  int lba;            /* first sector */
  int n;              /* READY: sectors held (0: none could be read) */
  unsigned char data[WIN][PSX_CD_RAW_SECTOR];
  /* mixer=3 (D84): the stream's filter when asked for; the PCM of the
   * sectors it plays (pcm_of: index into pcm, or -1); packets holding one */
  int predecode, mode, file, chan;
  signed char pcm_of[WIN];
  int npcm;
  volatile int pins;
  PsxXaPcm pcm[MAX_PCM];
} Window;

static Window *s_win;
static unsigned char *s_scratch; /* the reader's: WIN sectors as they are in the file */
static SceUID s_sema, s_thread;
static int s_on;

/* the reader's file handle: the last path it read */
static SceUID s_fd = -1;
static char s_fd_path[512];

/* stats since the last report (the reader's are counted up, read across threads) */
static unsigned s_hits, s_misses, s_noslot, s_pinned;
static volatile uint32_t s_rd_windows, s_rd_kb, s_rd_us, s_dec_sectors, s_dec_us;

/* the reader's: where its last decoded window ended, and the history there */
static int s_chain_lba = -1, s_chain_file, s_chain_chan;
static int s_chain_hist[2][2];

/* The sectors of w the stream plays, to PCM (the reader, w WANTED). */
static void predecode(Window *w) {
  int k, hist[2][2];
  SceUInt64 t0 = sceKernelGetProcessTimeWide();
  w->npcm = 0;
  memset(w->pcm_of, -1, sizeof w->pcm_of);
  if (w->lba == s_chain_lba && w->file == s_chain_file && w->chan == s_chain_chan)
    memcpy(hist, s_chain_hist, sizeof hist);
  else
    memset(hist, 0, sizeof hist); /* a stream starting (psx_xa_reset) */
  for (k = 0; k < w->n && w->npcm < MAX_PCM; k++) {
    PsxXaPcm *o = &w->pcm[w->npcm];
    if (!psx_xa_check_filter(w->data[k], w->mode, w->file, w->chan))
      continue;
    psx_xa_predecode(w->data[k], (const int(*)[2])hist, o);
    memcpy(hist, o->exit, sizeof hist);
    w->pcm_of[k] = (signed char)w->npcm++;
  }
  s_chain_lba = k == w->n ? w->lba + w->n : -1;
  s_chain_file = w->file;
  s_chain_chan = w->chan;
  memcpy(s_chain_hist, hist, sizeof hist);
  s_dec_sectors += (uint32_t)w->npcm;
  s_dec_us += (uint32_t)(sceKernelGetProcessTimeWide() - t0);
}

static int reader_main(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  for (;;) {
    int i;
    g_wd.xa = 0; /* breadcrumbs (watchdog.c, D73) */
    sceKernelWaitSema(s_sema, 1, NULL);
    g_wd.xa = 1;
    for (i = 0; i < NWIN; i++) {
      Window *w = &s_win[i];
      PsxCdXaLoc loc;
      int n = 0, got;
      SceUInt64 t0;
      if (__atomic_load_n(&w->state, __ATOMIC_ACQUIRE) != W_WANTED)
        continue;
      t0 = sceKernelGetProcessTimeWide();
      if (psx_cd_xa_locate(w->lba, &loc) == 0 && loc.stride <= MAX_STRIDE) {
        int want = loc.left < WIN ? loc.left : WIN;
        if (s_fd < 0 || strcmp(s_fd_path, loc.path)) {
          if (s_fd >= 0)
            sceIoClose(s_fd);
          s_fd = sceIoOpen(loc.path, SCE_O_RDONLY, 0);
          snprintf(s_fd_path, sizeof s_fd_path, "%s", loc.path);
        }
        got = s_fd >= 0 ? sceIoPread(s_fd, s_scratch, (SceSize)want * loc.stride, loc.offset) : -1;
        if (got > 0) {
          /* whole sectors only: a part one is the game thread's to read */
          n = got / loc.stride;
          if (got % loc.stride >= loc.skip + PSX_CD_RAW_SECTOR)
            n++;
          if (n > want)
            n = want;
          for (int k = 0; k < n; k++)
            memcpy(w->data[k], s_scratch + (size_t)k * loc.stride + loc.skip, PSX_CD_RAW_SECTOR);
          s_rd_kb += (unsigned)got / 1024;
        }
      }
      w->n = n;
      s_rd_windows++;
      s_rd_us += (uint32_t)(sceKernelGetProcessTimeWide() - t0);
      if (w->predecode) {
        predecode(w);
      } else {
        w->npcm = 0;
        memset(w->pcm_of, -1, sizeof w->pcm_of);
      }
      __atomic_store_n(&w->state, W_READY, __ATOMIC_RELEASE);
    }
  }
  return 0;
}

void vita_xa_ahead_init(void) {
  SceUID th;
  if (!g_vita_opts.xa_ahead) {
    vita_log("xa: music sectors read on the game thread (xa_ahead=0)");
    return;
  }
  s_win = calloc(NWIN, sizeof *s_win);
  s_scratch = malloc((size_t)WIN * MAX_STRIDE);
  s_sema = sceKernelCreateSema("spyro_xa_ahead", 0, 0, NWIN, NULL);
  th = sceKernelCreateThread("spyro_xa_ahead", reader_main, PRIORITY, 0x4000, 0,
                             SCE_KERNEL_CPU_MASK_USER_1, NULL);
  if (!s_win || !s_scratch || s_sema < 0 || th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("xa: no read-ahead thread (%08x); music sectors read on the game thread",
             (unsigned)th);
    return;
  }
  s_on = 1;
  s_thread = th;
  vita_watchdog_thread(WD_T_XA, th);
  vita_log("xa: music read ahead in %d-sector windows on core 1, priority 0x%x (xa_ahead=1, D64)",
           WIN, PRIORITY);
}

/* The window that holds or will hold lba (WANTED counts for its whole
 * span), or NULL; *end: where its sectors end. */
static Window *covering(int lba, int *end) {
  int i;
  for (i = 0; i < NWIN; i++) {
    Window *w = &s_win[i];
    int st = __atomic_load_n(&w->state, __ATOMIC_ACQUIRE);
    int n = st == W_READY ? w->n : st == W_WANTED ? WIN : 0;
    if (lba >= w->lba && lba < w->lba + n) {
      *end = w->lba + n;
      return w;
    }
  }
  return NULL;
}

/* Ask for the window from lba, in a slot the reader isn't filling: one
 * that's free, or else a ready one out of the read-ahead's reach. That is
 * one wholly behind the head (the lowest first), or else one that starts
 * REACH or more sectors ahead of it (the farthest first): after the stream
 * jumps back (a new track earlier on the disc, a track's loop) the windows
 * it filled are all ahead, and the head may never get there (D78). One
 * nearer stays: the head reaches it before the windows asked for now. */
#define REACH (NWIN * WIN)
static void want(int lba, int head) {
  Window *pick = NULL;
  int i, best = -1;
  for (i = 0; i < NWIN; i++) {
    Window *w = &s_win[i];
    int st = __atomic_load_n(&w->state, __ATOMIC_ACQUIRE), score;
    if (st != W_FREE && w->lba == lba)
      return; /* asked already (a window that couldn't be read stays asked) */
    if (st == W_FREE)
      score = INT_MAX;
    else if (st != W_READY)
      continue;
    else if (__atomic_load_n(&w->pins, __ATOMIC_ACQUIRE) > 0) {
      s_pinned++; /* a packet still holds its PCM (D84) */
      continue;
    }
    else if (w->lba + w->n <= head)
      score = INT_MAX / 2 + (head - w->lba);
    else if (w->lba >= head + REACH)
      score = w->lba - head;
    else
      continue;
    if (score > best)
      best = score, pick = w;
  }
  if (!pick) {
    s_noslot++;
    return; /* all near ahead or being read: the next sector asks again */
  }
  pick->lba = lba;
  {
    const PsxCdStream *st = psx_cd_stream();
    pick->predecode = g_psx_spu_native;
    pick->mode = st->mode;
    pick->file = st->file;
    pick->chan = st->chan;
  }
  __atomic_store_n(&pick->state, W_WANTED, __ATOMIC_RELEASE);
  sceKernelSignalSema(s_sema, 1);
}

/* psx_xa_accept for the mixer's psx_xa_sector (mixer.c): 1 if the stream
 * plays the sector at lba, which is then in raw. */
int vita_xa_accept(int lba, unsigned char *raw, const PsxXaPcm **pcm) {
  int i, next, end, got = 0;
  if (pcm)
    *pcm = NULL;
  if (!s_on)
    return psx_xa_accept(lba, raw);
  if (!(psx_cd_stream()->mode & CdlModeRT))
    return 0;
  for (i = 0; i < NWIN && !got; i++) {
    Window *w = &s_win[i];
    if (__atomic_load_n(&w->state, __ATOMIC_ACQUIRE) == W_READY && lba >= w->lba &&
        lba < w->lba + w->n) {
      memcpy(raw, w->data[lba - w->lba], PSX_CD_RAW_SECTOR);
      got = 1;
      if (pcm && w->pcm_of[lba - w->lba] >= 0 && psx_xa_check(raw)) {
        __atomic_add_fetch(&w->pins, 1, __ATOMIC_ACQ_REL);
        *pcm = &w->pcm[(int)w->pcm_of[lba - w->lba]];
      }
    }
  }
  if (got) {
    s_hits++;
  } else {
    if (psx_cd_read_raw(lba, raw) != 0) /* the same bytes, read here */
      return 0;
    s_misses++;
  }
  /* keep the next WIN sectors on their way */
  for (next = lba + 1; next - lba < WIN && covering(next, &end) && end > next;)
    next = end;
  if (next - lba < WIN)
    want(next, lba);
  return psx_xa_check(raw);
}

void vita_xa_release(const PsxXaPcm *pcm) {
  int i;
  for (i = 0; i < NWIN; i++) {
    Window *w = &s_win[i];
    if (pcm >= w->pcm && pcm < w->pcm + MAX_PCM) {
      __atomic_sub_fetch(&w->pins, 1, __ATOMIC_ACQ_REL);
      return;
    }
  }
}

SceUID vita_xa_ahead_thread(void) { return s_on ? s_thread : 0; }

void vita_xa_ahead_report(int vblank) {
  static uint32_t w0, kb0, us0;
  static uint32_t ds0, dus0;
  uint32_t w = s_rd_windows, kb = s_rd_kb, us = s_rd_us, ds = s_dec_sectors, dus = s_dec_us;
  if (!s_on || (!s_hits && !s_misses && w == w0))
    return;
  vita_log("xa %d: %u music sectors from the read-ahead, %u read on the game thread; "
           "read-ahead %u windows, %u KB in %u.%02u ms (core 1); %u asks found no window",
           vblank, s_hits, s_misses, (unsigned)(w - w0), (unsigned)(kb - kb0),
           (unsigned)((us - us0) / 1000), (unsigned)((us - us0) % 1000 / 10), s_noslot);
  if (g_psx_spu_native)
    vita_log("xa %d: native mix: read-ahead decoded %u sectors to PCM in %u.%02u ms (core 1); "
             "%u windows passed over as pinned",
             vblank, (unsigned)(ds - ds0), (unsigned)((dus - dus0) / 1000),
             (unsigned)((dus - dus0) % 1000 / 10), s_pinned);
  ds0 = ds;
  dus0 = dus;
  s_hits = s_misses = s_noslot = s_pinned = 0;
  w0 = w;
  kb0 = kb;
  us0 = us;
}
