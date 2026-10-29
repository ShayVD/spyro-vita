/* Decomp <libgpu.h> -> PsyCross's, plus the PsyQ names PsyCross renamed. */
#ifndef SPYRO_COMPAT_LIBGPU_H
#define SPYRO_COMPAT_LIBGPU_H
#include_next <libgpu.h>
typedef RECT16 RECT; /* PsyCross calls PsyQ's RECT RECT16 (Windows clash) */
#endif
