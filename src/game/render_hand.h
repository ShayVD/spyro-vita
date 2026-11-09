/* Shared pieces of the hand-written renderers (sky_hand.c, ...; decisions
 * D72). The renderers were hand-written MIPS asm; mips2c translated them
 * instruction by instruction, with every memory access through MIPS_KUSEG
 * and every GTE command through PsyCross's decoder. The hand C does the same
 * work with pointers into the arena and the GTE's maths on C values
 * (gte_native.h).
 *
 * Since D127 the hand C is readable C only, by D87's rule (stage 7d2,
 * D124): the control registers a renderer sets are set as before (the
 * rotation and translation; what libgte and the camera set is read); the
 * data registers, FLAG and the GTE commands are not used at all, nor the
 * PS1 scratchpad, the register spill area or the hand stack (RENDER_READABLE
 * below makes any of them a compile error). fast_render is 3 (these) or, in
 * the arena build, 0 (the mips2c units); the register-file builds of
 * fast_render 1 and 2 (D72, D80, D95, D98) are gone. Checked against the
 * units by the oracle replay's shadow and --fast-render-check
 * (hand_check.c), and against the presenter's build by --render-ctx-check. */
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
 * (a primitive it is writing, the working memory, a list it walks). */
#define AT32(a) (*(u32 *)(uintptr_t)(u32)(a))
#define AT16(a) (*(u16 *)(uintptr_t)(u32)(a))
#define AT8(a) (*(u8 *)(uintptr_t)(u32)(a))
/* The word BYTES bytes into a table (the asm's byte offsets into word
 * tables: vertex, colour and clip-word indices come pre-multiplied by 4). */
#define WORD_AT(base, bytes) (*(const u32 *)((const u8 *)(base) + (bytes)))

/* A renderer's working memory, where the asm used the PS1 scratchpad: its
 * own (g_render_work, render_rc.c; the presenter's g_pres_scratch,
 * render_ctx_names.h). Nothing reads it across calls (the poison audit,
 * D124), so it is a buffer, not state. */
extern unsigned char g_render_work[1024];
#define SCRATCH_BASE ((u32)(uintptr_t)g_render_work)

/* The GTE's maths on C values (D80, D124). */
#include "gte_native.h"

/* What the asm leaves in the GTE that a renderer still needs: the vertex in
 * V0 after the last (a moby sprite's projection takes it, moby_hand.c
 * DrawSprite), the screen words NCLIP's winding is taken from, and the far
 * colour of the last colour command (a control register the asm sets),
 * written at the points the asm's last command would have set it. The
 * register-file builds kept the GTE's whole state here (D80's NgteSync);
 * the calls that recorded it for them stay as no-ops. */
typedef struct {
  u32 v0xy, v0z;                    /* the vertex the asm leaves in V0 */
  u32 sxy0, sxy1, sxy2;             /* SXY0-2 as last moved there */
  u32 rfc[1], gfc[1], bfc[1], nc;   /* the last colour command's far colour */
} NgteSync;

static inline void ngte_sync_init(NgteSync *s) { s->sxy0 = s->sxy1 = s->sxy2 = s->v0xy = s->v0z = s->nc = 0; }
static inline void ngte_rtps_rec(NgteSync *s, u32 vxy, u32 vz, u32 rd) { (void)s, (void)vxy, (void)vz, (void)rd; }
#define NGTE_RD_SZ3 1
static inline void ngte_v0(NgteSync *s, u32 vxy, u32 vz) {
  s->v0xy = vxy;
  s->v0z = vz;
}
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

/* --fast-render-check (host, the arena build; hand_check.c): each call runs
 * the mips2c unit, then the hand C, from the same state, and the two are
 * compared. hand_check: RAM, the scratchpad and every GTE register (the
 * functions written readable from the start, HAND_UNIT_FN); hand_check_rc:
 * RAM but the scratchpad, the spill area and the stack below $sp, the
 * control registers but FLAG (the renderers, which leave the rest alone,
 * D124). Set at start-up, before any game code. */
extern int g_hand_check;
typedef u32 (*HandFn)(u32, u32, u32, u32);
u32 hand_check(const char *name, HandFn unit, HandFn hand, u32 a0, u32 a1, u32 a2, u32 a3);
/* The same, also comparing the two results (functions returning a value). */
u32 hand_check_v0(const char *name, HandFn unit, HandFn hand, u32 a0, u32 a1, u32 a2, u32 a3);
u32 hand_check_rc(const char *name, HandFn unit, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);
u32 hand_check_rc_v0(const char *name, HandFn unit, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);
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
/* the oracle replay's shadow (hand_check.c, D124; the arena build, which
 * links the mips2c units): the unit, then the hand C; the unit's state goes
 * on where nothing was compared */
extern int g_render_shadow;
u32 render_shadow(const char *name, HandFn unit, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);
u32 render_shadow_v0(const char *name, HandFn unit, HandFn rc, u32 a0, u32 a1, u32 a2, u32 a3);

#if !SPYRO_NATIVE_MEM
/* The arena build's C entry of a function the units also have: the stack
 * arguments as mips2c's C entry stores them, for the unit (fast_render=0,
 * the checks); defined here, before RENDER_READABLE takes g_mips_sp's name
 * away from the renderers' own code. */
static inline u32 hand_args_push(u32 a4, u32 a5, u32 a6, u32 a7) {
  const u32 sp = g_mips_sp;
  g_mips_sp = sp - 32u;
  M_U32(sp - 16u) = a4;
  M_U32(sp - 12u) = a5;
  M_U32(sp - 8u) = a6;
  M_U32(sp - 4u) = a7;
  return sp;
}
static inline void hand_args_pop(u32 sp) { g_mips_sp = sp; }
#endif

/* fast_render switch for a hand-written renderer (mips2c.cmake
 * M2C_RENDER_HAND): the mips2c unit's own entry is renamed NAME_gen; both of
 * its entries (the C one, NAME, and the register one, NAME__m2c, which
 * translated callers and the oracle replay use) pick the hand C or the unit.
 * HAND is the hand C, taking the four argument registers. In the presenter's
 * build (NAME_hand_pres.c, D123) it is NAME_pres. With both
 * --fast-render-check and --render-ctx-check the hand check's call of the
 * hand C is itself the presenter check (NAME_rcx), so one run does both. */
#if RENDER_PRES
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_pres(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#elif SPYRO_NATIVE_MEM
/* The native build (D100) links no mips2c unit: the hand C always, and no
 * stack arguments (only the units read them). */
#define HAND_PICK(name, hand, a0, a1, a2, a3)                                  \
  (g_nd_check           ? nd_check(#name, hand, a0, a1, a2, a3)                \
   : g_render_ctx_check ? render_ctx_check(#name, hand, name##_pres, a0, a1, a2, a3) \
                        : hand(a0, a1, a2, a3))
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_pres(u32, u32, u32, u32);                                         \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    (void)a4, (void)a5, (void)a6, (void)a7;                                    \
    return HAND_PICK(name, hand, a0, a1, a2, a3);                              \
  }
#else
#define HAND_PICK(name, hand, a0, a1, a2, a3)                                  \
  (g_render_shadow      ? render_shadow(#name, name##_gen__m2c, hand, a0, a1, a2, a3) \
   : g_hand_check       ? hand_check_rc(#name, name##_gen__m2c, g_render_ctx_check ? name##_rcx : hand, \
                                        a0, a1, a2, a3)                        \
   : g_nd_check         ? nd_check(#name, hand, a0, a1, a2, a3)                \
   : g_render_ctx_check ? render_ctx_check(#name, hand, name##_pres, a0, a1, a2, a3) \
                        : hand(a0, a1, a2, a3))
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##_pres(u32, u32, u32, u32);                                         \
  static u32 name##_rcx(u32 a0, u32 a1, u32 a2, u32 a3) {                      \
    return render_ctx_check(#name, hand, name##_pres, a0, a1, a2, a3);         \
  }                                                                            \
  u32 name##__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {                            \
    if (!g_fast_render_ro)                                                     \
      return name##_gen__m2c(a0, a1, a2, a3);                                  \
    return HAND_PICK(name, hand, a0, a1, a2, a3);                              \
  }                                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    u32 r_, sp_;                                                               \
    if (!g_fast_render_ro)                                                     \
      return name##_gen(a0, a1, a2, a3, a4, a5, a6, a7);                       \
    sp_ = hand_args_push(a4, a5, a6, a7);                                      \
    r_ = HAND_PICK(name, hand, a0, a1, a2, a3);                                \
    hand_args_pop(sp_);                                                        \
    return r_;                                                                 \
  }
#endif

/* The renderer units' other functions, written by hand after D95 (decisions
 * D96). HAND_UNIT_FN: those whose asm uses no GTE, scratchpad or register
 * model (the environment's animations, the particle slots), compared whole
 * by --fast-render-check (hand_check). HAND_UNIT_FN_RC: the B2 ones (the
 * culls, the sky's fog and portal clipping, the environment's animation
 * update), readable since D124, compared as the renderers are
 * (hand_check_rc, the oracle replay's shadow) and built for the presenter
 * too (NAME_pres). fast_render=0 runs the unit (NAME_gen). HAND takes the
 * four argument registers; the _V0 forms are for functions whose callers
 * read the result, which the checks compare too. */
#define HAND_UNIT_FN(name, hand) HAND_UNIT_FN_(name, hand, hand_check)
#define HAND_UNIT_FN_V0(name, hand) HAND_UNIT_FN_(name, hand, hand_check_v0)
#define HAND_UNIT_FN_RC(name, hand) HAND_UNIT_FN_RC_(name, hand, 0)
#define HAND_UNIT_FN_RC_V0(name, hand) HAND_UNIT_FN_RC_(name, hand, 1)
#if RENDER_PRES
#define HAND_UNIT_FN_(name, hand, check) /* not in the presenter's builds */
#define HAND_UNIT_FN_RC_(name, hand, v0)                                       \
  u32 name##_pres(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#elif SPYRO_NATIVE_MEM
#define HAND_UNIT_FN_(name, hand, check)                                       \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    (void)a4, (void)a5, (void)a6, (void)a7;                                    \
    return hand(a0, a1, a2, a3);                                               \
  }
#define HAND_UNIT_FN_RC_(name, hand, v0) HAND_UNIT_FN_(name, hand, 0)
#else
#define HAND_UNIT_FN_ENTRIES(name, pick)                                       \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {                            \
    if (!g_fast_render_ro)                                                     \
      return name##_gen__m2c(a0, a1, a2, a3);                                  \
    return pick;                                                               \
  }                                                                            \
  u32 name(u32 a0, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5, u32 a6, u32 a7) {   \
    u32 r_, sp_;                                                               \
    if (!g_fast_render_ro)                                                     \
      return name##_gen(a0, a1, a2, a3, a4, a5, a6, a7);                       \
    sp_ = hand_args_push(a4, a5, a6, a7);                                      \
    r_ = name##__m2c(a0, a1, a2, a3);                                          \
    hand_args_pop(sp_);                                                        \
    return r_;                                                                 \
  }
#define HAND_UNIT_FN_(name, hand, check)                                       \
  HAND_UNIT_FN_ENTRIES(name, g_hand_check ? check(#name, name##_gen__m2c, hand, a0, a1, a2, a3) \
                                          : hand(a0, a1, a2, a3))
#define HAND_UNIT_FN_RC_(name, hand, v0)                                       \
  HAND_UNIT_FN_ENTRIES(name,                                                   \
    g_render_shadow ? (v0 ? render_shadow_v0 : render_shadow)(#name, name##_gen__m2c, hand, a0, a1, a2, a3) \
    : g_hand_check  ? (v0 ? hand_check_rc_v0 : hand_check_rc)(#name, name##_gen__m2c, hand, a0, a1, a2, a3) \
                    : hand(a0, a1, a2, a3))
#endif

/* RENDER_READABLE (each renderer file defines it before including this,
 * D124, D127): the GTE's data registers, FLAG and its commands are not
 * used. Anything left of them in a renderer, or in its presenter build,
 * fails to compile here. The control registers (CTC2, CFC2 and the C2_
 * names of gtereg.h's control half) stay. */
#if RENDER_READABLE
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
#endif /* RENDER_READABLE */

#endif
