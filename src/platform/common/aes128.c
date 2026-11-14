/* psx_aes128.h over tiny-AES-c's ECB block functions (kokke's upstream,
 * third_party/tiny-AES-c). */
#include "psx_aes128.h"

#include <string.h>

/* tiny-AES-c's switches, the ones aes.c is compiled with
 * (cmake/platform_common.cmake): the block functions only, so its context
 * is the expanded key alone. Its header fixes AES128 itself. */
#ifndef ECB
#define ECB 1
#endif
#ifndef CBC
#define CBC 0
#endif
#ifndef CTR
#define CTR 0
#endif
#include "../../../third_party/tiny-AES-c/aes.h"

_Static_assert(sizeof(struct AES_ctx) == sizeof(PsxAes128) && AES_keyExpSize == 176,
               "PsxAes128 is tiny-AES-c's AES-128 context without CBC / CTR state");

/* PsxAes128 is that context's bytes */
#define CTX(a) ((struct AES_ctx *)(void *)(a)->round_key)
#define CCTX(a) ((const struct AES_ctx *)(const void *)(a)->round_key)

void psx_aes128_init(PsxAes128 *a, const unsigned char key[16]) { AES_init_ctx(CTX(a), key); }

void psx_aes128_encrypt(const PsxAes128 *a, const unsigned char in[16], unsigned char out[16]) {
  if (out != in)
    memcpy(out, in, 16);
  AES_ECB_encrypt(CCTX(a), out);
}

void psx_aes128_decrypt(const PsxAes128 *a, const unsigned char in[16], unsigned char out[16]) {
  if (out != in)
    memcpy(out, in, 16);
  AES_ECB_decrypt(CCTX(a), out);
}

void psx_aes128_cbc_encrypt(const PsxAes128 *a, unsigned char iv[16], const unsigned char *in,
                            unsigned char *out, unsigned n) {
  unsigned i, j;
  for (i = 0; i + 16 <= n; i += 16) {
    for (j = 0; j < 16; j++)
      out[i + j] = (unsigned char)(in[i + j] ^ iv[j]);
    AES_ECB_encrypt(CCTX(a), out + i);
    memcpy(iv, out + i, 16);
  }
}

void psx_aes128_cbc_decrypt(const PsxAes128 *a, unsigned char iv[16], const unsigned char *in,
                            unsigned char *out, unsigned n) {
  unsigned i, j;
  for (i = 0; i + 16 <= n; i += 16) {
    unsigned char c[16];
    memcpy(c, in + i, 16); /* in may be out */
    memcpy(out + i, c, 16);
    AES_ECB_decrypt(CCTX(a), out + i);
    for (j = 0; j < 16; j++)
      out[i + j] ^= iv[j];
    memcpy(iv, c, 16);
  }
}
