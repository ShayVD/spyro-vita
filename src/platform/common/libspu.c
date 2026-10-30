/* PsyQ libspu over a software SPU (docs/porting-plan.md §6): register state,
 * the SPU RAM image, voice lifetime and a mixer (ADPCM, pitch, volumes, CD
 * input from xa.c) that outputs through psx_audio_out(). Spyro's ADSR is fixed (ar dr
 * sr rr 0, sl 15; spu.c:39-56), which on hardware is a gate: full level within
 * a few samples of key on, silent within a few of key off. So a voice's
 * envelope is 0x7FFF from key on until key off or until its sample hits an
 * end flag without repeat, which is what SpuGetKeyStatus reports from. */
#include "psx_hooks.h"
#include "psx_cd.h"
#include "psx_spu.h"
#include "psx_xa.h"

#include <libspu.h>

#include <string.h>

#define BLOCK_SAMPLES 28
#define ALL_VOICES 0xFFFFFFu

PsxSpu g_psx_spu;

/* Presentation passes (src/game/present.c) re-run the game's draw for an
 * extra picture; any sound it starts belongs to the real draw only. */
int g_psx_spu_frozen;
long long g_psx_spu_dropped;
#define PASS_GUARD(...)                                                        \
  do {                                                                         \
    if (g_psx_spu_frozen) {                                                    \
      g_psx_spu_dropped++;                                                     \
      return __VA_ARGS__;                                                      \
    }                                                                          \
  } while (0)

static unsigned int ram_addr(unsigned int a) { return a & (PSX_SPU_RAM_SIZE - 1); }

static void voice_block_start(PsxSpuVoice *v) {
  if (g_psx_spu.ram[v->cur_addr + 1] & 4) /* loop start */
    v->loop_addr = v->cur_addr;
}

static const int k_f0[5] = {0, 60, 115, 98, 122};
static const int k_f1[5] = {0, 0, -52, -55, -60};

/* Decode the 16-byte SPU ADPCM block at cur_addr (psx-spx "SPU ADPCM"). */
static void voice_decode(PsxSpuVoice *v) {
  const unsigned char *b = g_psx_spu.ram + v->cur_addr;
  int shift = b[0] & 15, filter = (b[0] >> 4) & 7, i;
  if (shift > 12)
    shift = 9; /* hardware quirk */
  if (filter > 4)
    filter = 4;
  for (i = 0; i < BLOCK_SAMPLES; i++) {
    int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
    int s = (short)(nib << 12) >> shift;
    s += (v->hist[1] * k_f0[filter] + v->hist[0] * k_f1[filter] + 32) >> 6;
    s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
    v->hist[0] = v->hist[1];
    v->hist[1] = (short)s;
    v->dec[i] = (short)s;
  }
  v->dec_valid = 1;
}

/* Past the end of cur_addr's block: follow its flags (loop end jumps to the
 * loop address; without repeat the voice goes silent). Returns 0 if it did. */
static int voice_next_block(PsxSpuVoice *v) {
  unsigned char flags = g_psx_spu.ram[v->cur_addr + 1];
  if (v->dec_valid)
    v->prev = v->dec[BLOCK_SAMPLES - 1];
  if (flags & 1) {
    v->cur_addr = v->loop_addr & ~15u;
    if (!(flags & 2)) {
      v->env_phase = PSX_ENV_OFF;
      v->envx = 0;
      v->counter = 0;
      return 0;
    }
  } else {
    v->cur_addr = ram_addr(v->cur_addr + 16);
  }
  voice_block_start(v);
  v->dec_valid = 0;
  return 1;
}

void psx_spu_voice_advance(PsxSpuVoice *v, unsigned int samples) {
  unsigned int step = v->pitch > 0x4000 ? 0x4000 : v->pitch;
  unsigned int total;
  if (v->env_phase == PSX_ENV_OFF || v->env_phase == PSX_ENV_RELEASE)
    return;
  total = v->counter + step * samples;
  while (total >= BLOCK_SAMPLES << 12) {
    total -= BLOCK_SAMPLES << 12;
    if (!v->dec_valid)
      voice_decode(v); /* keep the decoder history right */
    if (!voice_next_block(v))
      return;
  }
  v->counter = total;
}

/* Volume registers in fixed mode: -0x4000..0x3FFF means -1.0..+1.0. Sweep
 * modes (bit 15) aren't used by Spyro and play at the base level. */
static int vol_q15(short reg) {
  int v = (reg & 0x8000) ? 0x3FFF : reg;
  if (v > 0x3FFF)
    v = 0x3FFF;
  return v * 2;
}

/* One voice's next output sample (linear interpolation between ADPCM
 * samples; the hardware uses a 4-tap Gaussian, see translation-log). */
static int voice_sample(PsxSpuVoice *v) {
  unsigned int step = v->pitch > 0x4000 ? 0x4000 : v->pitch;
  int i, a, b, frac, out;
  if (!v->dec_valid)
    voice_decode(v);
  i = (int)(v->counter >> 12);
  frac = (int)(v->counter & 0xFFF);
  a = i == 0 ? v->prev : v->dec[i - 1];
  b = v->dec[i];
  out = a + (((b - a) * frac) >> 12);
  v->counter += step;
  while (v->counter >= BLOCK_SAMPLES << 12) {
    v->counter -= BLOCK_SAMPLES << 12;
    if (!voice_next_block(v))
      break;
    voice_decode(v);
  }
  return out * v->envx >> 15;
}

#define VBLANK_SAMPLES (PSX_SPU_RATE / PSX_SPU_VBLANK_HZ)

void psx_spu_vblank(void) {
  static short out[VBLANK_SAMPLES * 2];
  const PsxCdStream *cd = psx_cd_stream();
  int n, i;
  for (n = 0; n < VBLANK_SAMPLES; n++) {
    int l = 0, r = 0, cl, cr;
    for (i = 0; i < PSX_SPU_VOICES; i++) {
      PsxSpuVoice *v = &g_psx_spu.voice[i];
      int s;
      if (v->env_phase == PSX_ENV_OFF || v->env_phase == PSX_ENV_RELEASE)
        continue;
      s = voice_sample(v);
      l += s * vol_q15(v->vol_l) >> 15;
      r += s * vol_q15(v->vol_r) >> 15;
    }
    l = l * vol_q15(g_psx_spu.mvol_l) >> 15;
    r = r * vol_q15(g_psx_spu.mvol_r) >> 15;
    psx_xa_next(&cl, &cr);
    if (g_psx_spu.cd_mix && !cd->muted) {
      /* CdMix routes the drive's channels (0x80 = 1.0): PsyQ's CdMix writes
       * val0..3 to the L->L, L->R, R->R, R->L registers (asm/psyq.s
       * func_80065108; psx-spx CDROM "Audio Volume"), not in the order its
       * header comments say. Then the SPU's CD input volume (0x7FFF = 1.0). */
      int ml = (cl * cd->mix[0] + cr * cd->mix[3]) >> 7;
      int mr = (cl * cd->mix[1] + cr * cd->mix[2]) >> 7;
      l += ml * g_psx_spu.cd_vol_l >> 15;
      r += mr * g_psx_spu.cd_vol_r >> 15;
    }
    out[n * 2] = (short)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
    out[n * 2 + 1] = (short)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
  }
  psx_audio_out(out, VBLANK_SAMPLES);
}

void SpuInit(void) {
  memset(&g_psx_spu, 0, sizeof g_psx_spu);
  g_psx_spu.cd_mix = 1;
}

void SpuQuit(void) {}

static void voice_set(PsxSpuVoice *v, const SpuVoiceAttr *a) {
  unsigned int m = a->mask;
  if (m == 0)
    m = 0xFFFFFFFFu; /* PsyQ: mask 0 sets everything */
  if (m & SPU_VOICE_VOLL)
    v->vol_l = a->volume.left;
  if (m & SPU_VOICE_VOLR)
    v->vol_r = a->volume.right;
  if (m & SPU_VOICE_VOLMODEL)
    v->volmode_l = a->volmode.left;
  if (m & SPU_VOICE_VOLMODER)
    v->volmode_r = a->volmode.right;
  if (m & SPU_VOICE_PITCH)
    v->pitch = a->pitch;
  if (m & SPU_VOICE_WDSA)
    v->start_addr = ram_addr(a->addr) & ~7u;
  if (m & SPU_VOICE_LSAX)
    v->loop_addr = ram_addr(a->loop_addr) & ~7u;
  if (m & SPU_VOICE_ADSR_AMODE)
    v->a_mode = a->a_mode;
  if (m & SPU_VOICE_ADSR_SMODE)
    v->s_mode = a->s_mode;
  if (m & SPU_VOICE_ADSR_RMODE)
    v->r_mode = a->r_mode;
  if (m & SPU_VOICE_ADSR_AR)
    v->ar = a->ar;
  if (m & SPU_VOICE_ADSR_DR)
    v->dr = a->dr;
  if (m & SPU_VOICE_ADSR_SR)
    v->sr = a->sr;
  if (m & SPU_VOICE_ADSR_RR)
    v->rr = a->rr;
  if (m & SPU_VOICE_ADSR_SL)
    v->sl = a->sl;
}

void SpuSetVoiceAttr(SpuVoiceAttr *arg) {
  PASS_GUARD();
  for (int i = 0; i < PSX_SPU_VOICES; i++)
    if (arg->voice & (1u << i))
      voice_set(&g_psx_spu.voice[i], arg);
}

static void voice_key_on(PsxSpuVoice *v) {
  v->key_on = 1;
  v->env_phase = PSX_ENV_SUSTAIN; /* ar 0 reaches full level in ~3 samples */
  v->envx = 0x7FFF;
  v->cur_addr = v->start_addr & ~15u;
  v->counter = 0;
  v->hist[0] = v->hist[1] = 0;
  v->prev = 0;
  v->dec_valid = 0;
  v->keyon_seq++;
  voice_block_start(v);
}

static void voice_key_off(PsxSpuVoice *v) {
  v->key_on = 0;
  v->env_phase = PSX_ENV_OFF; /* rr 0 releases in ~2 samples */
  v->envx = 0;
}

void SpuSetKey(int on_off, unsigned int voice_bit) {
  PASS_GUARD();
  for (int i = 0; i < PSX_SPU_VOICES; i++) {
    if (!(voice_bit & (1u << i)))
      continue;
    if (on_off == SPU_ON)
      voice_key_on(&g_psx_spu.voice[i]);
    else if (on_off == SPU_OFF)
      voice_key_off(&g_psx_spu.voice[i]);
  }
}

static int key_status(const PsxSpuVoice *v) {
  if (v->key_on)
    return v->envx ? SPU_ON : SPU_ON_ENV_OFF;
  return v->envx ? SPU_OFF_ENV_ON : SPU_OFF;
}

/* Status of the lowest voice in voice_bit, -1 if none (as PsyQ does). */
int SpuGetKeyStatus(unsigned int voice_bit) {
  for (int i = 0; i < PSX_SPU_VOICES; i++)
    if (voice_bit & (1u << i))
      return key_status(&g_psx_spu.voice[i]);
  return -1;
}

void SpuGetAllKeysStatus(char *status) {
  for (int i = 0; i < PSX_SPU_VOICES; i++)
    status[i] = (char)key_status(&g_psx_spu.voice[i]);
}

void SpuSetVoiceVolume(int vNum, short volL, short volR) {
  PASS_GUARD();
  if (vNum < 0 || vNum >= PSX_SPU_VOICES)
    return;
  g_psx_spu.voice[vNum].vol_l = volL;
  g_psx_spu.voice[vNum].vol_r = volR;
}

void SpuGetVoiceVolume(int vNum, short *volL, short *volR) {
  if (vNum < 0 || vNum >= PSX_SPU_VOICES)
    return;
  *volL = g_psx_spu.voice[vNum].vol_l;
  *volR = g_psx_spu.voice[vNum].vol_r;
}

void SpuSetVoicePitch(int vNum, unsigned short pitch) {
  PASS_GUARD();
  if (vNum >= 0 && vNum < PSX_SPU_VOICES)
    g_psx_spu.voice[vNum].pitch = pitch;
}

void SpuGetVoicePitch(int vNum, unsigned short *pitch) {
  if (vNum >= 0 && vNum < PSX_SPU_VOICES)
    *pitch = g_psx_spu.voice[vNum].pitch;
}

void SpuGetVoiceEnvelope(int vNum, short *envx) {
  if (vNum >= 0 && vNum < PSX_SPU_VOICES)
    *envx = g_psx_spu.voice[vNum].envx;
}

void SpuSetCommonAttr(SpuCommonAttr *attr) {
  PASS_GUARD();
  unsigned int m = attr->mask;
  if (m == 0)
    m = 0xFFFFFFFFu;
  if (m & SPU_COMMON_MVOLL)
    g_psx_spu.mvol_l = attr->mvol.left;
  if (m & SPU_COMMON_MVOLR)
    g_psx_spu.mvol_r = attr->mvol.right;
  if (m & SPU_COMMON_MVOLMODEL)
    g_psx_spu.mvolmode_l = attr->mvolmode.left;
  if (m & SPU_COMMON_MVOLMODER)
    g_psx_spu.mvolmode_r = attr->mvolmode.right;
  if (m & SPU_COMMON_CDVOLL)
    g_psx_spu.cd_vol_l = attr->cd.volume.left;
  if (m & SPU_COMMON_CDVOLR)
    g_psx_spu.cd_vol_r = attr->cd.volume.right;
  if (m & SPU_COMMON_CDREV)
    g_psx_spu.cd_reverb = attr->cd.reverb;
  if (m & SPU_COMMON_CDMIX)
    g_psx_spu.cd_mix = attr->cd.mix;
  if (m & SPU_COMMON_EXTVOLL)
    g_psx_spu.ext_vol_l = attr->ext.volume.left;
  if (m & SPU_COMMON_EXTVOLR)
    g_psx_spu.ext_vol_r = attr->ext.volume.right;
  if (m & SPU_COMMON_EXTREV)
    g_psx_spu.ext_reverb = attr->ext.reverb;
  if (m & SPU_COMMON_EXTMIX)
    g_psx_spu.ext_mix = attr->ext.mix;
}

void SpuGetCommonAttr(SpuCommonAttr *attr) {
  attr->mvol.left = g_psx_spu.mvol_l;
  attr->mvol.right = g_psx_spu.mvol_r;
  attr->mvolmode.left = g_psx_spu.mvolmode_l;
  attr->mvolmode.right = g_psx_spu.mvolmode_r;
  attr->mvolx.left = g_psx_spu.mvol_l;
  attr->mvolx.right = g_psx_spu.mvol_r;
  attr->cd.volume.left = g_psx_spu.cd_vol_l;
  attr->cd.volume.right = g_psx_spu.cd_vol_r;
  attr->cd.reverb = g_psx_spu.cd_reverb;
  attr->cd.mix = g_psx_spu.cd_mix;
  attr->ext.volume.left = g_psx_spu.ext_vol_l;
  attr->ext.volume.right = g_psx_spu.ext_vol_r;
  attr->ext.reverb = g_psx_spu.ext_reverb;
  attr->ext.mix = g_psx_spu.ext_mix;
}

/* Transfers are immediate; the address wraps at 512 KB like the hardware's. */
unsigned int SpuSetTransferStartAddr(unsigned int addr) {
  if (addr - 0x1010u > 0x7EFE8u) /* PsyQ accepts 0x1010..0x7FFF8 */
    return 0;
  g_psx_spu.transfer_addr = (addr + 7) & ~7u;
  return g_psx_spu.transfer_addr;
}

unsigned int SpuGetTransferStartAddr(void) { return g_psx_spu.transfer_addr; }

int SpuSetTransferMode(int mode) {
  g_psx_spu.transfer_mode = mode;
  return mode;
}

int SpuGetTransferMode(void) { return g_psx_spu.transfer_mode; }

static void spu_copy(unsigned char *buf, unsigned int size, int write) {
  unsigned int a = g_psx_spu.transfer_addr;
  while (size) {
    unsigned int n = PSX_SPU_RAM_SIZE - a;
    if (n > size)
      n = size;
    if (write)
      memcpy(g_psx_spu.ram + a, buf, n);
    else
      memcpy(buf, g_psx_spu.ram + a, n);
    buf += n;
    size -= n;
    a = 0;
  }
}

unsigned int SpuWrite(unsigned char *addr, unsigned int size) {
  PASS_GUARD(size);
  if (size > 0x7EFF0)
    size = 0x7EFF0;
  spu_copy(addr, size, 1);
  return size;
}

unsigned int SpuRead(unsigned char *addr, unsigned int size) {
  if (size > 0x7EFF0)
    size = 0x7EFF0;
  spu_copy(addr, size, 0);
  return size;
}

int SpuIsTransferCompleted(int flag) {
  (void)flag;
  return 1;
}

SpuTransferCallbackProc SpuSetTransferCallback(SpuTransferCallbackProc func) {
  (void)func; /* transfers complete before SpuWrite returns */
  return NULL;
}
