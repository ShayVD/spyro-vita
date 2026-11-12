/* Optional presentation features over the original 30 Hz logic
 * (docs/presentation.md): 16:9 widescreen and 60 fps interpolated frames.
 * Both are off by default, and then PortPresentDraw() is GamestateDraw():
 * nothing here runs.
 *
 * The logic pass is the original GamestateDraw, side effects and all. The
 * frames it doesn't draw itself (the in-between frame, and in the attract
 * demos the 16:9 picture while the logic draws the original 4:3 frame
 * hidden) are the presenter's (stage 7e-7g, decisions D125, D126, D128,
 * D129; src/game/pres/): the game's draw code built a second time over a
 * snapshot of what it reads, blended for the in-between frame, so it never
 * touches the game's memory. On the Vita it draws on a core of its own
 * beside the logic draw. Until stage 7g (D129) D30's pass drew those frames
 * by running GamestateDraw itself over the game's own memory and putting a
 * snapshot back. */
#include "tick60.h"
#include "present.h"
#include "lod.h"
#include "savestate.h"

#include "psx_arena.h"
#include "psx_clock.h"
#include "psx_mem.h"
#include "psx_region.h" /* g_Region: PAL's screen copy (is_screen_copy) */
#include "psx_walk.h"
#include "psx_libgpu.h"
#include "psx_gpu.h" /* the GPU state (gpu_walk.c) */
#if SPYRO_PRESENTER
#include "pres/pres_draw.h" /* stage 7e-7g (D125, D126, D128): the presenter */
#include "native_draw.h"    /* stage 7f (D126): g_nd_ranges_walk */
#include "psx_vcache.h"
#endif

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "buffers.h"
#include "common.h"
#include "camera.h"
#include "cutscene.h" /* g_CreditsStage (stage 7g) */
#include "environment.h"
#include "gamepad.h"
#include "graphics.h"
#include "moby.h"
#include "renderers.h"
#include "spyro.h"

#include <gtereg.h>
#include <libetc.h>
#include <libgpu.h>

extern void GamestateDraw(void);
extern unsigned int g_mips_v1; /* mips2c_rt.c */

PresentConfig g_present;
PresentStats g_present_stats;
int g_present_narrow; /* projection narrowed to 17/21 (camera matrix, HUD) */

/* Widescreen narrows x about the screen centre by 17/21 (D67). The picture
 * shown is the 512x224 the game draws (draw areas 8 lines into the 240-line
 * display, initialization.c), filling the 960x544 screen: 4:3 at 240 lines
 * is 0.625 x per y, at 960x544 from 512x224 x gets 1.875/2.4286 of y, so x
 * narrows by 0.625 / 0.772 = 17/21 (3/4 was 16:9 over all 240 lines). */
#define WS_NUM 17
#define WS_DEN 21
#define SCREEN_CX 256

int port_present_active(void) { return g_present.widescreen || g_present.interp; }

static long long clock_cpu_us(void) {
  return (long long)clock() * 1000000 / CLOCKS_PER_SEC;
}

long long (*g_present_clock_us)(void) = clock_cpu_us;

static long long now_us(void) { return g_present_clock_us(); }

/* ---------- projection (camera.c, draw.c and r_moby's HUD path) ---------- */

/* camera.c CameraUpdateMatrices and draw.c's level-transition matrix: the
 * game already scales the Y row by 320/512 for the 4:3 TV. Narrowing the X
 * row the same way squeezes the picture into 512 columns for a 16:9 display
 * and widens every frustum test done in this space (r_moby's moby culling,
 * for one) by the same factor. */
void PortProjectionHook(short pM[3][3]) {
  int j;
  if (!g_present_narrow)
    return;
  for (j = 0; j < 3; j++)
    pM[0][j] = (short)(pM[0][j] * WS_NUM / WS_DEN);
}

/* r_moby.s HUD models (the 3D letters, counters and icons) project with
 * OFX = their screen x and a fixed diag(1, 0.625, 1) matrix
 * (func_80022A2C at 0x80022D2C and 0x80022D78; tools/mips2c/hints.yaml). */
unsigned int port_ws_hud_ofx(unsigned int ofx) {
  long long d = (long long)(int)ofx - (SCREEN_CX << 16);
  return (unsigned int)((SCREEN_CX << 16) + d * WS_NUM / WS_DEN);
}

unsigned int port_ws_r1112(unsigned int r11r12) {
  short r11 = (short)(r11r12 & 0xFFFF), r12 = (short)(r11r12 >> 16);
  return (unsigned short)(r11 * WS_NUM / WS_DEN) |
         (unsigned int)(unsigned short)(r12 * WS_NUM / WS_DEN) << 16;
}

unsigned int port_ws_hud_r11(unsigned int r11r12) {
  short r11 = (short)(r11r12 & 0xFFFF);
  return (r11r12 & 0xFFFF0000u) | (unsigned short)(r11 * WS_NUM / WS_DEN);
}

/* ---------- 2D primitives (widescreen) ---------- */

/* C-built 2D primitives go into the HUD OT through func_800168DC
 * (order_table.c). For DrawOTag their x is narrowed about the centre so
 * they keep their shape on a 16:9 display. Full-width ones (fades,
 * letterbox bars, separators) and the grayscale screen copy behind the
 * pause menus stay as they are.
 * The narrowing is the walk's (D122, from stage 7g): at DrawOTag each
 * noted primitive that narrows is copied, narrowed in the copy, and handed
 * to the game thread's walk as a substitution (psx_gpu.h PsxGpuSubst), so
 * the walk parses the narrowed words in its place and the game's own
 * primitives are never edited. Before D122 they were narrowed in place
 * and put back after DrawOTag. */
#define MAX_2D 512
#define MAX_SUBST_WORDS 8192 /* a primitive is 13 words at most, with its tag */
static void *s_2d[MAX_2D];
static int s_n2d;
static int s_record_2d;
/* the substitutions the walk has: the primitives' addresses (ascending)
 * and their narrowed copies. Written at DrawOTag after the walk's join, so
 * a walk on another thread (D71) still reads the last list's. */
static uintptr_t s_sub_at[MAX_2D];
static const u_int *s_sub_words[MAX_2D];
static u_int s_sub_pool[MAX_SUBST_WORDS];
static PsxGpuSubst s_sub = {0, s_sub_at, s_sub_words};

void port_present_note_2d(void *prim) {
  if (s_record_2d && s_n2d < MAX_2D)
    s_2d[s_n2d++] = prim;
}

static int narrow_x(int x) {
  int d = x - SCREEN_CX;
  /* round to nearest, symmetric about the centre */
  return SCREEN_CX + (d >= 0 ? (d * WS_NUM + WS_DEN / 2) / WS_DEN
                             : -((-d * WS_NUM + WS_DEN / 2) / WS_DEN));
}

static void set_x(uint32_t *w, int x) {
  *w = (*w & 0xFFFF0000u) | (uint16_t)x;
}

#define XY_X(w) ((int)(int16_t)((w) & 0xFFFF))

/* One GP0 drawing command at p (words after the tag). Collects the words
 * holding vertex xy. Returns the number of xy words, 0 if not a drawing
 * command. */
static int prim_xy_words(uint32_t *p, int len, uint32_t **xy, int *rect_w) {
  uint32_t code = p[0] >> 24;
  int n = 0, i, k;
  *rect_w = -1;
  if ((code & 0xE0) == 0x20) { /* polygon */
    int verts = (code & 0x08) ? 4 : 3;
    int tex = (code & 0x04) != 0, gour = (code & 0x10) != 0;
    k = 1;
    for (i = 0; i < verts; i++) {
      if (gour && i > 0)
        k++;
      if (k >= len)
        return 0;
      xy[n++] = &p[k++];
      if (tex)
        k++;
    }
    return n;
  }
  if ((code & 0xE0) == 0x40) { /* line or polyline */
    int gour = (code & 0x10) != 0, poly = (code & 0x08) != 0;
    k = 1;
    for (i = 0; k < len; i++) {
      if (gour && i > 0) {
        if (poly && (p[k] & 0xF000F000u) == 0x50005000u)
          break;
        k++;
      }
      if (k >= len || (poly && i >= 2 && (p[k] & 0xF000F000u) == 0x50005000u))
        break;
      if (n < 8)
        xy[n++] = &p[k];
      k++;
      if (!poly && i == 1)
        break;
    }
    return n;
  }
  if ((code & 0xE0) == 0x60) { /* rectangle (TILE/SPRT) */
    int size = (code >> 3) & 3, tex = (code & 0x04) != 0;
    static const int fixed[4] = {0, 1, 8, 16};
    if (len < 2)
      return 0;
    xy[n++] = &p[1];
    if (size == 0 && len > 2 + tex)
      *rect_w = (int)(p[2 + tex] & 0xFFFF);
    else
      *rect_w = fixed[size];
    return n;
  }
  return 0;
}

/* The pause, inventory and flight-results backgrounds: POLY_FT4 strips of a
 * grayscale copy of the last frame, CLUT at VRAM (512,224) (draw.c), PAL's
 * at (512,256) under its 256-line picture (draw.c's RV(224, 256, 224), the
 * flight results' getClut(512, 256)). The copy is already a widescreen
 * frame, so it is shown full width. Before D150 only US's CLUT was known, so
 * PAL's strips were narrowed to 4:3 like the HUD: bars at the pause screen's
 * sides in 16:9. */
static int is_screen_copy(const uint32_t *p) {
  const uint32_t code = p[0] >> 24;
  const uint32_t y = g_Region == PSX_REGION_PAL ? 256u : 224u;
  return (code & 0xFC) == 0x2C && (p[2] >> 16) == ((y << 6) | (512u >> 4));
}

/* The walk's substitutions emptied (before a DrawOTag, after the join) */
static void subst_clear(void) {
  s_sub.n = 0;
  psx_gpu_game_walk()->subst = NULL;
}

/* The noted primitives (n of them) that narrow, each copied into the pool
 * and narrowed there, as a walk's substitutions (*nsub of them, by address
 * in at, their words in words): the words DrawOTag walked before D122 when
 * they were narrowed in place. Returns how many narrowed. The game
 * thread's (squeeze_2d) and the presenter's own (stage 7f, D126). */
static int squeeze(void *const *notes, int nnotes, uintptr_t *at, const u_int **words,
                   u_int *pool, int *nsub) {
  int i, j, used = 0, squeezed = 0;
  *nsub = 0;
  for (i = 0; i < nnotes; i++) {
    const uint32_t *const prim = (const uint32_t *)notes[i];
    const int len = (int)(prim[0] >> 24);
    uint32_t *tag, *xy[8];
    int n, rect_w, minx = 1 << 30, maxx = -(1 << 30);
    if (len == 0)
      continue;
    if (used + 1 + len > MAX_SUBST_WORDS)
      break; /* never reached: 512 primitives of 13 words */
    tag = pool + used;
    memcpy(tag, prim, (size_t)(1 + len) * 4);
    n = prim_xy_words(tag + 1, len, xy, &rect_w);
    if (n == 0 || is_screen_copy(tag + 1))
      continue;
    for (j = 0; j < n; j++) {
      int x = XY_X(*xy[j]);
      minx = x < minx ? x : minx;
      maxx = x > maxx ? x : maxx;
    }
    if (rect_w > 0)
      maxx += rect_w;
    if (maxx - minx >= 511)
      continue; /* full width */
    if (rect_w >= 0) {
      /* TILE/SPRT can't change width: move its centre */
      int cx = XY_X(*xy[0]) + rect_w / 2;
      set_x(xy[0], narrow_x(cx) - rect_w / 2);
    } else {
      for (j = 0; j < n; j++)
        set_x(xy[j], narrow_x(XY_X(*xy[j])));
    }
    /* in address order (insertion: they are noted nearly in order) */
    for (j = *nsub; j > 0 && at[j - 1] > (uintptr_t)prim; j--) {
      at[j] = at[j - 1];
      words[j] = words[j - 1];
    }
    at[j] = (uintptr_t)prim;
    words[j] = tag;
    (*nsub)++;
    used += 1 + len;
    squeezed++;
  }
  return squeezed;
}

static void squeeze_2d(void) {
  subst_clear();
  g_present_stats.squeezed_2d += squeeze(s_2d, s_n2d, s_sub_at, s_sub_words, s_sub_pool, &s_sub.n);
  if (s_sub.n)
    psx_gpu_game_walk()->subst = &s_sub;
}

/* ---------- presenter: captured frames, shown one per vblank ---------- */

/* A ring of PRESENT_SLOTS frames: up to QUEUE waiting and the one on
 * screen. Without a
 * platform capture (g_present_capture NULL) a slot holds the pixels
 * (StoreImage of the drawn area); with one (the Vita's GPU renderer) the
 * platform keeps the picture in its own slot and we only number them.
 *
 * Each frame carries the vblank it's due at (stage 7c, D112). Taken one per
 * vblank, the queue shows its head at the next vblank, so the frame filed
 * at vblank count c behind k waiting ones is due at c + 1 + k; when the
 * queue is full its oldest goes and the rest move up a vblank. That is what
 * the platform's render thread needs to show a frame at its own display
 * vblank, without the game thread presenting it. */
#define QUEUE 4
static uint16_t s_frames[PRESENT_SLOTS][PRESENT_W * PRESENT_H_MAX];
static uint8_t s_frame_wide[PRESENT_SLOTS];
static uint16_t s_frame_h[PRESENT_SLOTS]; /* its lines (D148) */
static int s_frame_due[PRESENT_SLOTS];
static int s_frame_begun; /* its begin() has gone to the platform */
static int s_qhead, s_qcount; /* oldest queued slot, number queued */
static int s_shown = -1;      /* slot on screen */
static int s_in_game_draw;    /* inside GamestateDraw (a pass or the real one) */
static int s_foreign_vblank = -1; /* last DrawOTag from anywhere else */
static int s_popped;          /* the last port_present_frame_slot() took a new one */
static int s_last_push_vblank = -1000;

const PresentCapture *g_present_capture;

static uint16_t s_capture[PRESENT_W * PRESENT_H_MAX];
static int s_captured, s_capture_on, s_capture_h = PRESENT_H;

/* A frame's lines (D148): a 256-line display (PAL's, D134) is drawn whole;
 * the US's 240-line one has 224 drawn 8 lines down, which the platforms
 * crop (shown_top). */
static int capture_h(const DISPENV *d) { return d->disp.h == PRESENT_H_MAX ? PRESENT_H_MAX : PRESENT_H; }

/* The capture of the area just drawn, into the platform's slot or (without
 * a platform capture) s_capture by StoreImage. With the OT walked later or
 * on another thread (D71) it waits for the walk (psx_walk_after): the
 * area's y is the walk's draw environment, and the pixels are the walk's. */
static void capture_area(void *arg) {
  RECT16 r;
  (void)arg;
  r.x = 0;
  r.y = activeDrawEnv.ofs[1];
  r.w = PRESENT_W;
  r.h = (short)(s_capture_h = capture_h(&activeDispEnv));
  if (g_present_capture) {
    g_present_capture->capture(r.x, r.y, r.w, r.h);
    return;
  }
  StoreImage(&r, (u_long *)s_capture);
}

/* The capture into a presentation slot, after the capture itself (D71) */
static void commit_slot(void *arg) {
  const PresentFrame *f = arg;
  if (g_present_capture)
    g_present_capture->commit(f);
  else {
    memcpy(s_frames[f->slot], s_capture, sizeof s_capture);
    s_frame_h[f->slot] = (uint16_t)s_capture_h;
  }
}

/* The vblank the next frame filed would be due at (7c): its place in the
 * queue after the ones waiting, the oldest dropped if it's full. */
static int due_next(void) {
  return psx_clock_due_at(psx_clock_count() + 1 + (s_qcount < QUEUE ? s_qcount : QUEUE - 1));
}

/* By time (D148): the display vblank the next frame filed is drawn for
 * (a due the display's), or -1: due_next()'s. */
static int s_due_for = -1;

/* D148: 60 fps by time, with the game's clock at 50 Hz (PAL at its own
 * timing) and the frames due at the display's 60 Hz vblanks
 * (psx_clock_due_display). Times are in TU, 1/300 s: a game vblank is 6,
 * a display vblank 5 (60 / gcd(hz, 60) and hz / gcd(hz, 60)). Tick N,
 * drawn at our vblank c, is shown at c + 4: hi = 6 (c + 4), one of our
 * vblanks after the in-between frame's real frame (c + 3: filed after the
 * logic draw's VSync, at c + 2, due a vblank after), so that at most one
 * of its display vblanks comes before the draw's wait is over: that
 * frame is drawn before the draw, on the presenter's core beside it, and
 * the others after it, each with a display vblank or more to be drawn in.
 * The display vblanks u since the last tick's time (5u in (lo, hi]: 2.4 a
 * tick, so 2 or 3) each get a frame of their own, the two ticks blended
 * with the previous one's weight (hi - 5u) / span. At 5u == hi it is tick
 * N unblended. */
typedef struct {
  int on;   /* a tick by time: its time is the next tick's start */
  int n;    /* the presenter's frames to draw (0: the logic draw's frame, due at u0) */
  int u0;   /* the first display vblank the tick covers */
  int hi;   /* the tick's time, TU */
  int span; /* hi less the last tick's time, TU (0: no blend) */
} TimePlan;
static int s_time_valid, s_time_hi; /* the last tick's time (by time), TU */

/* A capturing pass's first DrawOTag: the platform hears the frame's draws
 * start (7c's render thread may skip a late one's). */
static void frame_begin(void) {
  if (s_frame_begun)
    return;
  s_frame_begun = 1;
  if (g_present_capture && g_present_capture->begin)
    g_present_capture->begin(s_due_for >= 0 ? s_due_for : due_next(), 0); /* the logic pass's: a real frame */
}

static void capture_now(void) {
  psx_walk_after(capture_area, NULL, 0);
  s_captured = 1;
}

/* A frame's place in the queue: its slot and due vblank (and the frame
 * dropped for it). push_capture's; stage 7f's presenter has its frame's
 * reserved before it is drawn (D126). */
static void reserve(int wide, int interp, PresentFrame *f) {
  int i;
  f->dropped = -1;
  f->fixed = s_due_for >= 0;
  if (s_qcount == QUEUE) { /* drop the oldest; the rest are due a vblank sooner */
    f->dropped = s_qhead;
    s_qhead = (s_qhead + 1) % PRESENT_SLOTS;
    s_qcount--;
    for (i = 0; i < s_qcount && !f->fixed; i++) /* by time: each keeps its own (D148) */
      s_frame_due[(s_qhead + i) % PRESENT_SLOTS]--;
    g_present_stats.queue_drops++;
  }
  /* This may be the slot on screen (after a run of drops); harmless: the
   * queue isn't empty now, so the next vblank shows a newer slot. */
  f->slot = (s_qhead + s_qcount) % PRESENT_SLOTS;
  f->due = f->fixed ? s_due_for : due_next();
  f->wide = wide;
  f->interp = interp;
  s_frame_wide[f->slot] = (uint8_t)wide;
  s_frame_due[f->slot] = f->due;
  s_qcount++;
  s_last_push_vblank = psx_clock_count();
}

static void push_capture(int wide, int interp) {
  PresentFrame f;
  s_frame_begun = 0;
  if (!s_captured)
    return;
  s_captured = 0;
  reserve(wide, interp, &f);
  psx_walk_after(commit_slot, &f, sizeof f);
}

/* A save state was loaded (D51): the queued frames are from before it, and
 * the vblank counts the fallback compares were the old timeline's. */
static void budget_reset(void);

void port_present_reset(void) {
#if SPYRO_PRESENTER
  port_present_pres_join();
#endif
  budget_reset();
  s_qcount = 0;
  s_shown = -1;
  s_captured = 0;
  s_frame_begun = 0;
  s_foreign_vblank = -1;
  s_last_push_vblank = -1000;
  s_time_valid = 0;
}

/* The frame shown now, after a pop: a slot, or -1 for the PS1 display. The
 * game drew outside GamestateDraw since our last frame (loading screens,
 * code that draws from GamestateUpdate): that picture is the PS1
 * display's. A slow tick alone keeps the last frame on screen: falling back
 * then made the picture jump between 16:9 and 4:3. */
static int shown_slot(void) {
  if (s_shown < 0 || s_foreign_vblank > s_last_push_vblank) {
    g_present_stats.display_fallbacks++;
    return -1;
  }
  return s_shown;
}

static void pop(void) {
  s_shown = s_qhead;
  s_qhead = (s_qhead + 1) % PRESENT_SLOTS;
  s_qcount--;
}

int port_present_frame_slot(void) {
  if (!port_present_active()) {
    s_qcount = 0; /* switched off (a menu): drop what was queued */
    s_shown = -1;
    return -1;
  }
  s_popped = s_qcount > 0;
  if (s_qcount > 0)
    pop();
  return shown_slot();
}

int port_present_frame_due(int v) {
  if (!port_present_active()) {
    s_qcount = 0;
    s_shown = -1;
    return -1;
  }
  s_popped = 0;
  while (s_qcount > 0 && s_frame_due[s_qhead] - v <= 0) {
    if (s_popped)
      g_present_stats.due_superseded++;
    pop();
    s_popped = 1;
  }
  if (s_popped && s_frame_due[s_shown] != v)
    g_present_stats.due_late++;
  g_present_stats.due_units++;
  if (!s_popped && s_shown >= 0)
    g_present_stats.due_held++;
  return shown_slot();
}

int port_present_slot_due(int slot) {
  return slot >= 0 && slot < PRESENT_SLOTS ? s_frame_due[slot] : 0;
}

int port_present_slot_h(int slot) {
  return slot >= 0 && slot < PRESENT_SLOTS && s_frame_h[slot] ? s_frame_h[slot] : PRESENT_H;
}

int port_present_shown_h(void) { return port_present_slot_h(s_shown); }

const uint16_t *port_present_slot_pixels(int slot) {
  return slot < 0 || slot >= PRESENT_SLOTS || g_present_capture ? NULL : s_frames[slot];
}

const uint16_t *port_present_shown_pixels(void) {
  if (s_shown < 0 || g_present_capture || s_foreign_vblank > s_last_push_vblank)
    return NULL;
  return s_frames[s_shown];
}

const uint16_t *port_present_frame(void) {
  int slot = port_present_frame_slot();
  return slot < 0 || g_present_capture ? NULL : s_frames[slot];
}

int port_present_frame_new(void) { return s_popped; }

int port_present_frame_is_wide(void) {
  return s_shown >= 0 && s_frame_wide[s_shown];
}

/* ---------- DrawOTag's pre and post (libgpu.c) ---------- */

static int s_suppress_ot; /* logic pass under faithful widescreen */
#if SPYRO_PRESENTER
/* --pres-check (stage 7e, D125; since D129 against the logic draw): 2
 * while the logic draw runs (its list recorded and walked; the
 * presenter's is recorded by its own DrawOTag, pres_DrawOTag) */
static int s_pres_record;
#endif

static int ot_pre(u_long *p) {
  if (!s_in_game_draw)
    s_foreign_vblank = psx_clock_count();
  if (s_sub.n)
    subst_clear(); /* the last list's (DrawOTag joined its walk first) */
#if SPYRO_PRESENTER
  if (s_pres_record == 2)
    pres_check_frame(0, p, &activeDrawEnv, &activeDispEnv, NULL, 0);
#endif
  if (s_suppress_ot) {
    s_n2d = 0;
    return 1;
  }
  if (s_capture_on)
    frame_begin();
  if (s_record_2d)
    squeeze_2d();
  return 0;
}

/* The substitutions stay for the walk, which may still run (D71): the next
 * DrawOTag empties them after its join. */
static void ot_post(u_long *p) {
  (void)p;
  s_n2d = 0;
  if (s_capture_on)
    capture_now();
}

/* VSync (libetc.c), timing the waits (the host mixes audio and writes
 * screenshots there; the Vita mixes, presents and waits for the display) so
 * the game's own CPU time can leave them out: --present-stats and
 * interp_budget_us. */
static long long s_tick_vsync_us;

static int timed_vsync(int mode) {
  if (mode == 0) {
    long long t = now_us(), d;
    psx_platform_vsync();
    d = now_us() - t;
    g_present_stats.us_vsync += d;
    s_tick_vsync_us += d;
  }
  return psx_clock_count();
}

/* ---------- the primitive space (D54) ---------- */

/* GamestateDraw points each draw at its DB's primitive buffer (AllocateBuffers:
 * lower, upper, then the HUD OT and the world OT), 0x1C000 bytes or 0x13000
 * in reduced-space levels, with the HUD's mobys and the sky and particle
 * renderers' limit (D_80075780 - 0x400, r_cyclorama.s, r_particles.s) at
 * +0x1C000. The world and moby renderers check no bound. The original 4:3
 * view fits: the ending of level 64 (reduced space) reaches 73 KB of 76. The
 * wider frustum of a 16:9 draw went past the upper buffer into the OTs.
 * DrawOTag takes the primitives at once here, so with any presentation
 * option or draw distance on, every draw starts at the lower buffer and spans
 * both, as the pause screens' draws already start there (draw.c
 * func_8001A40C). The HUD's mobys and that limit go to the top, 0x600 under
 * the HUD OT. Those 0x600 bytes are the save fairy's card buffer
 * (func_800314B4 reads the save's header there and builds the save there);
 * the original's fairy draw keeps them free by moving the mobys down to
 * +0x1BA00 (draw.c func_8001D718), which it does only when the top is the
 * HUD OT itself, so the port keeps them free in every draw (D61). With
 * everything off nothing changes. */
static int poly_space_on(void) { return port_present_active() || g_lod.scale > LOD_ONE; }

void *port_poly_start(void *db_buf) {
  return poly_space_on() ? g_Buffers.m_LowerPolyBuffer : db_buf;
}

#define FAIRY_CARD_BUF 0x600 /* g_Buffers.m_HudOTStart - 0x600, func_800314B4 */

void PortPolySpace(void) {
  void *top = (char *)g_Buffers.m_HudOTStart - FAIRY_CARD_BUF;
  if (!poly_space_on())
    return;
  D_800757B0 = g_Buffers.m_LowerPolyBuffer;
  D_80075780 = top;
  D_800756FC = top;
  g_HudMobys = top;
}

/* The OT walked from a copy (walk.c, D71): the PS1 RAM a DrawOTag list can
 * reach. The game links its OT buckets from primitive to primitive and ends
 * the chain at the terminator (order_table.c), so that's the primitives
 * written since the draw began, from the lower buffer (where a spanning draw
 * starts, and below the upper one) up to the draw's pointer D_800757B0, and
 * the terminator. Before the buffers exist only the terminator. */
#define OT_TERMINATOR_AT PSX_OT_TERMINATOR_OFS /* order_table.c */
#define OT_TERMINATOR_LEN 0x14       /* its tag and 4 NOP words (port.c) */

static int in_arena(const void *p, uint32_t len);

int port_present_prim_ranges(PsxWalkRange *r, int max) {
  uint32_t lo, hi;
  int n = 0;
  if (max < 2)
    return 0;
  r[n].lo = OT_TERMINATOR_AT;
  r[n].hi = OT_TERMINATOR_AT + OT_TERMINATOR_LEN;
  n++;
  if (in_arena(g_Buffers.m_LowerPolyBuffer, 1) && in_arena(D_800757B0, 1)) {
    lo = (uint32_t)((uintptr_t)g_Buffers.m_LowerPolyBuffer - g_psx_arena_base);
    hi = (uint32_t)((uintptr_t)D_800757B0 - g_psx_arena_base);
    if (hi > lo) {
      r[n].lo = lo;
      r[n].hi = hi;
      n++;
    }
  }
  return n;
}

/* Every GamestateDraw: shown = its picture is shown (lod.c, D49). */
static void game_draw(int shown) {
  port_lod_draw_begin(shown);
  GamestateDraw();
  port_lod_draw_end(shown);
}

/* DrawOTag's calls do nothing until the first presentation tick (they were
 * PsyCross hooks installed there, patch 0005). */
static int s_ot_hooks;

static void hooks_init(void) { s_ot_hooks = 1; }

int port_present_ot_pre(u_long *p) { return s_ot_hooks ? ot_pre(p) : 0; }

void port_present_ot_post(u_long *p) {
  if (s_ot_hooks)
    ot_post(p);
}

/* ---------- the game's memory ---------- */

#define PARTICLE_SPACE 0x2000 /* g_Buffers.m_ParticleSpaceStart (AllocateBuffers, 4BEF8.c) */
#define SECTOR_CACHE 0x18     /* bytes 0x18-0x1B of a sector header (func_800258F0's) */
#define MAX_SECTORS 4096

static int count_mobys(void);

static int in_arena(const void *p, uint32_t len) {
  uintptr_t a = (uintptr_t)p;
  return a >= g_psx_arena_base && len <= PSX_MEM_SIZE &&
         a - g_psx_arena_base <= PSX_MEM_SIZE - len;
}

static unsigned char *sector_cache(int i) {
  unsigned char *sec =
      (unsigned char *)(uintptr_t)((uint32_t *)g_Environment.m_SectorPointer)[i];
  return in_arena(sec + SECTOR_CACHE, 4) ? sec + SECTOR_CACHE : NULL;
}

/* ---------- interpolation: transforms of the previous tick ---------- */

#define MAX_MOBYS 1024
#define MAX_PARTICLES (PARTICLE_SPACE / 32) /* loaders.c: 256 slots, 0xFF ends */
/* An animation track: animation, next animation, frame, next frame and
 * progress toward the next frame (a new frame every `len` units). */
typedef struct {
  unsigned char anim, next, frame, nframe, prog;
} Track;

typedef struct {
  short cls;
  signed char state;
  Vector3D pos;
  Vector3D8 rot;
  Track track;
} MobyXf;

static struct {
  int valid, gamestate, level;
  Vector3D cam_pos;
  Vector3D16 cam_rot;
  Vector3D spyro_pos;
  Vector3D8 spyro_rot[3]; /* body, head, tail */
  Track spyro_track[3];
  int nmobys;
  MobyXf mobys[MAX_MOBYS];
  int have_particles;
  Particle particles[MAX_PARTICLES];
} s_prev;

/* Moby animation (AnimationState, func_800522C0): 64 progress units per
 * frame. Spyro's body, head and tail (pete.c func_8003CB24 and kin): 16. */
#define MOBY_FRAME 64
#define SPYRO_FRAME 16

static Track moby_track(const Moby *m) {
  Track t;
  t.anim = m->m_AnimationState.m_Animation;
  t.next = m->m_AnimationState.m_NextAnimation;
  t.frame = m->m_AnimationState.m_Frame;
  t.nframe = m->m_AnimationState.m_NextFrame;
  t.prog = m->m_AnimationState.m_FrameProgress;
  return t;
}

static void set_moby_track(Moby *m, const Track *t) {
  m->m_AnimationState.m_Animation = t->anim;
  m->m_AnimationState.m_NextAnimation = t->next;
  m->m_AnimationState.m_Frame = t->frame;
  m->m_AnimationState.m_NextFrame = t->nframe;
  m->m_AnimationState.m_FrameProgress = t->prog;
}

/* Spyro's tracks k = 0 body, 1 head, 2 tail (r_pete reads these bytes);
 * sp: the game's Spyro, or the presenter's copy (stage 7e, D125). */
static Track spyro_track(const Spyro *sp, int k) {
  const unsigned char *anims = &sp->m_bodyAnimation;
  const unsigned char *frames = &sp->m_bodyAnimationFrame;
  const unsigned char *progs = &sp->m_bodyFrameProgress;
  Track t;
  t.anim = anims[2 * k];
  t.next = anims[2 * k + 1];
  t.frame = frames[2 * k];
  t.nframe = frames[2 * k + 1];
  t.prog = progs[k];
  return t;
}

static void set_spyro_track(Spyro *sp, int k, const Track *t) {
  unsigned char *anims = &sp->m_bodyAnimation;
  unsigned char *frames = &sp->m_bodyAnimationFrame;
  unsigned char *progs = &sp->m_bodyFrameProgress;
  anims[2 * k] = t->anim;
  anims[2 * k + 1] = t->next;
  frames[2 * k] = t->frame;
  frames[2 * k + 1] = t->nframe;
  progs[k] = t->prog;
}

static Vector3D8 *spyro_rot(Spyro *sp, int k) {
  return k == 0 ? &sp->m_bodyRotation : k == 1 ? &sp->m_headRotation : &sp->m_tailRotation;
}

/* The blend's weight of the previous tick, s_wn / s_wd: 1/2 for the
 * in-between frame, or (D148, by time) the frame's place between the two
 * ticks' times. Every blend below is written so that at 1/2 it is the
 * halfway arithmetic it always was, rounding included. */
static int s_wn = 1, s_wd = 2;
/* from prev toward cur by 1 - s_wn / s_wd; d = cur - prev >= 0 */
static int toward(int prev, int d) { return prev + d * (s_wd - s_wn) / s_wd; }

/* Along a track, by the blend's weight (halfway for the in-between frame).
 * A tick advances progress and crosses at most one frame (then frame = the
 * old next frame, anim = the old next anim), so the point is either between
 * the same two frames or on one side of the crossing. Anything else (a new
 * animation started, a loop restarted) keeps this tick's pose. Returns 1 if
 * the track was blended. */
static int mid_track(Track *cur, const Track *prev, int len) {
  int mid;
  if (cur->anim == prev->anim && cur->next == prev->next &&
      cur->frame == prev->frame && cur->nframe == prev->nframe) {
    if (cur->prog < prev->prog)
      return 0;
    cur->prog = (unsigned char)toward(prev->prog, cur->prog - prev->prog);
    return 1;
  }
  if (cur->anim == prev->next && cur->frame == prev->nframe &&
      prev->prog < len && cur->prog < len) {
    mid = toward(prev->prog, cur->prog + len - prev->prog);
    if (mid >= len) {
      cur->prog = (unsigned char)(mid - len);
    } else {
      *cur = *prev;
      cur->prog = (unsigned char)mid;
    }
    return 1;
  }
  return 0;
}

static int count_mobys_at(const Moby *mobys) {
  int n = 0;
  if (!mobys)
    return 0;
  while (n < MAX_MOBYS && (signed char)mobys[n].m_State != -1)
    n++;
  return n;
}

static int count_mobys(void) { return count_mobys_at(g_LevelMobys); }

static long long s_t_update;

void PortPresentBeforeUpdate(void) {
  long long t;
  int i;
#if SPYRO_PRESENTER
  port_present_pres_join(); /* stage 7f (D126): before the update changes what it reads */
#endif
  savestate_point(); /* the main loop top: save states happen here (D51) */
  if (g_present.stats || g_present.interp_budget_us)
    s_t_update = now_us();
  if (!g_present.interp) {
    s_prev.valid = 0; /* switched on later: no blending with an old tick */
    return;
  }
  t = now_us();
  s_prev.valid = 1;
  s_prev.gamestate = g_Gamestate;
  s_prev.level = g_LevelId;
  s_prev.cam_pos = g_Camera.m_Position;
  s_prev.cam_rot = g_Camera.m_Rotation;
  s_prev.spyro_pos = g_Spyro.m_Position;
  for (i = 0; i < 3; i++) {
    s_prev.spyro_rot[i] = *spyro_rot(&g_Spyro, i);
    s_prev.spyro_track[i] = spyro_track(&g_Spyro, i);
  }
  s_prev.nmobys = g_present.interp_mobys ? count_mobys() : 0;
  for (i = 0; i < s_prev.nmobys; i++) {
    Moby *m = &g_LevelMobys[i];
    s_prev.mobys[i].cls = m->m_Class;
    s_prev.mobys[i].state = (signed char)m->m_State;
    s_prev.mobys[i].pos = m->m_Position;
    s_prev.mobys[i].rot = m->m_Rotation;
    s_prev.mobys[i].track = moby_track(m);
  }
  s_prev.have_particles = g_present.interp_anim &&
                          in_arena(g_Particles, sizeof s_prev.particles);
  if (s_prev.have_particles)
    memcpy(s_prev.particles, g_Particles, sizeof s_prev.particles);
  g_present_stats.us_blend += now_us() - t;
}

/* Between the previous tick and this one by the blend's weight (halfway
 * for the in-between frame), measured from this one (so values stay in the
 * game's own range). Jumps (teleports, respawns, camera cuts) are not
 * blended. */
#define JUMP 0x2000
static int back(int d) { return d * s_wn / s_wd; } /* d / 2 at 1/2 */
static int mid_i(int prev, int cur, int *ok) {
  int d = cur - prev;
  if (d > JUMP || d < -JUMP)
    *ok = 0;
  return cur - back(d);
}

static int mid_vec(Vector3D *out, const Vector3D *prev, const Vector3D *cur) {
  int ok = 1;
  Vector3D r;
  r.x = mid_i(prev->x, cur->x, &ok);
  r.y = mid_i(prev->y, cur->y, &ok);
  r.z = mid_i(prev->z, cur->z, &ok);
  if (ok)
    *out = r;
  return ok;
}

static short mid_angle12(short prev, short cur) {
  int d = ((cur - prev + 0x800) & 0xFFF) - 0x800;
  return (short)(cur - back(d));
}

static unsigned char mid_angle8(unsigned char prev, unsigned char cur) {
  int d = (signed char)(unsigned char)(cur - prev);
  return (unsigned char)(cur - back(d));
}

static void mid_rot8(Vector3D8 *cur, const Vector3D8 *prev) {
  cur->x = mid_angle8(prev->x, cur->x);
  cur->y = mid_angle8(prev->y, cur->y);
  cur->z = mid_angle8(prev->z, cur->z);
}

/* Particles keep their slot for life (particle_alloc.s); a slot whose class
 * and render type match and whose timer moved on a little is the same
 * particle one tick later. A reused slot starts its timer again. */
#define PARTICLE_JUMP 0x800
static void blend_particles(Particle *parts) {
  int i, k;
  for (i = 0; i < MAX_PARTICLES && parts[i].m_RenderType != 0xFF; i++) {
    Particle *c = &parts[i];
    const Particle *p = &s_prev.particles[i];
    short *cp = &c->m_Data.m_Position.x;
    const short *pp = &p->m_Data.m_Position.x;
    int d[3];
    if (c->m_Class != p->m_Class || c->m_RenderType != p->m_RenderType ||
        c->m_Timer < p->m_Timer || c->m_Timer - p->m_Timer > 16)
      continue;
    for (k = 0; k < 3; k++) {
      d[k] = cp[k] - pp[k];
      if (d[k] > PARTICLE_JUMP || d[k] < -PARTICLE_JUMP)
        break;
    }
    if (k < 3)
      continue;
    for (k = 0; k < 3; k++)
      cp[k] = (short)(cp[k] - back(d[k]));
    g_present_stats.particles_blended++;
  }
}

static void count_track(int blended) {
  if (blended)
    g_present_stats.anim_blended++;
  else
    g_present_stats.anim_kept++;
}

static int can_interp(void) {
  return s_prev.valid && s_prev.gamestate == g_Gamestate &&
         s_prev.level == g_LevelId;
}

/* Blend the camera, Spyro, the mobys and the particles given: the
 * presenter's copies (stage 7e, D125; pres_snap.c; until D129 also the
 * game's own in place for D30's pass, which put a snapshot back). Camera rotation feeds
 * CameraUpdateMatrices at the start of GamestateDraw. Returns 0 if the
 * camera jumped: no in-between frame then. */
static int blend_on(Camera *camera, Spyro *sp, Moby *mobys, Particle *parts) {
  int i, n;
  Vector3D cam = camera->m_Position;
  if (!mid_vec(&cam, &s_prev.cam_pos, &camera->m_Position))
    return 0;
  camera->m_Position = cam;
  camera->m_Rotation.x = mid_angle12(s_prev.cam_rot.x, camera->m_Rotation.x);
  camera->m_Rotation.y = mid_angle12(s_prev.cam_rot.y, camera->m_Rotation.y);
  camera->m_Rotation.z = mid_angle12(s_prev.cam_rot.z, camera->m_Rotation.z);
  if (!g_present.interp_mobys)
    return 1;
  if (mid_vec(&sp->m_Position, &s_prev.spyro_pos, &sp->m_Position)) {
    for (i = 0; i < 3; i++) {
      Track t = spyro_track(sp, i);
      mid_rot8(spyro_rot(sp, i), &s_prev.spyro_rot[i]);
      if (g_present.interp_anim) {
        count_track(mid_track(&t, &s_prev.spyro_track[i], SPYRO_FRAME));
        set_spyro_track(sp, i, &t);
      }
    }
  }
  n = count_mobys_at(mobys);
  if (n > s_prev.nmobys)
    n = s_prev.nmobys;
  for (i = 0; i < n; i++) {
    Moby *m = &mobys[i];
    const MobyXf *p = &s_prev.mobys[i];
    if (m->m_Class != p->cls || (signed char)m->m_State < 0 || p->state < 0)
      continue;
    if (mid_vec(&m->m_Position, &p->pos, &m->m_Position)) {
      Track t = moby_track(m);
      mid_rot8(&m->m_Rotation, &p->rot);
      if (g_present.interp_anim) {
        count_track(mid_track(&t, &p->track, MOBY_FRAME));
        set_moby_track(m, &t);
      }
    }
  }
  if (s_prev.have_particles && parts)
    blend_particles(parts);
  return 1;
}

#if SPYRO_PRESENTER
/* The presenter's copies (pres_snap.c), blended as the game's would be */
int port_present_blend_to(void *camera, void *spyro, void *mobys, void *particles) {
  return blend_on(camera, spyro, mobys, particles);
}
#endif

/* ---------- passes ---------- */

/* interp_budget_us bookkeeping (PortPresentDraw); s_logic_cost: the last
 * logic pass's CPU time */
static long long s_busy_wo_interp, s_interp_cost, s_logic_cost;

/* interp_budget_us: the game thread's safety net (D75). 60 fps never falls
 * to 30 for the GPU or the render thread: where the GPU can't keep up the
 * platform lowers the resolution instead (the Vita's dynres). What's left is
 * D28's case: the game thread itself running late, which would slow the
 * game. Its CPU time per tick plus the in-between frame's, averaged over a
 * few ticks, over interp_budget_us (or over it on two ticks in a row: a
 * jump the average hasn't caught up with) is a miss. One tick over alone is
 * noise and changes nothing.
 * A miss holds 30 fps until the cost has fitted with an eighth to spare for
 * a whole hold (1 s); a miss within 10 s of coming back doubles the hold,
 * up to 8 s (D68), rather than flip between 60 and 30 tick by tick (D44).
 * While held, the in-between frame's cost is taken as this tick's logic pass
 * (it draws the same scene: 0.85-1.0 of it on hardware), not the last one
 * measured, which may be from a heavier spot and would never let go. A tick
 * over the miss line starts the hold again, one between it and the eighth
 * to spare only pauses it. */
#define HOLD_MIN 30  /* ticks: 1 s at 30 fps */
#define HOLD_MAX 240 /* 8 s */
#define STEADY 300   /* ticks at 60 fps after which a hold is HOLD_MIN again */
static int s_hold, s_hold_len = HOLD_MIN, s_since_back = STEADY, s_prev_interp;
static int s_cpu_over; /* the last tick's CPU time was over the budget */
static long long s_cpu_avg; /* CPU time per tick with the in-between frame, over ~4 ticks */

static void budget_reset(void) {
  s_hold = 0;
  s_hold_len = HOLD_MIN;
  s_since_back = STEADY;
  s_prev_interp = 0;
  s_cpu_over = 0;
  s_cpu_avg = 0;
}

/* 1 if this tick may have its in-between frame */
static int budget_allows(void) {
  long long b = g_present.interp_budget_us;
  long long cpu = s_busy_wo_interp + (s_prev_interp ? s_interp_cost : s_logic_cost);
  int over;
  s_cpu_avg = s_cpu_avg ? (s_cpu_avg * 3 + cpu) / 4 : cpu;
  over = s_cpu_avg > b || (cpu > b && s_cpu_over);
  s_cpu_over = cpu > b;
  if (!s_hold) {
    if (!over) {
      if (s_since_back < STEADY && ++s_since_back == STEADY)
        s_hold_len = HOLD_MIN;
      return 1;
    }
    if (s_since_back < STEADY)
      s_hold_len = s_hold_len * 2 > HOLD_MAX ? HOLD_MAX : s_hold_len * 2;
    s_hold = s_hold_len;
    s_since_back = 0;
    g_present_stats.budget_holds++;
    return 0;
  }
  if (over)
    s_hold = s_hold_len; /* not yet: the whole hold again */
  else if (s_cpu_avg * 8 > b * 7)
    ; /* fits, but without the eighth to spare: wait */
  else if (--s_hold == 0)
    return 1;
  return 0;
}

/* While the logic draws 4:3 (attract demos, ws_exact), the presenter's 16:9
 * frames keep their own copy of the renderer's visibility state: D_800771C8, one byte
 * per sector that r_environment's last draw found visible, which
 * func_800521C0 reads at the start of the next draw to drop mobys in hidden
 * sectors; and the sector caches (SECTOR_CACHE). With the 4:3 copy, mobys
 * in sectors only the 16:9 view shows were dropped: they popped in at the
 * edges. */
#define SECTOR_FLAGS 0x800771C8u /* psx.map D_800771C8, 0x100 bytes */
/* (wide_state_out takes the game's after the presenter's frame, for the
 * sectors it didn't draw; pres_draw_visibility then its own) */
static uint8_t s_wide_flags[0x100];
static uint32_t s_wide_cache[MAX_SECTORS];
static int s_wide_valid, s_wide_level, s_wide_nsectors;

static void wide_state_out(void) {
  int i;
  memcpy(s_wide_flags, PSX_ARENA(SECTOR_FLAGS), sizeof s_wide_flags);
  s_wide_nsectors = 0;
  if (in_arena(g_Environment.m_SectorPointer, 4) && g_Environment.m_SectorCount > 0 &&
      g_Environment.m_SectorCount <= MAX_SECTORS) {
    s_wide_nsectors = g_Environment.m_SectorCount;
    for (i = 0; i < s_wide_nsectors; i++) {
      unsigned char *c = sector_cache(i);
      if (c)
        memcpy(&s_wide_cache[i], c, 4);
    }
  }
  s_wide_level = g_LevelId;
  s_wide_valid = 1;
}

const PresentPresOut *g_present_pres;
void (*g_present_pres_watch)(int after);

#if SPYRO_PRESENTER
/* ---------- the presenter (stage 7e, D125; on its own core, 7f, D126) ----------
 *
 * The in-between frame is the presenter's, in every gamestate (play since
 * 7e, the rest since 7g, D128), and so is the attract demos' 16:9 real
 * frame: the game's draw code built a second time over a snapshot
 * (pres_draw.c). Its
 * output calls are its own, here: PutDrawEnv, PutDispEnv, DrawSync and
 * DrawOTag on libgpu's environments' copies and on a walk of its own (the
 * second PsxGpuWalk, gpu_walk.c), and its 2D primitives noted for the 16:9
 * narrowing into its own substitutions. So it shares nothing with the logic
 * draw but read-only data, and can run beside it:
 *  - the game thread takes the snapshot after update N (pres_draw_prepare)
 *    and starts the job;
 *  - with a platform worker (g_present_pres, the Vita) the job runs there,
 *    beside the logic draw N, recording into a buffer of its own; the game
 *    thread placed the buffer in the command ring first, so the render
 *    thread draws it before the logic's frame, as 7e drew it before;
 *  - else (the host, or the Vita with the vertex cache on) it runs inline,
 *    on the game thread, through the GR_* calls, as 7e did;
 *  - the game thread joins it before update N+1 (port_present_pres_join):
 *    the update changes the level's data the presenter reads (the
 *    animated bytes), and the wait keeps its frame exact without a copy.
 * libgpu's currentDispEnv: the presenter's PutDispEnv left it set to its
 * environment for the logic draw's (7e, D125). The game thread puts the
 * same value itself when it starts the job (the DB the draw goes to, the
 * one GamestateDraw takes), and the join counts any frame whose
 * environment wasn't that (disp_misses, 0 expected).
 * The one write the two draws could race on: the moby renderers' face
 * replacements, which the game's draw makes in the model's list and puts
 * back (D123). While the presenter runs beside it, the game's draw makes
 * them in a copy too (g_faces_copy), so neither writes a model's faces. */

static DRAWENV s_pw_env;
static DISPENV s_pw_active, s_pw_cur; /* activeDispEnv and currentDispEnv, the presenter's */
static PsxGpuWalk s_pw = {.env = &s_pw_env, .disp = &s_pw_active, .own = 1};

/* its 16:9 narrowing (squeeze_2d's, its own) */
static void *s_p2d[MAX_2D];
static int s_pn2d;
static uintptr_t s_psub_at[MAX_2D];
static const u_int *s_psub_words[MAX_2D];
static u_int s_psub_pool[MAX_SUBST_WORDS];
static PsxGpuSubst s_psub = {0, s_psub_at, s_psub_words};

typedef struct {
  const PresentPresOut *out; /* on the worker, into its buffer; NULL: inline */
  int check;                 /* --pres-check: the list recorded, not drawn */
  int wide;                  /* 16:9: its HUD narrowed */
  int interp;                /* an in-between frame (blended); 0: a real one (stage 7g) */
  int gl_wide;               /* g_present.widescreen, for the GL side's display state */
  int disabled;              /* g_GPUDisabledState */
  PresentFrame f;            /* on the worker: the slot reserved for it */
  int begun, captured, squeezed;
  int async; /* --pres-check-thread: on the host's thread */
  int gs;         /* the gamestate it draws */
  DISPENV put;    /* what it left as currentDispEnv */
  DISPENV expect; /* what the game thread put there for it */
  long long us;
} PresJob;
static PresJob s_job;
static int s_job_pending; /* game thread: running on the worker, not joined */
static PresentPresStats s_pstats;
/* --pres-check-thread, --pres-check-late: what the check does once the
 * presenter's job is joined: 1 compare, 2 + why skip */
static int s_check_deferred;

/* The logic draw's side of --pres-check (since D129; until then D30's
 * pass's): compare (what 1) or skip (2 + why), now or, with the
 * presenter's list still being recorded on its thread, at the join */
static void check_after(int what) {
  if (s_job_pending && s_job.check)
    s_check_deferred = what;
  else if (what == 1)
    pres_check_compare();
  else
    pres_check_skip(what - 2);
}
extern int g_faces_copy; /* render_ctx.c, moby_hand.c */

static int alloc_walk(void) {
  if (s_pw.vb)
    return 1;
  s_pw.vb = malloc(sizeof(GrVertex) * MAX_VERTEX_BUFFER_SIZE);
  s_pw.splits = malloc(sizeof(PsxGpuSplit) * PSX_GPU_MAX_SPLITS);
  s_pw.dr = malloc(sizeof(uint32_t) * 2 * PSX_GPU_DIRECT_MAX);
  if (!s_pw.vb || !s_pw.splits || !s_pw.dr) {
    free(s_pw.vb), free(s_pw.splits), free(s_pw.dr);
    s_pw.vb = NULL, s_pw.splits = NULL, s_pw.dr = NULL;
    return 0;
  }
  psx_gpu_clear_splits_in(&s_pw);
  return 1;
}

/* order_table.c's note, in the presenter's build */
void pres_note_2d(void *prim) {
  if (s_job.wide && s_pn2d < MAX_2D)
    s_p2d[s_pn2d++] = prim;
}

/* libgpu.c's PutDispEnv and PutDrawEnv on the presenter's environments */
DISPENV *pres_PutDispEnv(DISPENV *env) {
  if (env->isinter != s_pw_cur.isinter || env->disp.y == s_pw_cur.disp.y) {
    s_pw_active = s_pw_cur = *env;
  } else {
    s_pw_active = s_pw_cur;
    s_pw_cur = *env;
  }
  return 0;
}

DRAWENV *pres_PutDrawEnv(DRAWENV *env) {
  memcpy(&s_pw_env, env, sizeof s_pw_env);
  return 0;
}

/* libgpu.c's DrawSync: the walk's leftover splits (no callback: the game
 * sets none) */
int pres_DrawSync(int mode) {
  (void)mode;
  if (!s_job.check)
    psx_gpu_draw_splits_own(&s_pw);
  return 0;
}

static void pres_capture(int y) {
  const int h = capture_h(&s_pw_active); /* (D148) */
  if (s_job.out) {
    s_job.out->capture(0, y, PRESENT_W, h);
  } else if (g_present_capture) {
    g_present_capture->capture(0, y, PRESENT_W, h);
  } else { /* capture_area's StoreImage, from the presenter's area */
    RECT16 r;
    s_capture_h = h;
    r.x = 0, r.y = (short)y, r.w = PRESENT_W, r.h = (short)h;
    StoreImage(&r, (u_long *)s_capture);
  }
  s_job.captured = 1;
}

/* libgpu.c's DrawOTag with present.c's ot_pre and ot_post, on the
 * presenter's walk: the frame's start (7c), its 2D primitives narrowed,
 * the list walked and drawn, the capture */
void pres_DrawOTag(u_long *p) {
  PresJob *const j = &s_job;
  if (j->check) { /* ot_pre's --pres-check record (D125) */
    pres_check_frame(1, p, &s_pw_env, &s_pw_active, s_pw.nd_pool, s_pw.nd_pool_cap);
    s_pn2d = 0;
    if (!j->async)
      psx_vcache_ot_done(); /* (inline: as DrawOTag's) */
    return;
  }
  if (!j->begun) {
    j->begun = 1;
    if (j->out)
      j->out->begin(j->f.due, j->interp);
    else if (g_present_capture && g_present_capture->begin)
      g_present_capture->begin(s_due_for >= 0 ? s_due_for : due_next(), j->interp);
  }
  if (j->out)
    j->out->env(&s_pw_cur, j->disabled, j->gl_wide);
  s_pw.subst = NULL;
  s_psub.n = 0;
  if (j->wide) {
    j->squeezed += squeeze(s_p2d, s_pn2d, s_psub_at, s_psub_words, s_psub_pool, &s_psub.n);
    if (s_psub.n)
      s_pw.subst = &s_psub;
  }
  s_pn2d = 0;
  if (j->disabled) { /* LIBGPU.C: no post */
    psx_gpu_clear_splits_in(&s_pw);
    return;
  }
  s_pw.nd = g_pres_walk_nd;
  psx_gpu_walk_own(&s_pw, p);
  pres_capture(s_pw_env.ofs[1]);
  if (!j->out)
    psx_vcache_ot_done(); /* inline: as DrawOTag's (the worker never has the vertex cache) */
}

/* the job: the presenter's draw, on the worker or inline */
static void job_run(void) {
  long long t = now_us();
  if (s_job.out)
    s_job.out->open();
  GamestateDraw_pres();
  if (s_job.out) {
    if (s_job.captured)
      s_job.out->commit(&s_job.f);
    s_job.out->close();
  }
  s_job.put = s_pw_cur;
  s_job.us = now_us() - t;
}

/* After the job (the game thread): its numbers */
static void job_done(void) {
  g_present_stats.squeezed_2d += s_job.squeezed;
  if (s_job.check)
    return;
  if (memcmp(&s_job.put, &s_job.expect, sizeof s_job.put)) {
    s_pstats.disp_misses++;
    s_pstats.disp_miss_gs |= 1u << (s_job.gs & 31);
  }
  if (!s_job.captured) {
    s_pstats.uncaptured++;
    s_pstats.uncaptured_gs |= 1u << (s_job.gs & 31);
  }
}

void port_present_pres_join(void) {
  long long t;
  if (!s_job_pending)
    return;
  s_job_pending = 0;
  t = now_us();
  g_present_pres->wait();
  g_faces_copy = 0;
  if (s_check_deferred) { /* --pres-check-thread: the logic draw's frame waited for the presenter's */
    if (s_check_deferred == 1)
      pres_check_compare();
    else
      pres_check_skip(s_check_deferred - 2);
    s_check_deferred = 0;
  }
  t = now_us() - t;
  s_pstats.join_wait_us += t;
  s_pstats.joins_waited += t > 50; /* a wait, not a handover */
  s_pstats.worker_us += s_job.us;
  job_done();
}

void port_present_pres_stats(PresentPresStats *out) {
  *out = s_pstats;
  memset(&s_pstats, 0, sizeof s_pstats);
}
#else
void port_present_pres_join(void) {}
void port_present_pres_stats(PresentPresStats *out) { memset(out, 0, sizeof *out); }
#endif

#if SPYRO_PRESENTER

/* Stage 7g (D128): on the worker the frame's slot is reserved before the
 * draw runs, and libgpu's environment put last is set to the DB's, so the
 * draw must file a frame and put that environment. Every gamestate's draw
 * does (each ends with PutDispEnv(&g_CurDB->m_DispEnv) and a DrawOTag)
 * but the credits' first stage, which only copies the last picture into
 * the other buffer (credits_hand.c Draw: MoveImage, no DrawOTag): no
 * frame is filed there (nor was by D30's pass), so the presenter draws it
 * inline, where its frame isn't filed either. The host's runs count both
 * (PresentPresStats' disp_miss_gs, uncaptured_gs). */
static int files_a_frame(void) { return !(g_Gamestate == GS_Credits && g_CreditsStage == -1); }

/* A frame from the presenter: the in-between frame (blend 1), or the
 * attract demos' 16:9 real frame (blend 0, own_state), in every gamestate
 * (stage 7g, D128; until D129 D30's pass drew what the presenter didn't).
 * own_state: it starts from the 16:9 copies of the visibility state and
 * leaves its own there (wide_state_out, pres_draw_visibility); then it
 * runs inline, as the 16:9 real frame after it reads them. check:
 * --pres-check, its list recorded for the compare and not drawn (since
 * D129 the snapshot unblended, against the logic draw). Returns 0 if no frame
 * (the camera jumped); else 1 inline (its frame captured, for the caller
 * to queue) or 2 on the worker (its frame queued already). */
static int presenter_pass(int wide, int own_state, int blend, int check) {
  long long t = now_us();
  const int own = own_state && s_wide_valid && s_wide_level == g_LevelId &&
                  s_wide_nsectors == g_Environment.m_SectorCount;
  const PresentPresOut *const w = g_present_pres;
  const int worker = !check && !own_state && w && w->usable() && files_a_frame();
  /* --pres-check-thread: the check's presenter on a thread beside the
   * logic draw (its list only recorded, never drawn); --pres-check-late:
   * at the join, after it */
  const int async_check = check && !own_state && g_pres_check_thread && w && w->post;
  port_present_pres_join(); /* (it was, at the loop top) */
  if (!alloc_walk() ||
      !pres_draw_prepare(blend, wide, own ? s_wide_flags : NULL, own ? s_wide_cache : NULL,
                         own ? s_wide_nsectors : 0))
    return 0;
  memset(&s_job, 0, sizeof s_job);
  s_job.check = check;
  s_job.wide = wide;
  s_job.interp = blend;
  s_job.gs = g_Gamestate;
  s_job.gl_wide = g_present.widescreen;
  s_job.disabled = g_GPUDisabledState;
  /* libgpu's environments as the presenter's PutDrawEnv and PutDispEnv
   * find them (pres_draw_prepare joined the game's walk) */
  s_pw_env = activeDrawEnv;
  s_pw_active = activeDispEnv;
  s_pw_cur = currentDispEnv;
  psx_gpu_clear_splits_in(&s_pw);
  s_pw.subst = NULL;
  /* its draws: into the worker's buffer, or (inline) the GR_* calls */
  s_pw.out = worker ? w->gpu : NULL;
  pres_draw_pool(&s_pw.nd_pool, &s_pw.nd_pool_cap);
  s_pn2d = 0;
  if (!check) { /* the presenter's PutDispEnv's, for the logic draw's (job_done checks it) */
    s_job.expect = (g_CurDB == &g_DB[0] ? &g_DB[1] : &g_DB[0])->m_DispEnv;
    currentDispEnv = s_job.expect;
  }
  if (async_check) {
    s_job.async = 1;
    g_faces_copy = !g_faces_race;
    w->post(job_run);
    s_job_pending = 1;
    return 2;
  }
  if (worker) {
    s_job.out = w;
    s_frame_begun = 0;
    reserve(wide, blend, &s_job.f); /* filed now: shown before this tick's frame */
    w->submit(&s_job.f);
    g_faces_copy = 1; /* the logic draw's faces into a copy while it runs beside (moby_hand.c) */
    w->post(job_run);
    s_job_pending = 1;
    s_pstats.worker_frames++;
    g_present_stats.passes++;
    g_present_stats.us_pass += now_us() - t; /* the game thread's part */
    return 2;
  }
  {
    PsxGpuWalk *const ranges = g_nd_ranges_walk;
    g_nd_ranges_walk = &s_pw; /* the Vita's vertex buffers: the presenter's direct ranges */
    if (g_present_pres_watch)
      g_present_pres_watch(0);
    job_run();
    if (g_present_pres_watch)
      g_present_pres_watch(1);
    g_nd_ranges_walk = ranges;
  }
  job_done();
  if (!check) /* libgpu's environment put last, as the draw left it (7e) */
    currentDispEnv = s_pw_cur;
  if (own_state && !check) {
    wide_state_out(); /* the level's sector count and the bytes past the presenter's */
    pres_draw_visibility(s_wide_flags, s_wide_cache, s_wide_nsectors);
  }
  if (!check) {
    s_captured = s_job.captured;
    s_pstats.inline_frames++;
    g_present_stats.passes++;
    g_present_stats.us_pass += now_us() - t;
  }
  return 1;
}
#endif

#if SPYRO_PRESENTER
static int gcd(int a, int b) { return b ? gcd(b, a % b) : a; }
static int tu_game(void) { return 60 / gcd(psx_clock_hz(), 60); }
static int tu_display(void) { return psx_clock_hz() / gcd(psx_clock_hz(), 60); }

/* This tick's display vblanks (D148): blended if it may be (the in-between
 * frame's conditions, the last tick's time known and at most
 * PSX_CLOCK_MAX_CATCH_UP vblanks back, a gamestate that files frames).
 * Display vblanks before the next of ours (lo) are too soon to draw for:
 * after a slow tick the frame on screen is held there. */
static TimePlan time_plan(int interp) {
  TimePlan p = {0};
  const int tg = tu_game(), td = tu_display(), c = psx_clock_count();
  int lo = tg * (c + 2);
  p.on = 1;
  p.hi = tg * (c + 4);
  if (interp && s_time_valid && p.hi - s_time_hi > 0 &&
      p.hi - s_time_hi <= tg * PSX_CLOCK_MAX_CATCH_UP && files_a_frame()) {
    p.span = p.hi - s_time_hi;
    if (s_time_hi > lo)
      lo = s_time_hi;
  }
  p.u0 = lo / td + 1;
  if (p.span)
    p.n = p.hi / td - lo / td;
  return p;
}

/* Frame i of a tick by time: the presenter's, blended for its display
 * vblank (unblended at the tick's own time), filed due at it. Returns
 * presenter_pass's 0 (no frame), 1 (inline, queued here) or 2 (worker). */
static int time_frame(const TimePlan *p, int i, int wide_pass) {
  const int u = p->u0 + i, wn = p->hi - tu_display() * u;
  int r;
  s_wn = wn;
  s_wd = p->span;
  s_due_for = u;
  r = presenter_pass(g_present.widescreen, wide_pass, wn != 0, 0);
  if (r == 1)
    push_capture(g_present.widescreen, wn != 0);
  s_wn = 1;
  s_wd = 2;
  s_due_for = -1;
  if (r) {
    g_present_stats.time_frames++;
    if (wn)
      g_present_stats.interp_frames++;
  } else {
    g_present_stats.skipped_interp++;
  }
  return r;
}

/* The tick's frames after its first (D148), after the logic draw. The
 * game's draw makes the tick's VSync(0) itself (GamestateDraw ends with
 * DrawSync, VSync, PutDispEnv and DrawOTag), so these are filed after
 * that wait: the plan's time, one of our vblanks after the original's
 * real frame, leaves each a display vblank or more to be drawn in (a
 * frame its vblank has passed already is dropped). Each waits for the one
 * before (the presenter has one snapshot). libgpu's display environment is
 * the logic draw's afterwards: the presenter sets it for the draw it
 * expects next (presenter_pass), which here has been. */
static void time_rest(const TimePlan *p, int wide_pass) {
  int i;
  for (i = 1; i < p->n; i++) {
    DISPENV keep;
    if (p->u0 + i - psx_clock_due_at(psx_clock_count()) <= 0) {
      g_present_stats.skipped_interp++; /* its display vblank has passed */
      continue;
    }
    keep = currentDispEnv;
    time_frame(p, i, wide_pass);
    currentDispEnv = keep;
  }
}

/* The tick's end: its time is where the next one's frames start */
static void time_done(const TimePlan *p) {
  s_time_valid = p->on;
  s_time_hi = p->hi;
  if (p->on && p->n)
    g_present_stats.time_ticks++;
  else if (p->on)
    g_present_stats.time_fallbacks++;
}
#endif

/* main.c, instead of GamestateDraw() (patches/spyro-1-port/0005). */
void PortPresentDraw(void) {
  int wide_pass, logic_wide, interp, timed, active, pres = 0, pres_worker = 0, hide;
  long long t, t_interp = 0;
  TimePlan tp = {0};
  if (g_present.stats) {
    g_present_stats.us_update += now_us() - s_t_update;
    g_present_stats.ticks++;
  }
  timed = g_present.stats || g_present.interp_budget_us;
  active = port_present_active();
#if SPYRO_PRESENTER
  if (!active && !g_pres_check)
#else
  if (!active)
#endif
  {
    if (g_present.stats) {
      t = now_us();
      s_tick_vsync_us = 0;
      g_psyxVSyncHook = timed_vsync;
      game_draw(1);
      g_psyxVSyncHook = NULL;
      g_present_stats.logic_passes++;
      g_present_stats.us_logic += now_us() - t - s_tick_vsync_us;
    } else {
      game_draw(1);
    }
    return;
  }
  hooks_init();
#if SPYRO_PRESENTER
  pres = pres_draw_available();
#endif
  /* Widescreen: one 16:9 draw (the logic draw itself), except in attract
   * demos (or with ws_exact), where the logic must stay the original 4:3 one
   * for the recorded input to replay: there the logic draw stays 4:3 and
   * hidden, and the picture is the presenter's 16:9 frame. Without the
   * presenter (a build or a configuration that has none: the moved arena,
   * fast_render 0) the logic draw is shown there, 4:3, and there is no
   * in-between frame (stage 7g, D129: D30's pass drew both before). */
  {
    int exact = g_present.ws_exact || (!g_present.ws_logic && g_DemoMode != DEMO_MODE_NONE);
    wide_pass = g_present.widescreen && exact && pres;
    logic_wide = g_present.widescreen && !exact;
  }

  /* A 60 Hz update (D130) is one vblank: its frame is the next one shown,
   * with nothing in between */
  interp = g_present.interp && pres && can_interp() && !g_t60.next;
#if SPYRO_PRESENTER
  /* D148: 60 fps by time. The game's clock at 50 Hz (PAL), frames due at
   * the display's vblanks: a frame for every display vblank this tick
   * covers, each blended by its own time between the two ticks. */
  if (g_present.interp && psx_clock_due_display() && pres) {
    tp = time_plan(interp);
    if (!tp.n) {
      interp = 0; /* shown by its logic draw, at the tick's first display vblank */
      s_due_for = tp.u0;
    }
  }
#endif
  if (g_present.interp && !interp)
    g_present_stats.skipped_interp++;
#if SPYRO_PRESENTER
  /* Stage 7f (D126): on its own core the presenter costs the game thread
   * only its snapshot, so interp_budget doesn't apply */
  pres_worker = interp && !wide_pass && g_present_pres && g_present_pres->usable() &&
                files_a_frame();
#endif
  /* interp_budget_us: the last tick's game CPU time without the in-between
   * frame, plus what that frame cost when it last ran, must fit. Otherwise
   * this tick shows its real frame for both vblanks (30 fps) instead of
   * running late, which would slow the game itself (D28); budget_allows
   * holds 30 fps for a while after a miss (D68). Only the game thread's
   * own time counts: the GPU is the platform's to fix (D75). */
  if (interp && g_present.interp_budget_us && !pres_worker && !budget_allows()) {
    interp = 0;
    g_present_stats.budget_skips++;
    if (tp.n) { /* by time: the logic draw's frame then (D148) */
      tp.n = 0;
      s_due_for = tp.u0;
    }
  }
  s_prev_interp = interp;
#if SPYRO_PRESENTER
  /* The in-between frame: the presenter's (stage 7e, D125; every gamestate
   * since 7g, D128); by time (D148), the tick's first frame */
  if (interp) {
    long long tp0 = now_us();
    int r;
    t_interp = tp0;
    r = tp.n ? time_frame(&tp, 0, wide_pass) : presenter_pass(g_present.widescreen, wide_pass, 1, 0);
    if (r) {
      if (r == 1 && !tp.n) /* inline: its capture; on the worker it was filed already */
        push_capture(g_present.widescreen, 1); /* shown before this tick's frame */
      if (!tp.n)
        g_present_stats.interp_frames++;
    } else if (tp.n) { /* by time: no blend (the camera jumped): the logic draw's frame */
      tp.n = 0;          /* (time_frame counted it) */
      s_due_for = tp.u0;
    } else {
      g_present_stats.skipped_interp++;
    }
    t_interp = now_us() - t_interp;
    s_interp_cost = t_interp;
  }
  /* the attract demos' 16:9 real frame: the presenter's, unblended, from
   * its own visibility state, queued after the logic pass below (by time,
   * the frames blended by time are the picture: D148) */
  if (wide_pass && !tp.n)
    presenter_pass(1, 1, 0, 0);
  /* --pres-check (stage 7e, D125; since D129): the presenter's frame from
   * the same snapshot unblended against the logic draw's own list, record
   * for record, with the logic draw's projection and visibility state */
  if (g_pres_check) {
    if (!pres)
      pres_check_skip(4);
    else if (presenter_pass(logic_wide, 0, 0, 1))
      s_pres_record = 2; /* the logic pass below records its list too */
    else
      pres_check_skip(1);
  }
#endif
  /* By time (D148) the logic draw is hidden as the attract demos' 4:3 one
   * is: the presenter's frames are the picture. */
  hide = wide_pass || tp.n;

  /* The logic pass: the game's own draw, side effects and all. */
  t = now_us();
  s_tick_vsync_us = 0;
  if (timed)
    g_psyxVSyncHook = timed_vsync;
  s_suppress_ot = hide;
  g_present_narrow = logic_wide;
  s_record_2d = logic_wide;
  s_capture_on = active && !hide;
  s_in_game_draw = 1;
  game_draw(!wide_pass);
  s_in_game_draw = 0;
  s_capture_on = 0;
#if SPYRO_PRESENTER
  if (s_pres_record == 2) {
    s_pres_record = 0;
    check_after(1);
  }
#endif
  s_record_2d = 0;
  g_present_narrow = 0;
  s_suppress_ot = 0;
  g_psyxVSyncHook = NULL;
  /* This tick's frame goes in the queue where the original draw finishes,
   * so real frames are shown on the same vblanks as without presentation. */
  if (active && !tp.n)
    push_capture(wide_pass || logic_wide, 0);
  s_due_for = -1;
  g_present_stats.logic_passes++;
  g_present_stats.us_logic += now_us() - t - (timed ? s_tick_vsync_us : 0);
  if (g_present.interp_budget_us) {
    s_logic_cost = now_us() - t - s_tick_vsync_us;
    s_busy_wo_interp = now_us() - s_t_update - s_tick_vsync_us - t_interp;
  }
#if SPYRO_PRESENTER
  if (tp.n) { /* by time (D148): the tick's other frames */
    g_present_stats.time_hidden++;
    time_rest(&tp, wide_pass);
  }
  time_done(&tp);
#endif
}
