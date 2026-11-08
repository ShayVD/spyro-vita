/* Shared pieces of the hand-written renderers (sky_hand.c, ...; decisions
 * D72). The renderers were hand-written MIPS asm; mips2c translated them
 * instruction by instruction, with every memory access through MIPS_KUSEG
 * and every GTE command through PsyCross's decoder. The hand C does the same
 * work with pointers into the arena, registers as locals and the inline GTE
 * commands (gte_inline.h), and leaves memory, the scratchpad and the GTE's
 * registers as the asm does: checked by the oracle replay and by host and
 * Vita3K runs byte-identical to fast_render=0 (docs/translation-log.md). */
#ifndef RENDER_HAND_H
#define RENDER_HAND_H

#include "mips2c_rt.h"
#include "native_draw.h" /* D82: native_draw's check in HAND_PICK */
#include "psx_mem.h"

#include <string.h>

/* A PS1 address the game keeps in RAM (a pointer field, a list entry) as a
 * host pointer. The game's own pointers are arena addresses already; this
 * maps the KUSEG mirror (and on the Vita its other aliases) as MIPS_KUSEG
 * does, once per pointer instead of once per access. */
#define HPTR(type, a) ((type *)(uintptr_t)MIPS_KUSEG(a))
/* The arena address of a host pointer, as the game stores it. */
#define PSX_ADDR(p) ((u32)(uintptr_t)(p))

/* Words, halves and bytes at an arena address the code computed itself
 * (a primitive it is writing, the scratchpad, a list it walks). */
#define AT32(a) (*(u32 *)(uintptr_t)(u32)(a))
#define AT16(a) (*(u16 *)(uintptr_t)(u32)(a))
#define AT8(a) (*(u8 *)(uintptr_t)(u32)(a))
/* The word BYTES bytes into a table (the asm's byte offsets into word
 * tables: vertex, colour and clip-word indices come pre-multiplied by 4). */
#define WORD_AT(base, bytes) (*(const u32 *)((const u8 *)(base) + (bytes)))

/* fast_render=3 (stage 7d2, decisions D124): NAME_hand_rc.c, and the
 * presenter's NAME_hand_pres.c, build the renderers as readable C by D87's
 * rule. The control registers a renderer sets are set as before (the
 * rotation and translation; what libgte and the camera set is read); the
 * data registers, FLAG and the GTE commands are not used at all, nor the
 * PS1 scratchpad, the register spill area or the hand stack. The
 * register-file builds (NAME_hand.c, _ngte, _ndr) are unchanged; with them
 * gone (the owner's call on the default), the `#if !NGTE_RC` code goes. */
#ifndef NGTE_RC
#define NGTE_RC 0
#endif
#if NGTE_RC
#undef NGTE
#define NGTE 1
#undef NGTE_MATH
#define NGTE_MATH 1
#endif

/* The scratchpad (0x1F800000 on the PS1) is at the arena base; in the
 * native build it is its own array (psx_arena.h, D106). At fast_render=3 a
 * renderer's working memory is its own (g_render_work, the presenter's
 * g_pres_scratch): nothing reads it across calls (the poison audit, D124),
 * so it is no longer the PS1's scratchpad. */
#if NGTE_RC
extern unsigned char g_render_work[1024];
#define SCRATCH_BASE ((u32)(uintptr_t)g_render_work)
#elif SPYRO_NATIVE_MEM || RENDER_PRES /* the presenter's (render_ctx_names.h, D123) */
extern unsigned char g_psx_scratch[1024];
#define SCRATCH_BASE ((u32)(uintptr_t)g_psx_scratch)
#else
#define SCRATCH_BASE ((u32)g_psx_arena_base)
#endif

/* GTE commands, as the asm's opcodes (gte_inline.h). */
#define gte_rtps() m2c_gte_0x180001()
#define gte_nclip() m2c_gte_0x1400006()

/* The register save every renderer starts with: s0-s7, gp, sp, fp, ra to
 * D_80077DD8 (loaded back before `jr $ra`). mips2c's units start each call
 * with s0-s7, gp and fp at 0, sp at g_mips_sp and ra at MIPS_RA_SENTINEL,
 * so that is what they store; the hand C stores the same, so RAM stays
 * byte-identical to fast_render=0 (the oracle ignores the area). */
#if NGTE_RC
/* fast_render=3: no spill area (nothing reads it, D124) */
static inline void hand_spill_regs(void) {}
#else
extern char D_80077DD8[];
static inline void hand_spill_regs(void) {
  u32 *spill = (u32 *)D_80077DD8;
  int i;
  for (i = 0; i < 8; i++)
    spill[i] = 0; /* s0-s7 */
  spill[8] = 0;   /* gp */
  spill[9] = g_mips_sp;
  spill[10] = 0;  /* fp */
  spill[11] = MIPS_RA_SENTINEL;
}
#endif

/* fast_render=2 (decisions D80, docs/native-plan.md stage 1): each hand
 * renderer is compiled twice. NAME_hand.c itself, with NGTE 0, is today's
 * hand C on the GTE register file (fast_render=1); NAME_hand_ngte.c
 * includes it with NGTE 1, which turns on the native GTE maths of
 * gte_native.h in the `#if NGTE` blocks. With NGTE 0 the code is the
 * fast_render=1 code as it was.
 *
 * The register file stays exact. The GTE's state between functions lives in
 * gteRegs (the mips2c units, PsyCross and the decomp C read and write it
 * there, and the oracle replay compares all of it), so the native code
 * leaves gteRegs as the register-file code would have, before any
 * register-file code runs and before the renderer returns:
 *  - a loop whose commands interleave (INTPL with RTPS, two DPCS per step)
 *    runs native for all but its last few steps and the register-file code
 *    for those: after NGTE_TAIL RTPS or three colour commands, every
 *    register a command writes, FIFOs included, holds what the whole loop
 *    leaves;
 *  - other native commands go into an NgteSync, which keeps only what can
 *    still show in the register file (the inputs of the last four RTPS, of
 *    the last three DPCS, the SXY words set and NCLIP's MAC0) and replays or
 *    stores them (ngte_flush). */
#ifndef NGTE
#define NGTE 0
#endif
/* The units written by hand after D80 (HAND_RENDERER_UNIT) have a third
 * build, NAME_hand_ndr.c: NGTE 1 for native_draw's records with NGTE_MATH 0,
 * the GTE register file as fast_render=1 has it (D95's fast_render=2). Their
 * native GTE maths (D98) are in `#if NGTE_MATH` blocks, NAME_hand_ngte.c. */
#ifndef NGTE_MATH
#define NGTE_MATH NGTE
#endif
#include "gte_native.h"
#define NGTE_TAIL 4

/* An RTPS whose SZ3 the code read (MFC2 19 zeroes the register's high
 * half, which no RTPS writes: one read since the flush is enough). */
#define NGTE_RD_SZ3 1

typedef struct {
  /* RTPS: the last four inputs (V0's words) and their reads */
  u32 vxy[4], vz[4];
  u32 nr;        /* RTPS since the last flush */
  u32 rd;        /* NGTE_RD_* of any of them */
  u32 v0xy, v0z; /* what the code last moved into V0 after them */
  u8 v0;
  /* DPCS or INTPL: the last three, their inputs as moved there (RGBC or
   * IR1-3, IR0, RFC-BFC) */
  u32 rgbc[3], ir1[3], ir2[3], ir3[3], ir0[3], rfc[3], gfc[3], bfc[3];
  u8 intpl[3];
  u32 nc;
  u32 rgbc_post; /* what the code moved into RGBC after them */
  u8 rgbc_set;
  /* SXY0-2 as last moved there, and the last NCLIP's MAC0 */
  u32 sxy0, sxy1, sxy2;
  s32 mac0;
  u8 sxy_set, nclip;
  /* each group's last command, by a running count: the flush replays the
   * groups in that order (they write the same MAC, IR and FLAG; the last
   * one wins, and no group reads what another writes) */
  u32 seq, last_r, last_c, last_s;
} NgteSync;

#if NGTE_RC
/* fast_render=3: no register file to keep, so nothing is recorded. The
 * calls below stay in the shared source for the register-file builds and
 * compile to nothing here; NCLIP's winding is still computed from the
 * screen words as set. */
static inline void ngte_sync_init(NgteSync *s) { s->sxy0 = s->sxy1 = s->sxy2 = s->v0xy = s->v0z = s->nc = 0; }
static inline void ngte_rtps_rec(NgteSync *s, u32 vxy, u32 vz, u32 rd) { (void)s, (void)vxy, (void)vz, (void)rd; }
/* the vertex the asm leaves in V0 (the one after the last), which a moby
 * sprite's projection takes (moby_hand.c DrawSprite) */
static inline void ngte_v0(NgteSync *s, u32 vxy, u32 vz) {
  s->v0xy = vxy;
  s->v0z = vz;
}
/* A colour command's far colour, a control register the asm sets: the
 * last one's goes there at the flush, where the register-file builds
 * replay them (the same points in the code). */
static inline void ngte_dpcs_rec(NgteSync *s, u32 rgbc, u32 ir0, u32 rfc, u32 gfc, u32 bfc) {
  (void)rgbc, (void)ir0;
  s->rfc[0] = rfc, s->gfc[0] = gfc, s->bfc[0] = bfc, s->nc = 1;
}
static inline void ngte_intpl_rec(NgteSync *s, u32 ir1, u32 ir2, u32 ir3, u32 ir0, u32 rfc, u32 gfc,
                                  u32 bfc) {
  (void)ir1, (void)ir2, (void)ir3, (void)ir0;
  s->rfc[0] = rfc, s->gfc[0] = gfc, s->bfc[0] = bfc, s->nc = 1;
}
static inline void ngte_rgbc(NgteSync *s, u32 rgbc) { (void)s, (void)rgbc; }
static inline void ngte_sxy(NgteSync *s, u32 xy0, u32 xy1, u32 xy2) {
  s->sxy0 = xy0;
  s->sxy1 = xy1;
  s->sxy2 = xy2;
}
static inline void ngte_sxy0(NgteSync *s, u32 xy) { s->sxy0 = xy; }
static inline s32 ngte_nclip_s(NgteSync *s) { return ngte_nclip(s->sxy0, s->sxy1, s->sxy2); }
static inline void ngte_flush(NgteSync *s) {
  if (s->nc) {
    CTC2(s->rfc[0], 21);
    CTC2(s->gfc[0], 22);
    CTC2(s->bfc[0], 23);
    s->nc = 0;
  }
}
static inline void ngte_rtps_run(NgteSync *s, u32 n) { (void)s, (void)n; }
#else
static inline void ngte_sync_init(NgteSync *s) { memset(s, 0, sizeof *s); }

static inline void ngte_rtps_rec(NgteSync *s, u32 vxy, u32 vz, u32 rd) {
  const u32 k = s->nr++ & 3;
  s->vxy[k] = vxy;
  s->vz[k] = vz;
  s->rd |= rd;
  s->v0 = 0; /* a move into V0 before this RTPS doesn't outlast it */
  s->last_r = ++s->seq;
}
static inline void ngte_v0(NgteSync *s, u32 vxy, u32 vz) {
  s->v0xy = vxy;
  s->v0z = vz;
  s->v0 = 1;
}
static inline void ngte_dpcs_rec(NgteSync *s, u32 rgbc, u32 ir0, u32 rfc, u32 gfc, u32 bfc) {
  const u32 k = s->nc++ % 3;
  s->rgbc[k] = rgbc;
  s->ir0[k] = ir0;
  s->rfc[k] = rfc;
  s->gfc[k] = gfc;
  s->bfc[k] = bfc;
  s->intpl[k] = 0;
  s->rgbc_set = 0;
  s->last_c = ++s->seq;
}
/* INTPL's inputs, as moved into IR1-3, IR0 and RFC-BFC */
static inline void ngte_intpl_rec(NgteSync *s, u32 ir1, u32 ir2, u32 ir3, u32 ir0, u32 rfc, u32 gfc,
                                  u32 bfc) {
  const u32 k = s->nc++ % 3;
  s->ir1[k] = ir1;
  s->ir2[k] = ir2;
  s->ir3[k] = ir3;
  s->ir0[k] = ir0;
  s->rfc[k] = rfc;
  s->gfc[k] = gfc;
  s->bfc[k] = bfc;
  s->intpl[k] = 1;
  s->last_c = ++s->seq;
}
/* MTC2 to RGBC after the last DPCS (a pipelined load of the next colour) */
static inline void ngte_rgbc(NgteSync *s, u32 rgbc) {
  s->rgbc_post = rgbc;
  s->rgbc_set = 1;
}
/* MTC2 to SXY0-2 (whole words) */
static inline void ngte_sxy(NgteSync *s, u32 xy0, u32 xy1, u32 xy2) {
  s->sxy0 = xy0;
  s->sxy1 = xy1;
  s->sxy2 = xy2;
  s->sxy_set = 1;
  s->last_s = ++s->seq;
}
/* MTC2 to SXY0 alone */
static inline void ngte_sxy0(NgteSync *s, u32 xy) {
  s->sxy0 = xy;
  s->sxy_set = 1;
  s->last_s = ++s->seq;
}
/* NCLIP on SXY0-2 as set: MAC0, FLAG 0 */
static inline s32 ngte_nclip_s(NgteSync *s) {
  s->mac0 = ngte_nclip(s->sxy0, s->sxy1, s->sxy2);
  s->nclip = 1;
  s->last_s = ++s->seq;
  return s->mac0;
}

static inline void ngte_flush_r(NgteSync *s) {
  u32 k = s->nr > 4 ? s->nr - 4 : 0;
  for (; k < s->nr; k++) {
    MTC2(s->vz[k & 3], 1);
    MTC2(s->vxy[k & 3], 0);
    gte_rtps();
  }
  if (s->rd & NGTE_RD_SZ3)
    (void)MFC2(19);
  s->nr = 0;
  s->rd = 0;
}
static inline void ngte_flush_c(NgteSync *s) {
  u32 k = s->nc > 3 ? s->nc - 3 : 0;
  for (; k < s->nc; k++) {
    const u32 j = k % 3;
    CTC2(s->rfc[j], 21);
    CTC2(s->gfc[j], 22);
    CTC2(s->bfc[j], 23);
    MTC2(s->ir0[j], 8);
    if (s->intpl[j]) {
      MTC2(s->ir1[j], 9);
      MTC2(s->ir2[j], 10);
      MTC2(s->ir3[j], 11);
      m2c_gte_0x980011(); /* INTPL */
    } else {
      MTC2(s->rgbc[j], 6);
      m2c_gte_0x780010(); /* DPCS */
    }
  }
  if (s->rgbc_set)
    MTC2(s->rgbc_post, 6);
  s->nc = 0;
  s->rgbc_set = 0;
}
static inline void ngte_flush_s(NgteSync *s) {
  if (s->sxy_set) {
    C2_SXY0 = s->sxy0;
    C2_SXY1 = s->sxy1;
    C2_SXY2 = s->sxy2;
  }
  if (s->nclip) {
    C2_MAC0 = s->mac0;
    C2_FLAG = 0;
  }
  s->sxy_set = s->nclip = 0;
}

/* Before a run of N native RTPS: SXY0-2, MAC0 and FLAG as set by moves
 * and NCLIPs since the last RTPS can't show after four more RTPS, which
 * write all three; with fewer, everything pending goes to the register file
 * first so the FIFO shifts what it should. */
static inline void ngte_rtps_run(NgteSync *s, u32 n);
/* The register file as the native commands would have left it. */
static inline void ngte_flush(NgteSync *s) {
  int g;
  for (g = 0; g < 3; g++) {
    /* the group whose last command is the oldest of those pending */
    u32 r = s->nr ? s->last_r : ~0u, c = s->nc ? s->last_c : ~0u;
    u32 x = (s->sxy_set || s->nclip) ? s->last_s : ~0u;
    if (r == ~0u && c == ~0u && x == ~0u)
      break;
    if (r <= c && r <= x)
      ngte_flush_r(s);
    else if (c <= x)
      ngte_flush_c(s);
    else
      ngte_flush_s(s);
  }
  if (s->v0) {
    MTC2(s->v0z, 1);
    MTC2(s->v0xy, 0);
    s->v0 = 0;
  }
}
static inline void ngte_rtps_run(NgteSync *s, u32 n) {
  if (!(s->sxy_set || s->nclip))
    return;
  if (n >= 4)
    s->sxy_set = s->nclip = 0;
  else
    ngte_flush(s);
}
#endif /* NGTE_RC */

/* Run both hand renderers from the same state and compare RAM, the
 * scratchpad and every GTE register (host --fast-render-check, hand_check.c).
 * Set at start-up, before any game code. */
extern int g_hand_check;
typedef u32 (*HandFn)(u32, u32, u32, u32);
u32 hand_check(const char *name, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2, u32 a3);
/* The same, also comparing the two results (functions returning a value). */
u32 hand_check_v0(const char *name, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2, u32 a3);
/* The same for the units written by hand after D80 (D95, D98): the mips2c
 * unit is the reference; fast_render=1's build and fast_render=2's each run
 * from the same state and are compared with it. */
u32 hand_check_unit(const char *name, HandFn unit, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2,
                    u32 a3);

/* The entry fast_render picks: 1 the hand C on the register file, 2 its
 * native-GTE build (NAME_ngte, from NAME_hand_ngte.c). */
/* host --render-ctx-check (stage 7d, D123; render_ctx_check.c): each call
 * also runs the presenter build (NAME_pres) from the same inputs in its own
 * context, and the two are compared. */
extern int g_render_ctx_check;
#ifndef RENDER_CTX_FN
#define RENDER_CTX_FN
typedef u32 (*RenderCtxFn)(u32, u32, u32, u32);
#endif
u32 render_ctx_check(const char *name, RenderCtxFn logic, RenderCtxFn pres, u32 a0, u32 a1, u32 a2,
                     u32 a3);
/* fast_render=3 (D124): the readable build, NAME_rc. --fast-render-check
 * compares it with fast_render=1's build (hand_check_rc: RAM but the spill
 * area and the hand stack, and the control registers but FLAG); the other
 * checks run as at 2. */
u32 hand_check_rc(const char *name, HandFn reg, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);
u32 hand_check_rc_v0(const char *name, HandFn reg, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);
/* the oracle replay's shadow at fast_render=3 (hand_check.c; the arena
 * build, which links the mips2c units) */
extern int g_render_shadow;
u32 render_shadow(const char *name, HandFn unit, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);
u32 render_shadow_v0(const char *name, HandFn unit, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);
#if SPYRO_NATIVE_MEM
#define HAND_SHADOW_RC(name, a0, a1, a2, a3) /* no units in the native build */
#else
#define HAND_SHADOW_RC(name, a0, a1, a2, a3)                                   \
  g_render_shadow ? render_shadow(#name, name##_gen__m2c, name##_rc, a0, a1, a2, a3) :
#endif
/* With both --fast-render-check and --render-ctx-check, the hand check's
 * readable call is itself the presenter check (NAME_rcx), so one run does
 * both. */
#define HAND_PICK_RC(name, ref, a0, a1, a2, a3)                                \
  (HAND_SHADOW_RC(name, a0, a1, a2, a3)                                        \
   g_hand_check          ? hand_check_rc(#name, ref, g_render_ctx_check ? name##_rcx : name##_rc, a0, a1, a2, a3) \
   : g_nd_check          ? nd_check(#name, name##_rc, a0, a1, a2, a3)            \
   : g_render_ctx_check  ? render_ctx_check(#name, name##_rc, name##_pres, a0, a1, a2, a3) \
                         : name##_rc(a0, a1, a2, a3))
/* host --render-poison (D124, render_poison.c): at fast_render 1 and 2,
 * after each renderer call the scratchpad, the GTE's data registers and
 * FLAG, the spill area and the hand stack are overwritten; a run whose
 * outputs stay byte-identical shows nothing reads them across calls. */
extern int g_render_poison;
u32 render_poison(const char *name, HandFn fn, u32 a0, u32 a1, u32 a2, u32 a3);
#define HAND_PICK(name, hand, a0, a1, a2, a3)                                  \
  (g_fast_render_ro == 3 ? HAND_PICK_RC(name, hand, a0, a1, a2, a3)           \
   : g_render_poison     ? render_poison(#name, g_fast_render_ro == 2 ? name##_ngte : hand, a0, a1, a2, a3) \
   : g_fast_render_ro != 2 ? hand(a0, a1, a2, a3)                              \
   : g_hand_check        ? hand_check(#name, hand, name##_ngte, a0, a1, a2, a3) \
   : g_nd_check          ? nd_check(#name, name##_ngte, a0, a1, a2, a3)         \
   : g_render_ctx_check  ? render_ctx_check(#name, name##_ngte, name##_pres, a0, a1, a2, a3) \
                         : name##_ngte(a0, a1, a2, a3))

/* fast_render switch for a hand-written entry point (mips2c.cmake
 * M2C_RENDER_HAND): the mips2c unit's own entry is renamed NAME_gen; both of
 * its entries (the C one, NAME, and the register one, NAME__m2c, which
 * translated callers and the oracle replay use) pick the hand C or the unit.
 * HAND is the hand C, taking the four argument registers. In the NGTE build
 * this defines NAME_ngte, the same function with the native GTE maths. */
/* The logic builds' fast_render=3 entries, and the readable call run as
 * the presenter check (HAND_PICK_RC) */
#define HAND_RC_CTX(name)                                                      \
  u32 name##_rc(u32, u32, u32, u32);                                           \
  u32 name##_pres(u32, u32, u32, u32);                                         \
  static u32 name##_rcx(u32 a0, u32 a1, u32 a2, u32 a3) {                      \
    return render_ctx_check(#name, name##_rc, name##_pres, a0, a1, a2, a3);    \
  }
#if RENDER_PRES
/* The presenter build (NAME_hand_pres.c, stage 7d, D123): fast_render=3's
 * code (D124) on the presenter's state (render_ctx_names.h), as NAME_pres. */
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_pres(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#elif NGTE_RC
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_rc(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#elif NGTE
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_ngte(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#elif SPYRO_NATIVE_MEM
/* The native build (D100) links no mips2c unit: the hand C always, and no
 * stack arguments (only the units read them). */
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_ngte(u32, u32, u32, u32);                                         \
  HAND_RC_CTX(name)                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    (void)a4, (void)a5, (void)a6, (void)a7;                                    \
    return HAND_PICK(name, hand, a0, a1, a2, a3);                              \
  }
#else
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##_ngte(u32, u32, u32, u32);                                         \
  HAND_RC_CTX(name)                                                            \
  u32 name##__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {                            \
    if (g_fast_render_ro)                                                      \
      return HAND_PICK(name, hand, a0, a1, a2, a3);                            \
    return name##_gen__m2c(a0, a1, a2, a3);                                    \
  }                                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    u32 r_, sp_ = g_mips_sp;                                                   \
    if (!g_fast_render_ro)                                                     \
      return name##_gen(a0, a1, a2, a3, a4, a5, a6, a7);                       \
    /* the stack arguments, as mips2c's C entry stores them */                 \
    g_mips_sp = sp_ - 32u;                                                     \
    M_U32(sp_ - 16u) = a4;                                                     \
    M_U32(sp_ - 12u) = a5;                                                     \
    M_U32(sp_ - 8u) = a6;                                                      \
    M_U32(sp_ - 4u) = a7;                                                      \
    r_ = HAND_PICK(name, hand, a0, a1, a2, a3);                                \
    g_mips_sp = sp_;                                                           \
    return r_;                                                                 \
  }
#endif


/* The renderers written by hand after D80 (the shadows, the flame, the
 * particles; decisions D95): the register-file hand C, and since D98 its
 * NGTE build with native GTE maths as well as native_draw's records. So
 * --fast-render-check runs each call three ways from the same state, the
 * mips2c unit itself (NAME_gen__m2c), fast_render=1's build and
 * fast_render=2's, and compares both builds with the unit: RAM, the
 * scratchpad and every GTE register must come out the same. Otherwise as
 * HAND_RENDERER. */
/* fast_render=2's build of a unit: with ngte_fx (config.txt, host
 * --ngte-fx; D98) the native GTE maths, else the register file */
extern int g_ngte_fx;
#define HAND_UNIT2(name) (g_ngte_fx ? name##_ngte : name##_ndr)
#if SPYRO_NATIVE_MEM
/* no unit to check against: fast_render=1's build against 2's */
#define HAND_CHECK_UNIT(name, hand, a0, a1, a2, a3)                            \
  hand_check(#name, hand, HAND_UNIT2(name), a0, a1, a2, a3)
#else
#define HAND_CHECK_UNIT(name, hand, a0, a1, a2, a3)                            \
  hand_check_unit(#name, name##_gen__m2c, hand, HAND_UNIT2(name), a0, a1, a2, a3)
#endif
#define HAND_PICK_UNIT(name, hand, a0, a1, a2, a3)                             \
  (g_fast_render_ro == 3 ? HAND_PICK_RC(name, hand, a0, a1, a2, a3)           \
   : g_render_poison     ? render_poison(#name, g_fast_render_ro == 2 ? HAND_UNIT2(name) : hand, a0, a1, a2, a3) \
   : g_fast_render_ro != 2 ? hand(a0, a1, a2, a3)                              \
   : g_hand_check        ? HAND_CHECK_UNIT(name, hand, a0, a1, a2, a3)          \
   : g_nd_check          ? nd_check(#name, HAND_UNIT2(name), a0, a1, a2, a3)    \
   : g_render_ctx_check  ? render_ctx_check(#name, HAND_UNIT2(name), name##_pres, a0, a1, a2, a3) \
                         : HAND_UNIT2(name)(a0, a1, a2, a3))
#if RENDER_PRES || NGTE_RC
#define HAND_RENDERER_UNIT(name, hand) HAND_RENDERER(name, hand)
#elif NGTE && NGTE_MATH
#define HAND_RENDERER_UNIT(name, hand)                                         \
  u32 name##_ngte(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#elif NGTE
#define HAND_RENDERER_UNIT(name, hand)                                         \
  u32 name##_ndr(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#elif SPYRO_NATIVE_MEM
#define HAND_RENDERER_UNIT(name, hand)                                         \
  u32 name##_ngte(u32, u32, u32, u32);                                         \
  u32 name##_ndr(u32, u32, u32, u32);                                          \
  HAND_RC_CTX(name)                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    (void)a4, (void)a5, (void)a6, (void)a7;                                    \
    return HAND_PICK_UNIT(name, hand, a0, a1, a2, a3);                         \
  }
#else
#define HAND_RENDERER_UNIT(name, hand)                                         \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##_ngte(u32, u32, u32, u32);                                         \
  u32 name##_ndr(u32, u32, u32, u32);                                          \
  HAND_RC_CTX(name)                                                            \
  u32 name##__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {                            \
    if (g_fast_render_ro)                                                      \
      return HAND_PICK_UNIT(name, hand, a0, a1, a2, a3);                       \
    return name##_gen__m2c(a0, a1, a2, a3);                                    \
  }                                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    u32 r_, sp_ = g_mips_sp;                                                   \
    if (!g_fast_render_ro)                                                     \
      return name##_gen(a0, a1, a2, a3, a4, a5, a6, a7);                       \
    g_mips_sp = sp_ - 32u;                                                     \
    M_U32(sp_ - 16u) = a4;                                                     \
    M_U32(sp_ - 12u) = a5;                                                     \
    M_U32(sp_ - 8u) = a6;                                                      \
    M_U32(sp_ - 4u) = a7;                                                      \
    r_ = HAND_PICK_UNIT(name, hand, a0, a1, a2, a3);                           \
    g_mips_sp = sp_;                                                           \
    return r_;                                                                 \
  }
#endif

/* The renderer units' other functions, written by hand after D95 (the
 * culls, the sky's helpers, the environment's animations, the particle
 * slots; decisions D96): one version, with no records, so no NGTE build.
 * fast_render 0 runs the unit (NAME_gen), 1 and 2 the hand C;
 * --fast-render-check compares each call with the unit (NAME_gen__m2c):
 * RAM, the scratchpad and every GTE register. HAND takes the four argument
 * registers. Otherwise as HAND_RENDERER_UNIT. */
#define HAND_UNIT_FN(name, hand) HAND_UNIT_FN_(name, hand, hand_check)
/* For a function whose callers read its result: the check compares it too. */
#define HAND_UNIT_FN_V0(name, hand) HAND_UNIT_FN_(name, hand, hand_check_v0)
/* The B2 ones among them (the culls, the sky's clipping, the environment's
 * animation update) also have fast_render=3's build, NAME_hand_rc.c (D124):
 * HAND_UNIT_FN_RC. --fast-render-check compares it with the hand C of
 * fast_render 1 and 2 (hand_check_rc), and --render-poison runs at 1 and 2
 * as for the renderers. */
#if NGTE_RC
#define HAND_UNIT_FN_RC(name, hand) HAND_UNIT_FN_RC_(name, hand, 0)
#define HAND_UNIT_FN_RC_V0(name, hand) HAND_UNIT_FN_RC_(name, hand, 1)
#define HAND_UNIT_FN_RC_(name, hand, v0)                                       \
  u32 name##_rc(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#else
#if SPYRO_NATIVE_MEM
#define HAND_UNIT_SHADOW_RC(name, v0, a0, a1, a2, a3) /* no units in the native build */
#else
#define HAND_UNIT_SHADOW_RC(name, v0, a0, a1, a2, a3)                          \
  g_render_shadow ? (v0 ? render_shadow_v0 : render_shadow)(#name, name##_gen__m2c, name##_rc, a0, a1, a2, a3) :
#endif
#define HAND_UNIT_RC_PICK(name, hand, v0, a0, a1, a2, a3)                      \
  (g_fast_render_ro == 3                                                       \
     ? (HAND_UNIT_SHADOW_RC(name, v0, a0, a1, a2, a3)                          \
        g_hand_check ? (v0 ? hand_check_rc_v0 : hand_check_rc)(#name, hand, name##_rc, a0, a1, a2, a3) \
                     : name##_rc(a0, a1, a2, a3))                              \
   : g_render_poison ? render_poison(#name, hand, a0, a1, a2, a3)              \
                     : hand(a0, a1, a2, a3))
#define HAND_UNIT_FN_RC(name, hand) HAND_UNIT_FN_RC_(name, hand, 0)
#define HAND_UNIT_FN_RC_V0(name, hand) HAND_UNIT_FN_RC_(name, hand, 1)
#if SPYRO_NATIVE_MEM
#define HAND_UNIT_FN_RC_(name, hand, v0)                                       \
  u32 name##_rc(u32, u32, u32, u32);                                           \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    (void)a4, (void)a5, (void)a6, (void)a7;                                    \
    return HAND_UNIT_RC_PICK(name, hand, v0, a0, a1, a2, a3);                  \
  }
#else
#define HAND_UNIT_FN_RC_(name, hand, v0)                                       \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##_rc(u32, u32, u32, u32);                                           \
  u32 name##__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {                            \
    if (!g_fast_render_ro)                                                     \
      return name##_gen__m2c(a0, a1, a2, a3);                                  \
    if (g_hand_check && g_fast_render_ro == 2)                                 \
      return (v0 ? hand_check_v0 : hand_check)(#name, name##_gen__m2c, hand, a0, a1, a2, a3); \
    return HAND_UNIT_RC_PICK(name, hand, v0, a0, a1, a2, a3);                  \
  }                                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    u32 r_, sp_ = g_mips_sp;                                                   \
    if (!g_fast_render_ro)                                                     \
      return name##_gen(a0, a1, a2, a3, a4, a5, a6, a7);                       \
    g_mips_sp = sp_ - 32u;                                                     \
    M_U32(sp_ - 16u) = a4;                                                     \
    M_U32(sp_ - 12u) = a5;                                                     \
    M_U32(sp_ - 8u) = a6;                                                      \
    M_U32(sp_ - 4u) = a7;                                                      \
    r_ = name##__m2c(a0, a1, a2, a3);                                          \
    g_mips_sp = sp_;                                                           \
    return r_;                                                                 \
  }
#endif
#endif
#if NGTE_RC
#define HAND_UNIT_FN_(name, hand, check) /* no fast_render=3 build */
#elif SPYRO_NATIVE_MEM
#define HAND_UNIT_FN_(name, hand, check)                                       \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    (void)a4, (void)a5, (void)a6, (void)a7;                                    \
    return hand(a0, a1, a2, a3);                                               \
  }
#else
#define HAND_UNIT_FN_(name, hand, check)                                       \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {                            \
    if (!g_fast_render_ro)                                                     \
      return name##_gen__m2c(a0, a1, a2, a3);                                  \
    if (g_hand_check && g_fast_render_ro == 2)                                 \
      return check(#name, name##_gen__m2c, hand, a0, a1, a2, a3);              \
    return hand(a0, a1, a2, a3);                                               \
  }                                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    u32 r_, sp_ = g_mips_sp;                                                   \
    if (!g_fast_render_ro)                                                     \
      return name##_gen(a0, a1, a2, a3, a4, a5, a6, a7);                       \
    g_mips_sp = sp_ - 32u;                                                     \
    M_U32(sp_ - 16u) = a4;                                                     \
    M_U32(sp_ - 12u) = a5;                                                     \
    M_U32(sp_ - 8u) = a6;                                                      \
    M_U32(sp_ - 4u) = a7;                                                      \
    r_ = name##__m2c(a0, a1, a2, a3);                                          \
    g_mips_sp = sp_;                                                           \
    return r_;                                                                 \
  }
#endif


#if NGTE_RC
/* fast_render=3 (D124): the GTE's data registers, FLAG and its commands are
 * not used. Anything left of them in a NAME_hand_rc.c or NAME_hand_pres.c
 * build fails to compile here. The control registers (CTC2, CFC2 and the
 * C2_ names of gtereg.h's control half) stay. */
#undef MTC2
#undef MFC2
#define MTC2(v, r) ngte_rc_no_data_registers
#define MFC2(r) ngte_rc_no_data_registers
#undef gte_rtps
#undef gte_nclip
#define gte_rtps() ngte_rc_no_gte_commands
#define gte_nclip() ngte_rc_no_gte_commands
#undef m2c_gte_0x138041C
#define m2c_gte_0x138041C ngte_rc_no_gte_commands
#undef m2c_gte_0x170000C
#define m2c_gte_0x170000C ngte_rc_no_gte_commands
#undef m2c_gte_0x180001
#define m2c_gte_0x180001 ngte_rc_no_gte_commands
#undef m2c_gte_0x190003D
#define m2c_gte_0x190003D ngte_rc_no_gte_commands
#undef m2c_gte_0x198003D
#define m2c_gte_0x198003D ngte_rc_no_gte_commands
#undef m2c_gte_0x1A0003E
#define m2c_gte_0x1A0003E ngte_rc_no_gte_commands
#undef m2c_gte_0x1A8003E
#define m2c_gte_0x1A8003E ngte_rc_no_gte_commands
#undef m2c_gte_0x280030
#define m2c_gte_0x280030 ngte_rc_no_gte_commands
#undef m2c_gte_0x41E012
#define m2c_gte_0x41E012 ngte_rc_no_gte_commands
#undef m2c_gte_0x43E012
#define m2c_gte_0x43E012 ngte_rc_no_gte_commands
#undef m2c_gte_0x45E012
#define m2c_gte_0x45E012 ngte_rc_no_gte_commands
#undef m2c_gte_0x480012
#define m2c_gte_0x480012 ngte_rc_no_gte_commands
#undef m2c_gte_0x486012
#define m2c_gte_0x486012 ngte_rc_no_gte_commands
#undef m2c_gte_0x49E012
#define m2c_gte_0x49E012 ngte_rc_no_gte_commands
#undef m2c_gte_0x780010
#define m2c_gte_0x780010 ngte_rc_no_gte_commands
#undef m2c_gte_0x980011
#define m2c_gte_0x980011 ngte_rc_no_gte_commands
#undef m2c_gte_0xA00428
#define m2c_gte_0xA00428 ngte_rc_no_gte_commands
#undef C2_VX0
#define C2_VX0 ngte_rc_no_data_registers
#undef C2_VY0
#define C2_VY0 ngte_rc_no_data_registers
#undef C2_VZ0
#define C2_VZ0 ngte_rc_no_data_registers
#undef C2_VX1
#define C2_VX1 ngte_rc_no_data_registers
#undef C2_VY1
#define C2_VY1 ngte_rc_no_data_registers
#undef C2_VZ1
#define C2_VZ1 ngte_rc_no_data_registers
#undef C2_VX2
#define C2_VX2 ngte_rc_no_data_registers
#undef C2_VY2
#define C2_VY2 ngte_rc_no_data_registers
#undef C2_VZ2
#define C2_VZ2 ngte_rc_no_data_registers
#undef C2_R
#define C2_R ngte_rc_no_data_registers
#undef C2_G
#define C2_G ngte_rc_no_data_registers
#undef C2_B
#define C2_B ngte_rc_no_data_registers
#undef C2_CODE
#define C2_CODE ngte_rc_no_data_registers
#undef C2_OTZ
#define C2_OTZ ngte_rc_no_data_registers
#undef C2_IR0
#define C2_IR0 ngte_rc_no_data_registers
#undef C2_IR1
#define C2_IR1 ngte_rc_no_data_registers
#undef C2_IR2
#define C2_IR2 ngte_rc_no_data_registers
#undef C2_IR3
#define C2_IR3 ngte_rc_no_data_registers
#undef C2_SXY0
#define C2_SXY0 ngte_rc_no_data_registers
#undef C2_SX0
#define C2_SX0 ngte_rc_no_data_registers
#undef C2_SY0
#define C2_SY0 ngte_rc_no_data_registers
#undef C2_SXY1
#define C2_SXY1 ngte_rc_no_data_registers
#undef C2_SX1
#define C2_SX1 ngte_rc_no_data_registers
#undef C2_SY1
#define C2_SY1 ngte_rc_no_data_registers
#undef C2_SXY2
#define C2_SXY2 ngte_rc_no_data_registers
#undef C2_SX2
#define C2_SX2 ngte_rc_no_data_registers
#undef C2_SY2
#define C2_SY2 ngte_rc_no_data_registers
#undef C2_SXYP
#define C2_SXYP ngte_rc_no_data_registers
#undef C2_SXP
#define C2_SXP ngte_rc_no_data_registers
#undef C2_SYP
#define C2_SYP ngte_rc_no_data_registers
#undef C2_SZ0
#define C2_SZ0 ngte_rc_no_data_registers
#undef C2_SZ1
#define C2_SZ1 ngte_rc_no_data_registers
#undef C2_SZ2
#define C2_SZ2 ngte_rc_no_data_registers
#undef C2_SZ3
#define C2_SZ3 ngte_rc_no_data_registers
#undef C2_RGB0
#define C2_RGB0 ngte_rc_no_data_registers
#undef C2_R0
#define C2_R0 ngte_rc_no_data_registers
#undef C2_G0
#define C2_G0 ngte_rc_no_data_registers
#undef C2_B0
#define C2_B0 ngte_rc_no_data_registers
#undef C2_CD0
#define C2_CD0 ngte_rc_no_data_registers
#undef C2_RGB1
#define C2_RGB1 ngte_rc_no_data_registers
#undef C2_R1
#define C2_R1 ngte_rc_no_data_registers
#undef C2_G1
#define C2_G1 ngte_rc_no_data_registers
#undef C2_B1
#define C2_B1 ngte_rc_no_data_registers
#undef C2_CD1
#define C2_CD1 ngte_rc_no_data_registers
#undef C2_RGB2
#define C2_RGB2 ngte_rc_no_data_registers
#undef C2_R2
#define C2_R2 ngte_rc_no_data_registers
#undef C2_G2
#define C2_G2 ngte_rc_no_data_registers
#undef C2_B2
#define C2_B2 ngte_rc_no_data_registers
#undef C2_CD2
#define C2_CD2 ngte_rc_no_data_registers
#undef C2_RES1
#define C2_RES1 ngte_rc_no_data_registers
#undef C2_MAC0
#define C2_MAC0 ngte_rc_no_data_registers
#undef C2_MAC1
#define C2_MAC1 ngte_rc_no_data_registers
#undef C2_MAC2
#define C2_MAC2 ngte_rc_no_data_registers
#undef C2_MAC3
#define C2_MAC3 ngte_rc_no_data_registers
#undef C2_IRGB
#define C2_IRGB ngte_rc_no_data_registers
#undef C2_ORGB
#define C2_ORGB ngte_rc_no_data_registers
#undef C2_LZCS
#define C2_LZCS ngte_rc_no_data_registers
#undef C2_LZCR
#define C2_LZCR ngte_rc_no_data_registers
#undef C2_FLAG
#define C2_FLAG ngte_rc_no_data_registers
/* nor the spill area or the hand stack (a name left resolves to nothing
 * at link time) */
#define g_mips_sp ngte_rc_no_hand_stack
#define D_80077DD8 ngte_rc_no_spill_area
/* gteRegs.CP2D by name: the logic's control registers only (the presenter's
 * have no data half at all, render_ctx.h) */
typedef struct { psxCP2Ctrl CP2C; } NgteCtlRegs;
#if !RENDER_PRES
#define gteRegs (*(NgteCtlRegs *)&gteRegs.CP2C)
#endif
/* CTC2 as mips2c_rt.h's but for FLAG (31), a command's result, which these
 * builds never move: so a register number the compiler can't fold (a
 * loop's) needs no out-of-line move */
static inline __attribute__((always_inline)) void ngte_rc_ctc2(unsigned int value, int reg) {
  switch (reg) {
  case 4: case 12: case 20: case 26: case 27: case 29: case 30:
    value = (unsigned int)(int)(short)value;
    break;
  }
  gteRegs.CP2C.p[reg].d = value;
}
#undef CTC2
#define CTC2(v, r) ngte_rc_ctc2((v), (r))
#endif

#endif
