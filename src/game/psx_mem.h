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
 * MIPS_KUSEG is a plain cast there) and no demands on the base: everything
 * decodes relative to it, and the tags are bit 0. Values are host
 * addresses. */
#ifndef PSX_MEM_H
#define PSX_MEM_H

#include <stdint.h>

extern uintptr_t g_psx_arena_base;

#if SPYRO_NATIVE_MEM
/* The arena at any base (D100): N packed bits are the address's low N bits,
 * and the arena (2 MB) fits in 2^N bytes, so they decode relative to the
 * base. */
#define MEM_LOW_(bits, mask)                                                   \
  (g_psx_arena_base + (((uint32_t)(bits) - (uint32_t)g_psx_arena_base) & (mask)))
#define MEM_OT_NEXT(tag) MEM_LOW_(tag, 0xFFFFFFu)
#define MEM_OT_OFS(tag) (((uint32_t)(tag) - (uint32_t)g_psx_arena_base) & 0xFFFFFFu)
#define MEM_ANIM21(w) MEM_LOW_(w, 0x1FFFFFu)
#define MEM_ANIM_HALF(w) MEM_LOW_(((uint32_t)(w) << 11) >> 10, 0x3FFFFFu)
#define MEM_ADDR24(a) MEM_LOW_(a, 0xFFFFFFu)
/* The tags are bit 0 instead of bit 31 (the models and collision models
 * are word-aligned; loaders.c checks it), so they travel with the pointer
 * as before and nothing needs the arena above 2 GB. */
#define MEM_COLL_PTR(p) ((uintptr_t)(p) & ~(uintptr_t)1)
#define MEM_COLL_TAGGED(p) ((int)((uintptr_t)(p) & 1))
#define MEM_MODEL_PTR(w) ((uintptr_t)(w) & ~(uintptr_t)1)
#define MEM_MODEL_SIMPLE(w) (!(w) || ((uintptr_t)(w) & 1))
#define MEM_SIMPLE_ADDR(a) ((uintptr_t)(a) | 1)
/* OT tags the renderers build: an asm tag constant K (length << 24 |
 * 0x80000000), kept as is; a tag word linking to P, or (T 0, the chain's
 * first store) P itself; a chain's last tag. The arena may cross a 16 MB
 * line, so the link is P's low 24 bits, not an XOR with a fixed top byte. */
#define MEM_TAG(k) ((uint32_t)(k))
#define MEM_TAG_LINK(t, p)                                                     \
  ((t) ? ((((uint32_t)(t) ^ 0x80000000u) & 0xFF000000u) | ((uint32_t)(p) & 0xFFFFFFu)) : (uint32_t)(p))
#define MEM_TAG_LINK_AT(t, p, off) MEM_TAG_LINK(t, (uint32_t)(p) + (off))
#define MEM_TAG_END(t) (((uint32_t)(t) ^ 0x80000000u) & 0xFF000000u)
/* A subdivision list's sector header: its offset >> 2, below any polygon
 * address. */
#define MEM_SUBDIV_HDR(sec) (((uint32_t)(sec) - (uint32_t)g_psx_arena_base) >> 2)
#define MEM_SUBDIV_IS_HDR(e) ((uint32_t)(e) < 0x80000u)
#define MEM_SUBDIV_SEC(e) ((uint32_t)g_psx_arena_base + ((uint32_t)(e) << 2))
#else
/* An OT link (a tag's low 24 bits) -> the primitive's address. */
#define MEM_OT_NEXT(tag) (g_psx_arena_base | ((uint32_t)(tag) & 0xFFFFFFu))
/* ... and its offset in the arena (walk.c's copy of it). */
#define MEM_OT_OFS(tag) ((uint32_t)(tag) & 0xFFFFFFu)
/* A frame word's 21-bit packed address. */
#define MEM_ANIM21(w) (g_psx_arena_base + ((uint32_t)(w) & 0x1FFFFFu))
/* A frame word holding the address halved into its low 21 bits (Spyro's
 * frames). */
#define MEM_ANIM_HALF(w) (g_psx_arena_base + (((uint32_t)(w) << 11) >> 10))
/* An address the moby renderer kept shifted left by 8 (its face-save
 * list): the KUSEG alias puts the top byte back (mips2c_rt.h). */
#define MEM_ADDR24(a) MIPS_KUSEG((uint32_t)(a) | 0x80000000u)
/* A collision model pointer (Moby m_CollisionGroup, Model
 * m_CollisionModels[]) and its kind, the bit the WAD's offset carries. */
#define MEM_COLL_PTR(p) ((uintptr_t)(p) | 0x80000000u)
#define MEM_COLL_TAGGED(p) ((int32_t)(uintptr_t)(p) < 0)
/* A g_Models[] word: the model's address (a SimpleModel's has bit 31
 * cleared, which the KUSEG alias maps back), and whether it is one (a null
 * word counts as one, as the original's sign test does). */
#define MEM_MODEL_PTR(w) MIPS_KUSEG(w)
#define MEM_MODEL_SIMPLE(w) ((int32_t)(uintptr_t)(w) >= 0)
/* An address in the form g_Models[] holds a SimpleModel's (to compare
 * with one). */
#define MEM_SIMPLE_ADDR(a) ((uint32_t)(a) & 0x7FFFFFFFu)
/* OT tags the renderers build by XOR with an arena pointer, whose top byte
 * the tag constant cancels (mips2c_rt.h MIPS_ARENA_TAG, D25). */
#define MEM_TAG(k) MIPS_ARENA_TAG(k)
#define MEM_TAG_LINK(t, p) ((t) ^ (p))
#define MEM_TAG_LINK_AT(t, p, off) (((t) ^ (p)) + (off))
#define MEM_TAG_END(t) ((t) ^ MIPS_ARENA_TAG(0x80000000u))
/* The environment renderer's subdivision lists (env_hand.c): a sector's
 * header entry, then its polygons' addresses | flags. The asm tells them
 * apart by the sign: a header is the sector's address >> 2, a polygon's
 * address has bit 31 (the arena is above 2 GB). */
#define MEM_SUBDIV_HDR(sec) ((uint32_t)(sec) >> 2)
#define MEM_SUBDIV_IS_HDR(e) ((int32_t)(e) >= 0)
#define MEM_SUBDIV_SEC(e) MIPS_KUSEG((uint32_t)(e) << 2)
#endif

#endif
