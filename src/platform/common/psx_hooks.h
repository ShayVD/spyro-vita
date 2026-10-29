/* Calls between the platform layer (src/platform/<host>) and the shared PS1
 * hardware models in src/platform/common. */
#ifndef PSX_HOOKS_H
#define PSX_HOOKS_H

#ifdef __cplusplus
extern "C" {
#endif

/* libcd over files (src/platform/common/libcd.c). */
int psx_cd_open(const char *disc_dir); /* reads <disc_dir>/manifest.json; 0 = ok */
void psx_cd_vblank(void);              /* once per virtual vblank, before the VSync callback */

/* SPU (src/platform/common/libspu.c). */
void psx_spu_vblank(void); /* once per virtual vblank */

#ifdef __cplusplus
}
#endif

#endif
