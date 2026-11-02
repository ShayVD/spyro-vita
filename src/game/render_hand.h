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

/* The scratchpad (0x1F800000 on the PS1) is at the arena base. */
#define SCRATCH_BASE ((u32)g_psx_arena_base)

/* GTE commands, as the asm's opcodes (gte_inline.h). */
#define gte_rtps() m2c_gte_0x180001()
#define gte_nclip() m2c_gte_0x1400006()

/* The register save every renderer starts with: s0-s7, gp, sp, fp, ra to
 * D_80077DD8 (loaded back before `jr $ra`). mips2c's units start each call
 * with s0-s7, gp and fp at 0, sp at g_mips_sp and ra at MIPS_RA_SENTINEL,
 * so that is what they store; the hand C stores the same, so RAM stays
 * byte-identical to fast_render=0 (the oracle ignores the area). */
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

/* fast_render=2 (decisions D79, docs/native-plan.md stage 1): each hand
 * renderer is compiled twice. NAME_hand.c itself, with NGTE 0, is today's
 * hand C on the GTE register file (fast_render=1); NAME_hand_ngte.c
 * includes it with NGTE 1, which turns on the native GTE maths of
 * gte_native.h at the sites marked `if (NGTE ...)`. With NGTE 0 those
 * branches compile away and the code is the fast_render=1 code as it was.
 *
 * How the native code keeps the register file exact: the GTE's state
 * between functions stays in gteRegs (the mips2c units, PsyCross and the
 * decomp C read and write it there). A vertex loop runs native for all but
 * its last NGTE_TAIL vertices and the register-file code for those, so the
 * FIFOs, MAC, IR and FLAG end as the register-file loop leaves them. A
 * polygon loop keeps what its last native commands would have left (the
 * SXY words it set, NCLIP's MAC0, ...) in an NgteSync and writes it to the
 * register file (ngte_flush) before any register-file code runs and before
 * the renderer returns. */
#ifndef NGTE
#define NGTE 0
#endif
#include "gte_native.h"
/* The last vertices of a vertex loop that run on the register file: SZ0-3
 * is the deepest FIFO, so after four RTPS every register an RTPS writes
 * holds what the register-file loop leaves. */
#define NGTE_TAIL 4

/* What native NCLIPs have left pending for the register file. */
typedef struct {
  u32 sxy0, sxy1, sxy2; /* the screen positions the code put in SXY0-2 */
  s32 mac0;             /* the last NCLIP's result */
  u8 sxy_set, nclip;    /* SXY0-2 written, and an NCLIP run, since the flush */
} NgteSync;

#define NGTE_SYNC_INIT {0, 0, 0, 0, 0, 0}

/* MTC2 to SXY0-2, as the register-file code would: whole words */
static inline void ngte_set_sxy(NgteSync *s, u32 xy0, u32 xy1, u32 xy2) {
  s->sxy0 = xy0;
  s->sxy1 = xy1;
  s->sxy2 = xy2;
  s->sxy_set = 1;
}
/* NCLIP on what's pending: MAC0, FLAG 0 */
static inline s32 ngte_nclip_s(NgteSync *s) {
  s->mac0 = ngte_nclip(s->sxy0, s->sxy1, s->sxy2);
  s->nclip = 1;
  return s->mac0;
}
/* The register file as the pending moves and NCLIPs would have left it. */
static inline void ngte_flush(NgteSync *s) {
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

/* The last RTPS of a native vertex loop whose only GTE work is RTPS on V0
 * (and plain reads of what it leaves). Replaying the last four on the
 * register file, with the same projection, leaves every register as the
 * whole loop would have: SZ0-3, SXY0-2, MAC, IR, FLAG. Then V0 gets what
 * the code last moved there (the pipelined load of the vertex after). */
typedef struct {
  u32 vxy[4], vz[4];
  u32 n;          /* RTPS since the last replay */
  u32 v0xy, v0z;  /* V0 after them, when v0 is set */
  int v0;
} NgteRing;

#define NGTE_RING_INIT {{0, 0, 0, 0}, {0, 0, 0, 0}, 0, 0, 0, 0}

static inline void ngte_ring_push(NgteRing *r, u32 vxy, u32 vz) {
  r->vxy[r->n & 3] = vxy;
  r->vz[r->n & 3] = vz;
  r->n++;
}
static inline void ngte_ring_v0(NgteRing *r, u32 vxy, u32 vz) {
  r->v0xy = vxy;
  r->v0z = vz;
  r->v0 = 1;
}
/* READ_SZ3: the loop read SZ3 after each RTPS (MFC2 19 zeroes its high
 * half; the replay does the same). */
static inline void ngte_ring_replay(NgteRing *r, int read_sz3) {
  u32 k = r->n > 4 ? r->n - 4 : 0;
  for (; k < r->n; k++) {
    MTC2(r->vz[k & 3], 1);
    MTC2(r->vxy[k & 3], 0);
    gte_rtps();
    if (read_sz3)
      (void)MFC2(19);
  }
  if (r->v0) {
    MTC2(r->v0z, 1);
    MTC2(r->v0xy, 0);
  }
  r->n = 0;
  r->v0 = 0;
}

/* Run both hand renderers from the same state and compare RAM, the
 * scratchpad and every GTE register (host --fast-render-check, hand_check.c).
 * Set at start-up, before any game code. */
extern int g_hand_check;
typedef u32 (*HandFn)(u32, u32, u32, u32);
u32 hand_check(const char *name, HandFn reg, HandFn native, u32 a0, u32 a1, u32 a2, u32 a3);

/* The entry fast_render picks: 1 the hand C on the register file, 2 its
 * native-GTE build (NAME_ngte, from NAME_hand_ngte.c). */
#define HAND_PICK(name, hand, a0, a1, a2, a3)                                  \
  (g_fast_render_ro != 2 ? hand(a0, a1, a2, a3)                                \
   : g_hand_check        ? hand_check(#name, hand, name##_ngte, a0, a1, a2, a3) \
                         : name##_ngte(a0, a1, a2, a3))

/* fast_render switch for a hand-written entry point (mips2c.cmake
 * M2C_RENDER_HAND): the mips2c unit's own entry is renamed NAME_gen; both of
 * its entries (the C one, NAME, and the register one, NAME__m2c, which
 * translated callers and the oracle replay use) pick the hand C or the unit.
 * HAND is the hand C, taking the four argument registers. In the NGTE build
 * this defines NAME_ngte, the same function with the native GTE maths. */
#if NGTE
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_ngte(u32 a0, u32 a1, u32 a2, u32 a3) { return hand(a0, a1, a2, a3); }
#else
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##_ngte(u32, u32, u32, u32);                                         \
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

#endif
