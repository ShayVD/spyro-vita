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

/* fast_render switch for a hand-written entry point (mips2c.cmake
 * M2C_RENDER_HAND): the mips2c unit's own entry is renamed NAME_gen; both of
 * its entries (the C one, NAME, and the register one, NAME__m2c, which
 * translated callers and the oracle replay use) pick the hand C or the unit.
 * HAND is the hand C, taking the four argument registers. */
#define HAND_RENDERER(name, hand)                                              \
  u32 name##_gen(u32, u32, u32, u32, u32, u32, u32, u32);                      \
  u32 name##_gen__m2c(u32, u32, u32, u32);                                     \
  u32 name##__m2c(u32 a0, u32 a1, u32 a2, u32 a3) {                            \
    if (g_fast_render_ro)                                                      \
      return hand(a0, a1, a2, a3);                                             \
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
    r_ = hand(a0, a1, a2, a3);                                                 \
    g_mips_sp = sp_;                                                           \
    return r_;                                                                 \
  }

#endif
