/* psx_aes128.h over tiny-AES-c's ECB block functions. */
#include "psx_aes128.h"

#include <string.h>

void psx_aes128_init(PsxAes128 *a, const unsigned char key[16]) { AES_init_ctx(&a->ctx, key); }

void psx_aes128_encrypt(const PsxAes128 *a, const unsigned char in[16], unsigned char out[16]) {
  if (out != in)
    memcpy(out, in, 16);
  AES_ECB_encrypt(&a->ctx, out);
}

void psx_aes128_decrypt(const PsxAes128 *a, const unsigned char in[16], unsigned char out[16]) {
  if (out != in)
    memcpy(out, in, 16);
  AES_ECB_decrypt(&a->ctx, out);
}

void psx_aes128_cbc_encrypt(const PsxAes128 *a, unsigned char iv[16], const unsigned char *in,
                            unsigned char *out, unsigned n) {
  unsigned i, j;
  for (i = 0; i + 16 <= n; i += 16) {
    for (j = 0; j < 16; j++)
      out[i + j] = (unsigned char)(in[i + j] ^ iv[j]);
    AES_ECB_encrypt(&a->ctx, out + i);
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
    AES_ECB_decrypt(&a->ctx, out + i);
    for (j = 0; j < 16; j++)
      out[i + j] ^= iv[j];
    memcpy(iv, c, 16);
  }
}
