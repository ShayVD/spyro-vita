/* AES-128 for the PS1 Classics EBOOT's PGD header (pbp.c, decisions D157):
 * kokke's tiny-AES-c (third_party/tiny-AES-c, upstream, the Unlicense), one
 * block at a time, with CBC over a buffer done here. aes128.c is the only
 * file that includes tiny-AES-c's header, so its ECB / CBC / CTR switches
 * stay out of everything else. */
#ifndef PSX_AES128_H
#define PSX_AES128_H

#ifdef __cplusplus
extern "C" {
#endif

/* The expanded key: tiny-AES-c's struct AES_ctx for AES-128 with no CBC or
 * CTR state (aes128.c checks the size). */
typedef struct {
  unsigned char round_key[176];
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
