/* The native SPU mix (docs/native-plan.md stage 5, decisions D84): the same
 * output as psx_spu_mix (libspu.c), sample for sample, by a cheaper route.
 *
 * - SPU RAM is decoded to PCM once, block by block, the first time a voice
 *   plays a block, into a cache of 28-sample blocks; SpuWrite only forgets
 *   the blocks it wrote (D88: decoding them there cost the game thread
 *   56 ms a sound bank on hardware). A block's samples depend on the ADPCM history it is
 *   entered with (unless its filter is 0), and that history depends on the
 *   path a voice took (key on resets it, a loop end jumps). So each cached
 *   block keeps the history it was decoded from, and a voice uses it only
 *   when its own history is the same; otherwise the block is decoded again
 *   (into a second way, kept for the next time). The PCM a voice reads is
 *   then always the decode voice_decode would have made: exact by
 *   construction, whatever the hit rate.
 * - The mix runs voice by voice over the vblank instead of sample by
 *   sample: each voice's blocks for the vblank are laid out in a row (the
 *   block flags followed by libspu.c's own psx_spu_voice_next_pos), then its
 *   735 samples are interpolated and added in one loop, NEON on ARM. Integer
 *   sums don't care about order, so the totals are psx_spu_mix's.
 * - The CD input comes from psx_xa_take, psx_xa_next's frames in a batch. */
#include "psx_spu.h"
#include "psx_xa.h"

#include <string.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define HAVE_NEON 1
#endif

#define BLOCK_SAMPLES 28
#define NBLK (PSX_SPU_RAM_SIZE / 16)
#define WAYS 2
#define N PSX_SPU_VBLANK_SAMPLES
/* A vblank at the highest pitch (4 samples a sample) reads 735 * 4 / 28 =
 * 105 blocks, plus the one it starts in and the one it ends in. */
#define MAX_BLOCKS (N * 4 / BLOCK_SAMPLES + 3)

enum { PCM_NONE, PCM_KEYED, PCM_ANY }; /* PCM_ANY: filter 0, no history */

static short s_pcm[WAYS][NBLK][BLOCK_SAMPLES];
static short s_key[WAYS][NBLK][2]; /* the history it was decoded from (older, old) */
static unsigned char s_state[WAYS][NBLK];

int g_psx_spu_native;
int g_psx_spu_simd = 1;
PsxSpuNativeStats g_psx_spu_native_stats;

static const int k_f0[5] = {0, 60, 115, 98, 122};
static const int k_f1[5] = {0, 0, -52, -55, -60};

/* voice_decode's arithmetic (libspu.c), from a given history. */
static int decode_block(const unsigned char *b, int h0, int h1, short out[BLOCK_SAMPLES]) {
  int shift = b[0] & 15, filter = (b[0] >> 4) & 7, i;
  if (shift > 12)
    shift = 9;
  if (filter > 4)
    filter = 4;
  for (i = 0; i < BLOCK_SAMPLES; i++) {
    int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
    int s = (short)(nib << 12) >> shift;
    s += (h1 * k_f0[filter] + h0 * k_f1[filter] + 32) >> 6;
    s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
    h0 = h1;
    h1 = (short)s;
    out[i] = (short)s;
  }
  return filter;
}

static void fill(int w, unsigned blk, int h0, int h1) {
  int filter = decode_block(g_psx_spu.ram + blk * 16, h0, h1, s_pcm[w][blk]);
  s_key[w][blk][0] = (short)h0;
  s_key[w][blk][1] = (short)h1;
  s_state[w][blk] = filter ? PCM_KEYED : PCM_ANY;
}

void psx_spu_pcm_reset(void) {
  memset(s_state, 0, sizeof s_state);
}

/* SPU RAM [addr, addr + size) was written: forget the PCM of every block
 * it touched, in both ways. A voice decodes each again the first time it
 * plays it, from its own history (pcm_block). */
void psx_spu_pcm_write(unsigned addr, unsigned size) {
  unsigned first, n, w;
  if (!g_psx_spu_native || !size)
    return;
  first = (addr & (PSX_SPU_RAM_SIZE - 1)) >> 4;
  n = (((addr & 15) + size + 15) >> 4);
  if (n > NBLK)
    n = NBLK;
  for (w = 0; w < WAYS; w++) {
    unsigned head = first + n <= NBLK ? n : NBLK - first; /* the rest wraps to block 0 */
    memset(&s_state[w][first], PCM_NONE, head);
    memset(&s_state[w][0], PCM_NONE, n - head);
  }
  g_psx_spu_native_stats.written += n;
}

/* The 28 samples of the block at addr as voice_decode makes them from
 * hist (older, old). */
static const short *pcm_block(unsigned addr, const short hist[2]) {
  unsigned blk = addr >> 4;
  int w;
  for (w = 0; w < WAYS; w++) {
    int st = s_state[w][blk];
    if (st == PCM_ANY ||
        (st == PCM_KEYED && s_key[w][blk][0] == hist[0] && s_key[w][blk][1] == hist[1])) {
      g_psx_spu_native_stats.hits[w]++;
      return s_pcm[w][blk];
    }
  }
  w = s_state[0][blk] == PCM_NONE ? 0 : WAYS - 1;
  fill(w, blk, hist[0], hist[1]);
  g_psx_spu_native_stats.misses++;
  return s_pcm[w][blk];
}

/* voice_decode for the mixer: cur_addr's block into dec, the history after it. */
static void fetch(PsxSpuVoice *v) {
  const short *p = pcm_block(v->cur_addr, v->hist);
  memcpy(v->dec, p, sizeof v->dec);
  v->hist[0] = p[BLOCK_SAMPLES - 2];
  v->hist[1] = p[BLOCK_SAMPLES - 1];
  v->dec_valid = 1;
}

static int vol_q15(short reg) { /* libspu.c's */
  int v = (reg & 0x8000) ? 0x3FFF : reg;
  if (v > 0x3FFF)
    v = 0x3FFF;
  return v * 2;
}

static short s_row[1 + MAX_BLOCKS * BLOCK_SAMPLES];
static int s_acc[2][N];

/* Samples [0, n) of one voice from its row, added to the sums. The row
 * holds the sample before the first block (prev) and then the blocks, so
 * position P (4.12, from the first block's start) interpolates between
 * row[P >> 12] and row[(P >> 12) + 1]: voice_sample's a and b. */
static void run_c(const short *row, unsigned p, unsigned step, int n, int env, int vl, int vr,
                  int *acc_l, int *acc_r) {
  int i;
  for (i = 0; i < n; i++, p += step) {
    int a = row[p >> 12], b = row[(p >> 12) + 1];
#ifdef SPU_NATIVE_MUTANT /* tests/spu: a test that can't fail this isn't one */
    int out = a + (((b - a) * (int)(p & 0xFFF) + 1) >> 12);
#else
    int out = a + (((b - a) * (int)(p & 0xFFF)) >> 12);
#endif
    int s = out * env >> 15;
    acc_l[i] += s * vl >> 15;
    acc_r[i] += s * vr >> 15;
  }
}

#ifdef HAVE_NEON
/* run_c, four samples at a time: the two taps are gathered lane by lane,
 * the arithmetic is the same in 32-bit lanes (every product fits: the
 * interpolation's 17 x 12 bits, the rest 16 x 16). */
static void run_neon(const short *row, unsigned p, unsigned step, int n, int env, int vl, int vr,
                     int *acc_l, int *acc_r) {
  int i = 0;
  const uint32_t lane[4] = {0, 1, 2, 3};
  uint32x4_t vp = vmlaq_n_u32(vdupq_n_u32(p), vld1q_u32(lane), step);
  const uint32x4_t step4 = vdupq_n_u32(step * 4), mask = vdupq_n_u32(0xFFF);
  const int16x4_t venv = vdup_n_s16((short)env), vvl = vdup_n_s16((short)vl),
                  vvr = vdup_n_s16((short)vr);
  for (; i + 4 <= n; i += 4) {
    unsigned p0 = p, p1 = p + step, p2 = p + step * 2, p3 = p + step * 3;
    int16x4_t a = vdup_n_s16(0), b = vdup_n_s16(0);
    int32x4_t frac, out;
    int16x4_t s;
    a = vld1_lane_s16(row + (p0 >> 12), a, 0);
    b = vld1_lane_s16(row + (p0 >> 12) + 1, b, 0);
    a = vld1_lane_s16(row + (p1 >> 12), a, 1);
    b = vld1_lane_s16(row + (p1 >> 12) + 1, b, 1);
    a = vld1_lane_s16(row + (p2 >> 12), a, 2);
    b = vld1_lane_s16(row + (p2 >> 12) + 1, b, 2);
    a = vld1_lane_s16(row + (p3 >> 12), a, 3);
    b = vld1_lane_s16(row + (p3 >> 12) + 1, b, 3);
    frac = vreinterpretq_s32_u32(vandq_u32(vp, mask));
    out = vaddq_s32(vmovl_s16(a), vshrq_n_s32(vmulq_s32(vsubl_s16(b, a), frac), 12));
    s = vmovn_s32(vshrq_n_s32(vmulq_s32(out, vmovl_s16(venv)), 15));
    vst1q_s32(acc_l + i, vaddq_s32(vld1q_s32(acc_l + i), vshrq_n_s32(vmull_s16(s, vvl), 15)));
    vst1q_s32(acc_r + i, vaddq_s32(vld1q_s32(acc_r + i), vshrq_n_s32(vmull_s16(s, vvr), 15)));
    vp = vaddq_u32(vp, step4);
    p += step * 4;
  }
  run_c(row, p, step, n - i, env, vl, vr, acc_l + i, acc_r + i);
}
#endif

/* One voice's vblank: what voice_sample does 735 times. */
static void mix_voice(PsxSpuVoice *v) {
  const unsigned step = v->pitch > 0x4000 ? 0x4000 : v->pitch, blk_len = BLOCK_SAMPLES << 12;
  const unsigned p0 = v->counter, end = p0 + step * N; /* the position after the last sample */
  int blocks = 1, n = N, env = v->envx;
  if (!v->dec_valid)
    fetch(v);
  s_row[0] = v->prev;
  memcpy(s_row + 1, v->dec, sizeof v->dec);
  /* voice_sample's block changes, in order: after the sample whose step
   * passes a block's end */
  while (end >= (unsigned)blocks * blk_len) {
    v->prev = v->dec[BLOCK_SAMPLES - 1];
    if (!psx_spu_voice_next_pos(v)) {
      /* the sample that stepped past the end is the voice's last, and it
       * is silent (envx is 0 by then): n - 1 sound */
      unsigned bound = (unsigned)blocks * blk_len - p0;
      n = (int)((bound + step - 1) / step) - 1;
      break;
    }
    fetch(v);
    memcpy(s_row + 1 + blocks * BLOCK_SAMPLES, v->dec, sizeof v->dec);
    blocks++;
  }
  if (n == N)
    v->counter = end - (unsigned)(blocks - 1) * blk_len;
  if (n <= 0 || !env)
    return;
#ifdef HAVE_NEON
  if (g_psx_spu_simd) {
    run_neon(s_row, p0, step, n, env, vol_q15(v->vol_l), vol_q15(v->vol_r), s_acc[0], s_acc[1]);
    return;
  }
#endif
  run_c(s_row, p0, step, n, env, vol_q15(v->vol_l), vol_q15(v->vol_r), s_acc[0], s_acc[1]);
}

void psx_spu_mix_native(PsxSpuVoice voice[PSX_SPU_VOICES], const PsxSpuMixRegs *reg,
                        short out[PSX_SPU_VBLANK_SAMPLES * 2]) {
  static short cd[N * 2];
  const int mvl = vol_q15(reg->mvol_l), mvr = vol_q15(reg->mvol_r);
  const int m0 = reg->cd_matrix[0], m1 = reg->cd_matrix[1], m2 = reg->cd_matrix[2],
            m3 = reg->cd_matrix[3], cvl = reg->cd_vol_l, cvr = reg->cd_vol_r;
  int i = 0, k;
  memset(s_acc, 0, sizeof s_acc);
  for (k = 0; k < PSX_SPU_VOICES; k++) {
    PsxSpuVoice *v = &voice[k];
    if (v->env_phase != PSX_ENV_OFF && v->env_phase != PSX_ENV_RELEASE)
      mix_voice(v);
  }
  psx_xa_take(cd, N);
#ifdef HAVE_NEON
  if (g_psx_spu_simd) {
    const int32x4_t vmvl = vdupq_n_s32(mvl), vmvr = vdupq_n_s32(mvr);
    const int32x4_t v0 = vdupq_n_s32(m0), v1 = vdupq_n_s32(m1), v2 = vdupq_n_s32(m2),
                    v3 = vdupq_n_s32(m3), vcl = vdupq_n_s32(cvl), vcr = vdupq_n_s32(cvr);
    for (; i + 4 <= N; i += 4) {
      int16x4x2_t c = vld2_s16(cd + i * 2), o;
      int32x4_t l = vshrq_n_s32(vmulq_s32(vld1q_s32(s_acc[0] + i), vmvl), 15);
      int32x4_t r = vshrq_n_s32(vmulq_s32(vld1q_s32(s_acc[1] + i), vmvr), 15);
      if (reg->cd_on) {
        int32x4_t cl = vmovl_s16(c.val[0]), cr = vmovl_s16(c.val[1]);
        int32x4_t ml = vshrq_n_s32(vaddq_s32(vmulq_s32(cl, v0), vmulq_s32(cr, v3)), 7);
        int32x4_t mr = vshrq_n_s32(vaddq_s32(vmulq_s32(cl, v1), vmulq_s32(cr, v2)), 7);
        l = vaddq_s32(l, vshrq_n_s32(vmulq_s32(ml, vcl), 15));
        r = vaddq_s32(r, vshrq_n_s32(vmulq_s32(mr, vcr), 15));
      }
      o.val[0] = vqmovn_s32(l);
      o.val[1] = vqmovn_s32(r);
      vst2_s16(out + i * 2, o);
    }
  }
#endif
  for (; i < N; i++) { /* psx_spu_mix's tail of the sample loop */
    int l = s_acc[0][i] * mvl >> 15, r = s_acc[1][i] * mvr >> 15;
    if (reg->cd_on) {
      int cl = cd[i * 2], cr = cd[i * 2 + 1];
      int ml = (cl * m0 + cr * m3) >> 7;
      int mr = (cl * m1 + cr * m2) >> 7;
      l += ml * cvl >> 15;
      r += mr * cvr >> 15;
    }
    out[i * 2] = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
    out[i * 2 + 1] = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
  }
}
