/* The SPU mix and XA decode on their own core (decisions D37). Once per
 * vblank the game thread runs psx_spu_step() on the live SPU state (the same
 * voice positions, block flags and key-status changes a mix makes, without
 * the samples) and posts a packet: the voices and mix registers as they were
 * before that step, and the XA events since the last packet (stream resets,
 * and the raw sectors libcd passed, read on the game thread). The mixer
 * thread replays the packets in order with its own copies of the voices,
 * which keep the ADPCM decoder state, and hands each vblank's samples to
 * psx_audio_out (SDL2 queue, audio.wav). The XA FIFO (xa.c) is only touched
 * there.
 *
 * Nothing the game reads comes from the mixer thread, and the mixer reads
 * only its packets and SPU RAM; SPU RAM writes (SpuWrite, SpuInit) wait for
 * every posted packet first. So the output is the synchronous
 * psx_spu_vblank()'s, byte for byte, whatever the threads' timing. Calls
 * reach us through -Wl,--wrap (CMakeLists.txt), so src/platform/common and
 * the linux32 host don't change. */
#include "vita.h"
#include "watchdog.h"
#include "psx_cd.h"
#include "psx_hooks.h"
#include "psx_spu.h"
#include "psx_xa.h"

#include <psp2/kernel/cpu.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>

#define RING 8      /* packets in flight; the game thread waits when all are */
#define MAX_EVENTS 8
#define MAX_SECTORS 4 /* a vblank passes at most 3 (double speed) */
#define PRIORITY 0x70 /* a short job with a deadline: ahead of the game thread (logged) if they share a core */

enum { EV_SECTOR, EV_RESET };

typedef struct {
  PsxSpuVoice voice[PSX_SPU_VOICES];
  PsxSpuMixRegs reg;
  int mix; /* 0: events only (the event list was full) */
  int nev, nsect;
  unsigned char ev[MAX_EVENTS];
  unsigned char raw[MAX_SECTORS][PSX_CD_RAW_SECTOR];
} Packet;

static Packet s_ring[RING];
static unsigned s_head; /* game thread: packets posted */
static unsigned s_tail; /* mixer thread: packets done */
static SceUID s_free, s_ready; /* semaphores: free slots, posted packets */
static int s_running;
static volatile int s_phase; /* mixer thread: 0 idle, 1 XA, 2 voices, 3 mix, 4 out (debug) */

/* Mixer-thread state. */
static PsxSpuVoice s_voice[PSX_SPU_VOICES];
static short s_out[PSX_SPU_VBLANK_SAMPLES * 2];

/* Stats for vita_mixer_report (read across threads; only approximate). */
static volatile uint64_t s_mix_us, s_mix_worst;
static volatile unsigned s_mixes, s_mismatch;
static uint64_t s_wait_us; /* game thread waiting for a free slot or a drain */
static unsigned s_waits;

static int s_open; /* game thread: cur() holds a begun packet */

static Packet *cur(void) { return &s_ring[s_head % RING]; }

/* Game thread: a slot for the next packet. */
static void game_wait(SceUID sema, int n) {
  SceUInt64 t0 = sceKernelGetProcessTimeWide();
  SceUInt tmo = 2000000;
  int r;
  while ((r = sceKernelWaitSema(sema, n, &tmo)) < 0) {
    vita_log("mixer: wait for %d slots: %08x; posted %u, done %u, open %d, mixer phase %d", n,
             (unsigned)r, s_head, s_tail, s_open, s_phase);
    tmo = 2000000;
  }
  s_wait_us += sceKernelGetProcessTimeWide() - t0;
  s_waits++;
}

static void begin(void) {
  if (sceKernelPollSema(s_free, 1) < 0)
    game_wait(s_free, 1);
  Packet *p = cur();
  p->nev = p->nsect = 0;
  p->mix = 0;
}

static Packet *open_packet(void) {
  if (!s_open) {
    begin();
    s_open = 1;
  }
  return cur();
}

static void post(void) {
  s_open = 0;
  s_head++;
  sceKernelSignalSema(s_ready, 1);
}

static void add_event(int type, const unsigned char *raw) {
  Packet *p = open_packet();
  if (p->nev == MAX_EVENTS || (raw && p->nsect == MAX_SECTORS)) {
    post(); /* no mix: the rest goes in the next packet, in order */
    p = open_packet();
  }
  if (raw)
    memcpy(p->raw[p->nsect++], raw, PSX_CD_RAW_SECTOR);
  p->ev[p->nev++] = (unsigned char)type;
}

/* ---- mixer thread ---- */

/* --wrap redirects psx_xa_reset in this file too: the mixer calls the real one. */
void __real_psx_xa_reset(void);

static void adopt_voices(const PsxSpuVoice *snap) {
  int i;
  for (i = 0; i < PSX_SPU_VOICES; i++) {
    PsxSpuVoice *m = &s_voice[i];
    const PsxSpuVoice *g = &snap[i];
    if (g->keyon_seq != m->keyon_seq) { /* keyed on since: key on reset the decoder */
      *m = *g;
      continue;
    }
    /* No key on: the step left the game's position where our last mix left
     * ours. Registers, loop address and envelope (key off) are the game's. */
    if (g->cur_addr != m->cur_addr || g->counter != m->counter)
      s_mismatch++;
    {
      short hist0 = m->hist[0], hist1 = m->hist[1], prev = m->prev;
      int dec_valid = m->dec_valid;
      short dec[28];
      memcpy(dec, m->dec, sizeof dec);
      *m = *g;
      m->hist[0] = hist0;
      m->hist[1] = hist1;
      m->prev = prev;
      m->dec_valid = dec_valid;
      memcpy(m->dec, dec, sizeof dec);
    }
  }
}

static void run_packet(Packet *p) {
  int i, s = 0;
  s_phase = 1;
  for (i = 0; i < p->nev; i++) {
    if (p->ev[i] == EV_RESET)
      __real_psx_xa_reset();
    else
      psx_xa_decode(p->raw[s++]);
  }
  if (p->mix) {
    s_phase = 2;
    adopt_voices(p->voice);
    s_phase = 3;
    psx_spu_mix(s_voice, &p->reg, s_out);
    s_phase = 4;
    psx_audio_out(s_out, PSX_SPU_VBLANK_SAMPLES);
  }
  s_phase = 0;
}

static int mixer_main(SceSize args, void *argp) {
  (void)args;
  (void)argp;
  for (;;) {
    SceUInt64 t0, dt;
    sceKernelWaitSema(s_ready, 1, NULL);
    t0 = sceKernelGetProcessTimeWide();
    run_packet(&s_ring[s_tail % RING]);
    dt = sceKernelGetProcessTimeWide() - t0;
    s_mix_us += dt;
    if (dt > s_mix_worst)
      s_mix_worst = dt;
    s_mixes++;
    s_tail++;
    sceKernelSignalSema(s_free, 1);
  }
  return 0;
}

/* ---- game thread ---- */

void vita_mixer_init(void) {
  SceUID th;
  if (!g_vita_opts.mixer) {
    vita_log("mixer: on the game thread (mixer=0)");
    return;
  }
  s_free = sceKernelCreateSema("spyro_mix_free", 0, RING, RING, NULL);
  s_ready = sceKernelCreateSema("spyro_mix_ready", 0, 0, RING, NULL);
  th = sceKernelCreateThread("spyro_mixer", mixer_main, PRIORITY, 0x10000, 0,
                             SCE_KERNEL_CPU_MASK_USER_1, NULL);
  if (s_free < 0 || s_ready < 0 || th < 0 || sceKernelStartThread(th, 0, NULL) < 0) {
    vita_log("mixer: no thread (%08x %08x %08x); mixing on the game thread",
             (unsigned)s_free, (unsigned)s_ready, (unsigned)th);
    return;
  }
  s_running = 1;
  vita_watchdog_thread(WD_T_MIXER, th);
  vita_log("mixer: thread on core 1%s, priority 0x%x; game thread priority 0x%x, affinity %08x",
           g_vita_opts.mixer == 2 ? " (lockstep)" : "", PRIORITY,
           (unsigned)sceKernelGetThreadCurrentPriority(),
           (unsigned)sceKernelGetThreadCpuAffinityMask(sceKernelGetThreadId()));
}

/* The hang line (watchdog.c, D73). */
int vita_mixer_where(unsigned *posted, unsigned *done) {
  *posted = s_head;
  *done = s_tail;
  return s_phase;
}

/* Every posted packet done. */
void vita_mixer_drain(void) {
  if (!s_running)
    return;
  if (s_open) /* events with no vblank after them yet: run them now */
    post();
  if (s_tail != s_head) {
    prof_enter(PROF_SPU);
    game_wait(s_free, RING);
    sceKernelSignalSema(s_free, RING);
    prof_leave();
  }
}

/* Save states (D51), after vita_mixer_drain(). Save: the decoder state lives
 * in the mixer's voices, so it goes into g_psx_spu's (which the game thread
 * never decodes with), except for voices keyed on since the last packet
 * (key on reset theirs). Load: the mixer goes on from g_psx_spu's voices. */
void vita_mixer_state(int load) {
  int i;
  if (!s_running)
    return;
  for (i = 0; i < PSX_SPU_VOICES; i++) {
    PsxSpuVoice *m = &s_voice[i], *g = &g_psx_spu.voice[i];
    if (load) {
      *m = *g;
    } else if (g->keyon_seq == m->keyon_seq) {
      memcpy(g->hist, m->hist, sizeof g->hist);
      memcpy(g->dec, m->dec, sizeof g->dec);
      g->prev = m->prev;
      g->dec_valid = m->dec_valid;
    }
  }
}

/* Once per vblank, after psx_cd_vblank (instead of psx_spu_vblank). */
void vita_mixer_vblank(void) {
  Packet *p;
  if (!s_running) {
    psx_spu_vblank();
    return;
  }
  p = open_packet();
  memcpy(p->voice, g_psx_spu.voice, sizeof p->voice);
  psx_spu_mix_regs(&p->reg);
  p->mix = 1;
  post();
  psx_spu_step();
  if (g_vita_opts.mixer == 2)
    vita_mixer_drain();
}

void vita_mixer_report(int vblank) {
  unsigned n = s_mixes;
  uint64_t us = s_mix_us;
  if (!n)
    return;
  vita_log("mixer %d: %u vblanks mixed, %u.%02u ms each, worst %u.%02u ms; game thread "
           "waited %u.%02u ms in %u waits; %u position mismatches; audio queue dry %u, cut %u",
           vblank, n, (unsigned)(us / n / 1000), (unsigned)(us / n % 1000 / 10),
           (unsigned)(s_mix_worst / 1000), (unsigned)(s_mix_worst % 1000 / 10),
           (unsigned)(s_wait_us / 1000), (unsigned)(s_wait_us % 1000 / 10), s_waits,
           s_mismatch, g_vita_audio_dry, g_vita_audio_cleared);
  g_vita_audio_dry = g_vita_audio_cleared = 0;
  s_mix_us = 0;
  s_mix_worst = 0;
  s_mixes = 0;
  s_wait_us = 0;
  s_waits = 0;
}

/* ---- calls from src/platform/common, through -Wl,--wrap ---- */

void __real_SpuInit(void);
unsigned int __real_SpuWrite(unsigned char *addr, unsigned int size);

void __wrap_psx_xa_reset(void) {
  if (s_running)
    add_event(EV_RESET, NULL);
  else
    __real_psx_xa_reset();
}

void __wrap_psx_xa_sector(int lba) {
  unsigned char raw[PSX_CD_RAW_SECTOR];
  if (!vita_xa_accept(lba, raw)) /* the read-ahead's sectors, or read here (D64) */
    return;
  if (s_running)
    add_event(EV_SECTOR, raw);
  else
    psx_xa_decode(raw);
}

/* The mixer reads SPU RAM: let it finish what is posted first. */
void __wrap_SpuInit(void) {
  vita_mixer_drain();
  memset(s_voice, 0, sizeof s_voice); /* as SpuInit leaves g_psx_spu's (the mixer is idle) */
  __real_SpuInit();
}

unsigned int __wrap_SpuWrite(unsigned char *addr, unsigned int size) {
  vita_mixer_drain();
  return __real_SpuWrite(addr, size);
}
