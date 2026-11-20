/* linux64 (the low-image model, D174): the overlays' function pointers
 * (the decomp's overlay_pointers.h) as normal 8-byte globals. On the PS1
 * and the 32-bit hosts they are 4-byte data labels in the image of PS1 RAM
 * (tools/data2s.py); a call through a __ptr32 function pointer miscompiles
 * (clang 19 loads 8 bytes, or crashes), so on linux64 the slots are renamed
 * p32slot_NAME (cmake/game.cmake --host-label) and left unused, and the
 * names are these. Nothing reads the slots by address (checked for D174:
 * no hand C, presenter snapshot or save-state entry names them).
 * Save states don't hold them; SetOverlayPointers sets them at each level
 * load. */

#if SPYRO_P32
#include <assert.h> /* PsyCross headers use C11 static_assert */
#include "overlay_pointers.h"

Moby *(*g_SpawnMoby)(int pClass, Moby *pParent);
void (*g_UpdateMoby)();
void (*g_FlightResultsUpdate)();
void (*D_8007567C)();
void (*g_SpawnParticle)(int pAmount, int pClass, void *spawnParam, int extraParam);
void (*g_UpdateParticle)(int);
void (*D_800757A0)(Moby *pBalloonist);
void (*D_800758A8)();
void (*D_8007574C)();
void (*D_800758D8)();
void (*D_80075694)();
void (*D_800757A8)(int pUnk);
int (*D_800758C4)(int pTimer, Vector3D *pPos, int pColor);
#endif
