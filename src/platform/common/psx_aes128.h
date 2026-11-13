/* AES-128 for the PS1 Classics EBOOT's PGD header (pbp.c, decisions D157):
 * tiny-AES-c (third_party/tiny-AES-c, the Unlicense; the owner's fork of
 * kokke's), one block at a time, with CBC over a buffer done here. The
 * configuration macros below must match the ones aes.c is compiled with
 * (cmake/platform_common.cmake). */
#ifndef PSX_AES128_H
#define PSX_AES128_H

#ifndef AES128
#define AES128 1
#endif
#ifndef AES_ENABLE_ECB
#define AES_ENABLE_ECB 1
#endif
#ifndef AES_ENABLE_CBC
#define AES_ENABLE_CBC 0
#endif
#ifndef AES_ENABLE_CTR
#define AES_ENABLE_CTR 0
#endif
#ifndef AES_SBOX_MODE
#define AES_SBOX_MODE 3 /* AES_SBOX_MODE_FAST: a table, not constant time; nothing secret here */
#endif
#include "../../../third_party/tiny-AES-c/aes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  struct AES_ctx ctx;
} PsxAes128;

void psx_aes128_init(PsxAes128 *a, const unsigned char key[16]);
void psx_aes128_encrypt(const PsxAes128 *a, const unsigned char in[16], unsigned char out[16]);
void psx_aes128_decrypt(const PsxAes128 *a, const unsigned char in[16], unsigned char out[16]);
/* CBC over n bytes (a multiple of 16); in and out may be the same buffer. iv
 * is updated to the last ciphertext block, so a buffer can be done in
 * pieces. */
void psx_aes128_cbc_encrypt(const PsxAes128 *a, unsigned char iv[16], const unsigned char *in,
                            unsigned char *out, unsigned n);
void psx_aes128_cbc_decrypt(const PsxAes128 *a, unsigned char iv[16], const unsigned char *in,
                            unsigned char *out, unsigned n);

#ifdef __cplusplus
}
#endif

#endif
