/* The particle slots (asm/particle_alloc.s): allocation and freeing,
 * written by hand from the asm so it reads as C (docs/native-plan.md
 * stage 3, decisions D94). Replaces the mips2c unit (cmake/mips2c.cmake
 * M2C_HAND_MULTI); checked against the unit with the shadows of
 * hand_shadow.h and against PCSX-Redux with the oracle replay
 * (docs/translation-log.md). The asm uses neither the scratchpad nor the
 * GTE.
 *
 * M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) takes that function from the mips2c
 * unit instead, linked as <name>_gen, for A/B tests. */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "common.h"

#include "hand_shadow.h"
#include "rand.h"
#include "renderers.h"

#include <stddef.h>
#include <stdint.h>

static_assert(sizeof(Particle) == 0x20 && offsetof(Particle, m_RenderType) == 1, "Particle");

/* The mips2c unit's entries, linked as <name>_gen for A/B tests and the
 * shadows (cmake M2C_HAND_MULTI). */
u_int func_80053570_gen(), func_80053608_gen();

/* g_Particles holds 256 slots. A slot's render type byte, read signed, is
 * its state: 0 or more, in use; -1, free, and so is every slot after it
 * (the end of the used ones); -2, a free slot among used ones.
 * g_ParticleAllocPtr is the first free slot. */
#define PARTICLE_END -1
#define PARTICLE_HOLE -2
#define PARTICLE_SLOTS 0x100

static inline signed char *State(Particle *p) { return (signed char *)&p->m_RenderType; }

#define PARTICLE_FUZZ (HAND_SHADOW && !M2C_func_80053570 && !M2C_func_80053608)
#if PARTICLE_FUZZ
static Particle *Alloc(int type);
static void Free(Particle *p);

/* Fuzzing (SPYRO_HAND_FUZZ=N, hand_shadow.h): the slots' states made up
 * at random before each variant: up to a random end, slots in use and
 * holes; after it, -1 or junk; g_ParticleAllocPtr at the first free slot
 * (the end of the array when there's none, a quarter of the time). Then
 * the call: an allocation, or the freeing of a random slot in use. */
static Particle *FuzzSlots(void) {
  Particle *start = g_Particles, *in_use[PARTICLE_SLOTS];
  int end = HandFuzzRand() & 3 ? (int)(HandFuzzRand() % (PARTICLE_SLOTS + 1)) : PARTICLE_SLOTS, i, n = 0;
  int holes = HandFuzzRand() & 1;
  g_ParticleAllocPtr = start + end;
  for (i = 0; i < PARTICLE_SLOTS; i++) {
    signed char *st = State(&start[i]);
    if (i < end) {
      *st = holes && !(HandFuzzRand() % 5) ? PARTICLE_HOLE : (signed char)(HandFuzzRand() % 6);
      if (*st == PARTICLE_HOLE && g_ParticleAllocPtr > &start[i])
        g_ParticleAllocPtr = &start[i];
      if (*st >= 0)
        in_use[n++] = &start[i];
    } else
      *st = i == end || HandFuzzRand() & 1 ? PARTICLE_END : (signed char)HandFuzzRand();
  }
  return n ? in_use[HandFuzzRand() % n] : NULL;
}

static void FuzzParticles(u_int seed, int type) {
  int n = HandFuzzBegin("func_80053570", seed), k;
  for (k = 0; k < n; k++) {
    Particle *p = FuzzSlots();
    if (k & 1 && p)
      HAND_FUZZ_VOID(func_80053608, func_80053608_gen(p), Free(p));
    else
      HAND_FUZZ_ONE(func_80053570, func_80053570_gen(type), Alloc(type));
  }
}
#endif

#if M2C_func_80053570
Particle *func_80053570(int pRenderType) { return (Particle *)func_80053570_gen(pRenderType); }
#else
static Particle *Alloc(int type) {
  Particle *end = g_Particles + PARTICLE_SLOTS, *p = g_ParticleAllocPtr, *next;
  int was;
  if (p == end) {
    /* Full: one of the last 256 slots, at random, is taken over. */
    p = end - ((rand() & 0xFF) + 1);
    p->m_RenderType = type;
    return p;
  }
  was = *State(p);
  p->m_RenderType = type;
  next = p + 1;
  if (was == PARTICLE_END) {
    /* The end of the used slots moves on by one. */
    if (next != end)
      *State(next) = PARTICLE_END;
  } else {
    /* A hole was taken: the next free slot is the next hole, or the end. */
    while (next != end && *State(next) >= 0)
      next++;
  }
  g_ParticleAllocPtr = next;
  return p;
}

/* Takes a free particle slot and gives it pRenderType. */
Particle *func_80053570(int pRenderType) {
#if PARTICLE_FUZZ
  FuzzParticles(pRenderType ^ (u_int)(uintptr_t)g_ParticleAllocPtr, pRenderType);
#endif
  HAND_SHADOW_CALL(func_80053570, func_80053570_gen(pRenderType), Alloc(pRenderType));
  return Alloc(pRenderType);
}
#endif

#if M2C_func_80053608
void func_80053608(Particle *particle) { func_80053608_gen(particle); }
#else
static void Free(Particle *p) {
  Particle *start = g_Particles, *end = start + PARTICLE_SLOTS, *last;
  if (p + 1 != end && *State(p + 1) != PARTICLE_END) {
    /* A slot among used ones: a hole. */
    *State(p) = PARTICLE_HOLE;
    if (g_ParticleAllocPtr >= p)
      g_ParticleAllocPtr = p;
    return;
  }
  /* The last used slot: the end moves back past the holes before it, to
   * just after the last slot still in use (or to the first slot). */
  last = p - 1;
  if (p != start)
    while (*State(last) < 0 && last-- != start)
      ;
  last++;
  if (g_ParticleAllocPtr >= last)
    g_ParticleAllocPtr = last;
  *State(last) = PARTICLE_END;
}

/* Frees a particle slot. */
void func_80053608(Particle *particle) {
#if PARTICLE_FUZZ
  FuzzParticles((u_int)(uintptr_t)particle, particle->m_RenderType);
#endif
  HAND_SHADOW_VOID(func_80053608, func_80053608_gen(particle), Free(particle));
  Free(particle);
}
#endif
