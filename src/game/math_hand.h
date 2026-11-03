/* What math_hand.c shares with the other hand-written C (decisions D87). */
#ifndef MATH_HAND_H
#define MATH_HAND_H

#include "matrix.h"

/* RotVec8ToMatrix's body: pIn (or the identity, for NULL) turned by the
 * three 8-bit angles in angles, to pOut and the GTE's rotation (and a zero
 * translation); loadLast 0 leaves the last turn out of the GTE. */
void RotateByAngles8(u_int angles, const MATRIX *pIn, MATRIX *pOut, int loadLast);

#endif
