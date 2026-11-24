/* One straight-line function per GTE opcode the translated code issues
 * (gte_inline.h, fps plan C5, decisions D72). */
#include "gte_native.h"

#include <stddef.h>

#define M2C_GTE_DEFINE(op) void m2c_gte_##op(void) { m2c_gte_op(op); }
M2C_GTE_OPS(M2C_GTE_DEFINE)

/* The vertex cache's hooks on a projection (gtereg.h, patches 0007 and 0011;
 * decisions D39, D89): ours since stage 8c (D116), PsyCross's GTE_operator
 * in the reference library calls the first. gte_native.h's RTPS and RTPT
 * call the cache by name since #205 (vcache.c ngte_vcache, with the
 * registers by pointer and the GTE's own sums, where this file copied the
 * 15 registers for the value hook); the value hook being set still says
 * whether the cache records, and the sky's records still come through it
 * (sky_hand.c). */
void (*g_psyxRtpHook)(int idx, int sf) = NULL;
void (*g_psyxRtpValueHook)(const int *proj, int vx, int vy, int vz, unsigned int sxy, int sz3) = NULL;
