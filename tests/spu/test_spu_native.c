/* The native SPU mix (src/platform/common/spu_native.c, decisions D84)
 * against libspu.c's psx_spu_mix: random SPU RAM (any shift, filter and
 * block flags), voices keyed on, off and re-pitched at random, XA input from
 * random sectors. Every vblank both mixes start from the same voices and XA
 * state; the 735 output frames, every voice field and the XA state after
 * them must be the same. Where NEON is there (the Vita; an arm64 Mac:
 * tests/spu/neon_check.sh), the NEON path is checked against the C path too.
 *   test_spu_native [VBLANKS [SEED]]
 * A mutant build (-DMUTANT) changes one rounding in the C path and must fail. */
#include "psx_cd.h"
#include "psx_spu.h"
#include "psx_state.h"
#include "psx_xa.h"

#include <libspu.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* What xa.c and libspu.c need from libcd.c. */
static PsxCdStream s_stream = {0, 0, 0, 0, 0, 0, {0x80, 0, 0x80, 0}};
const PsxCdStream *psx_cd_stream(void) { return &s_stream; }
int psx_cd_read_raw(int lba, unsigned char raw[PSX_CD_RAW_SECTOR]) {
  (void)lba;
  memset(raw, 0, PSX_CD_RAW_SECTOR);
  return -1;
}
void psx_audio_out(const short *pcm, int frames) {
  (void)pcm;
  (void)frames;
}

static unsigned s_rng = 1;
static unsigned rnd(void) {
  s_rng = s_rng * 1103515245u + 12345u;
  return s_rng >> 8;
}

/* Blocks with every shift (13-15 too) and filter (5-7 too); flags mostly 0,
 * sometimes loop start, loop end with or without repeat. */
static void random_ram(unsigned from, unsigned to) {
  unsigned a;
  for (a = from; a < to; a += 16) {
    unsigned char *b = g_psx_spu.ram + a;
    unsigned f = rnd() % 100, i;
    b[0] = (unsigned char)rnd();
    b[1] = f < 80 ? 0 : f < 88 ? 4 : f < 94 ? 3 : f < 97 ? 1 : (unsigned char)rnd() & 7;
    for (i = 2; i < 16; i++)
      b[i] = (unsigned char)rnd();
  }
}

static void random_sector(unsigned char raw[PSX_CD_RAW_SECTOR]) {
  int i;
  for (i = 0; i < PSX_CD_RAW_SECTOR; i++)
    raw[i] = (unsigned char)rnd();
  raw[0] = 1;
  raw[1] = 0;
  raw[2] = 0x64; /* audio, Form 2 */
  raw[3] = (unsigned char)(rnd() % 4 == 0 ? 0x04 | (rnd() & 1) : rnd() & 1); /* 18.9 or 37.8 kHz, mono or stereo */
  for (i = 0; i < 18; i++) { /* sound parameters: filters 0-3, any shift */
    int k;
    for (k = 0; k < 8; k++)
      raw[8 + i * 128 + 4 + k] = (unsigned char)(rnd() & 0x3F);
  }
}

static void random_voice_change(void) {
  SpuVoiceAttr a;
  int v = (int)(rnd() % PSX_SPU_VOICES);
  unsigned r = rnd() % 10;
  memset(&a, 0, sizeof a);
  a.voice = 1u << v;
  if (r < 4) {
    unsigned p = rnd() % 8;
    a.mask = SPU_VOICE_VOLL | SPU_VOICE_VOLR | SPU_VOICE_PITCH | SPU_VOICE_WDSA | SPU_VOICE_LSAX;
    a.volume.left = (short)(rnd() % 3 == 0 ? rnd() : rnd() & 0x3FFF);
    a.volume.right = (short)(rnd() & 0x3FFF);
    a.pitch = (unsigned short)(p == 0 ? 0 : p == 1 ? 0x1000 : p == 2 ? 0x4000 + (rnd() & 0xFFF) : rnd() & 0x3FFF);
    a.addr = rnd() % PSX_SPU_RAM_SIZE;
    a.loop_addr = rnd() % PSX_SPU_RAM_SIZE;
    SpuSetVoiceAttr(&a);
    SpuSetKey(SPU_ON, a.voice);
  } else if (r < 6) {
    SpuSetKey(SPU_OFF, a.voice);
  } else if (r < 8) {
    SpuSetVoicePitch(v, (unsigned short)(rnd() & 0x5FFF));
  } else {
    SpuSetVoiceVolume(v, (short)(rnd() & 0x3FFF), (short)(rnd() & 0x3FFF));
  }
}

static unsigned char s_xa_a[1 << 17], s_xa_b[1 << 17];

static size_t xa_io(unsigned char *buf, int mode) {
  PsxStateIO io;
  io.mode = mode;
  io.p = buf;
  io.n = 0;
  psx_xa_state(&io);
  return io.n;
}

static int compare(int vb, const char *what, const PsxSpuVoice *va, const PsxSpuVoice *vbv,
                   const short *oa, const short *ob) {
  int i;
  for (i = 0; i < PSX_SPU_VBLANK_SAMPLES * 2; i++)
    if (oa[i] != ob[i]) {
      printf("vblank %d, %s: frame %d %s: %d, not %d\n", vb, what, i / 2, i & 1 ? "R" : "L", ob[i],
             oa[i]);
      return 1;
    }
  for (i = 0; i < PSX_SPU_VOICES; i++)
    if (memcmp(&va[i], &vbv[i], sizeof va[i])) {
      printf("vblank %d, %s: voice %d differs after the mix (cur_addr %x/%x counter %x/%x)\n", vb,
             what, i, va[i].cur_addr, vbv[i].cur_addr, va[i].counter, vbv[i].counter);
      return 1;
    }
  return 0;
}

int main(int argc, char **argv) {
  static PsxSpuVoice va[PSX_SPU_VOICES], vn[PSX_SPU_VOICES], vs[PSX_SPU_VOICES];
  static short oa[PSX_SPU_VBLANK_SAMPLES * 2], on[PSX_SPU_VBLANK_SAMPLES * 2],
      os[PSX_SPU_VBLANK_SAMPLES * 2];
  int vblanks = argc > 1 ? atoi(argv[1]) : 20000, vb, simd_checked = 0;
  size_t xan;
  PsxSpuMixRegs reg;
  s_rng = argc > 2 ? (unsigned)atoi(argv[2]) : 1;
  g_psx_spu_native = 1;
  SpuInit();
  random_ram(0, PSX_SPU_RAM_SIZE);
  SpuSetTransferStartAddr(0x1010);
  SpuWrite(g_psx_spu.ram + 0x1010, PSX_SPU_RAM_SIZE - 0x1010); /* the cache from SpuWrite */
  psx_xa_reset();
  for (vb = 0; vb < vblanks; vb++) {
    int k;
    for (k = rnd() % 6; k > 0; k--)
      random_voice_change();
    if (rnd() % 50 == 0) { /* new samples: SpuWrite of a part, the rest left as it was */
      unsigned a = 0x1010 + (rnd() % 0x70000 & ~7u), n = rnd() % 0x8000 + 1;
      static unsigned char buf[0x8000 + 16];
      unsigned i;
      for (i = 0; i < n; i++)
        buf[i] = (unsigned char)rnd();
      SpuSetTransferStartAddr(a);
      SpuWrite(buf, n);
    }
    if (rnd() % 500 == 0)
      psx_spu_pcm_reset(); /* as after a state load: everything decoded in the mix */
    if (rnd() % 3 != 0) { /* XA: about the stream's 2.5 sectors a vblank, less often */
      unsigned char raw[PSX_CD_RAW_SECTOR];
      random_sector(raw);
      psx_xa_decode(raw);
    }
    if (rnd() % 400 == 0)
      psx_xa_reset();
    reg.mvol_l = (short)(rnd() % 8 == 0 ? rnd() : 0x3FFF);
    reg.mvol_r = (short)(rnd() & 0x7FFF);
    reg.cd_vol_l = (short)rnd();
    reg.cd_vol_r = (short)(rnd() & 0x7FFF);
    reg.cd_on = rnd() % 4 != 0;
    for (k = 0; k < 4; k++)
      reg.cd_matrix[k] = (unsigned char)(rnd() % 3 == 0 ? rnd() : k == 0 || k == 2 ? 0x80 : 0);
    xan = xa_io(s_xa_a, PSX_STATE_SAVE);
    memcpy(va, g_psx_spu.voice, sizeof va);
    memcpy(vn, g_psx_spu.voice, sizeof vn);
    memcpy(vs, g_psx_spu.voice, sizeof vs);
    psx_spu_mix(va, &reg, oa);
    xa_io(s_xa_b, PSX_STATE_SAVE);
    xa_io(s_xa_a, PSX_STATE_LOAD);
    g_psx_spu_simd = 0;
    psx_spu_mix_native(vn, &reg, on);
    if (compare(vb, "native C", va, vn, oa, on))
      return 1;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    xa_io(s_xa_a, PSX_STATE_LOAD);
    g_psx_spu_simd = 1;
    psx_spu_mix_native(vs, &reg, os);
    if (compare(vb, "native NEON", va, vs, oa, os))
      return 1;
    simd_checked++;
#endif
    {
      static unsigned char after[1 << 17];
      xa_io(after, PSX_STATE_SAVE);
      if (memcmp(after, s_xa_b, xan)) {
        printf("vblank %d: XA state differs after the mix\n", vb);
        return 1;
      }
    }
    memcpy(g_psx_spu.voice, va, sizeof va);
  }
  {
    const PsxSpuNativeStats *st = &g_psx_spu_native_stats;
    printf("spu native: %d vblanks identical (%d with NEON too); blocks %llu way 0, %llu way 1, "
           "%llu decoded in the mix\n",
           vblanks, simd_checked, st->hits[0], st->hits[1], st->misses);
  }
  return 0;
}
