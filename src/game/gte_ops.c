/* One straight-line function per GTE opcode the translated code issues
 * (gte_inline.h, fps plan C5, decisions D72). */
#include "gte_native.h"

#define M2C_GTE_DEFINE(op) void m2c_gte_##op(void) { m2c_gte_op(op); }
M2C_GTE_OPS(M2C_GTE_DEFINE)

/* gte_native.h's RTPS to the vertex cache: the registers it read, as
 * g_psyxRtpValueHook takes them (D89). */
void ngte_vcache(const NgteProj *p, unsigned int vxy, unsigned int vz, const NgteRtp *o) {
  const int proj[15] = {p->r11, p->r12, p->r13, p->r21, p->r22, p->r23, p->r31, p->r32, p->r33,
                        p->trx, p->try_, p->trz, p->ofx, p->ofy, p->h};
  g_psyxRtpValueHook(proj, ngte_vx(vxy), ngte_vy(vxy), ngte_vz(vz), o->sxy, (int)o->sz3);
}
