/* One straight-line function per GTE opcode the translated code issues
 * (gte_inline.h, fps plan C5, decisions D72). */
#include "gte_inline.h"

#define M2C_GTE_DEFINE(op) void m2c_gte_##op(void) { m2c_gte_op(op); }
M2C_GTE_OPS(M2C_GTE_DEFINE)
