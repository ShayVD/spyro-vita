/* Every place the port turns a value the game packed into fewer bits, or
 * tagged with bit 31, back into an address (docs/native-plan.md stage 4,
 * decisions D97, D100). The game's memory is the 2 MB arena
 * (src/platform/common/arena.c); what the game packs:
 *  - OT links: a tag's low 24 bits (PsyCross's setaddr, the renderers);
 *  - animation frames: the frame word's low 21 bits, or (Spyro's frames)
 *    the address halved into them (loaders.c PatchMobyModelPointers);
 *  - a SimpleModel in g_Models[]: bit 31 cleared (loaders.c);
 *  - a collision model's kind: bit 31 of the pointer (the WAD's offset
 *    carries it; loaders.c, read by the collision code and moby_update.c).
 *
 * The arena and moved builds (D97) expand to the expressions the code has
 * always used, which need the arena 16 MB-aligned above 2 GB. The native
 * build (SPYRO_NATIVE_MEM) has no aliases of PS1 RAM (mips2c_rt.h
 * MIPS_KUSEG is a plain cast there), so a value that was an alias decodes
 * here instead. Values are host addresses (uintptr_t). */
#ifndef PSX_MEM_H
#define PSX_MEM_H

#include <stdint.h>

extern uintptr_t g_psx_arena_base;

/* An OT link (a tag's low 24 bits) -> the primitive's address. */
#define MEM_OT_NEXT(tag) (g_psx_arena_base | ((uint32_t)(tag) & 0xFFFFFFu))
/* ... and its offset in the arena (walk.c's copy of it). */
#define MEM_OT_OFS(tag) ((uint32_t)(tag) & 0xFFFFFFu)
/* A frame word's 21-bit packed address. */
#define MEM_ANIM21(w) (g_psx_arena_base + ((uint32_t)(w) & 0x1FFFFFu))
/* A frame word holding the address halved into its low 21 bits (Spyro's
 * frames). */
#define MEM_ANIM_HALF(w) (g_psx_arena_base + (((uint32_t)(w) << 11) >> 10))
/* A collision model pointer (Moby m_CollisionGroup, Model
 * m_CollisionModels[]) and its kind, the bit the WAD's offset carries. */
#define MEM_COLL_PTR(p) ((uintptr_t)(p) | 0x80000000u)
#define MEM_COLL_TAGGED(p) ((int32_t)(uintptr_t)(p) < 0)
/* A g_Models[] word: a SimpleModel (the lit and HUD models) has bit 31
 * cleared; a null word counts as one, as the original's sign test does. */
#define MEM_MODEL_SIMPLE(w) ((int32_t)(uintptr_t)(w) >= 0)
/* An address in the form g_Models[] holds a SimpleModel's (to compare
 * with one). */
#define MEM_SIMPLE_ADDR(a) ((uint32_t)(a) & 0x7FFFFFFFu)

#if SPYRO_NATIVE_MEM
/* the address a g_Models[] word names (bit 31 put back) */
#define MEM_MODEL_PTR(w) ((uint32_t)(w) | 0x80000000u)
/* an address the moby renderer kept shifted left by 8 (its face-save list) */
#define MEM_ADDR24(a) ((uint32_t)(a) | 0x80000000u)
#else
/* the KUSEG alias does both (mips2c_rt.h) */
#define MEM_MODEL_PTR(w) MIPS_KUSEG(w)
#define MEM_ADDR24(a) MIPS_KUSEG((uint32_t)(a) | 0x80000000u)
#endif

#endif
