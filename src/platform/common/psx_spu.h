/* SPU state behind our libspu (docs/porting-plan.md §6): the 512 KB SPU RAM
 * image and 24 voices with the hardware's playback position (current ADPCM
 * block + 4.12 sample counter). psx_spu_vblank() mixes one vblank of output
 * (735 samples at 44.1 kHz) and hands it to psx_audio_out(). */
#ifndef PSX_SPU_H
#define PSX_SPU_H

#ifdef __cplusplus
extern "C" {
#endif

#define PSX_SPU_RAM_SIZE 0x80000
#define PSX_SPU_VOICES 24
#define PSX_SPU_RATE 44100
#define PSX_SPU_VBLANK_HZ 60 /* virtual vblank: 735 samples each */
#define PSX_SPU_VBLANK_SAMPLES (PSX_SPU_RATE / PSX_SPU_VBLANK_HZ)

enum { PSX_ENV_OFF, PSX_ENV_ATTACK, PSX_ENV_DECAY, PSX_ENV_SUSTAIN, PSX_ENV_RELEASE };

typedef struct {
  /* registers, as libspu set them */
  short vol_l, vol_r, volmode_l, volmode_r;
  unsigned short pitch;    /* 0x1000 = 44100 Hz; hardware caps at 0x4000 */
  unsigned int start_addr; /* bytes */
  unsigned int loop_addr;  /* bytes; ADPCM loop-start flags overwrite it */
  int a_mode, s_mode, r_mode;
  unsigned short ar, dr, sr, rr, sl;
  /* playback */
  unsigned char key_on;    /* libspu key status bit */
  unsigned char env_phase; /* PSX_ENV_* */
  short envx;              /* 0..0x7FFF */
  unsigned int cur_addr;   /* current 16-byte ADPCM block */
  unsigned int counter;    /* position in the block, 4.12 samples (28 per block) */
  short hist[2];           /* ADPCM decoder history (older, old) */
  unsigned int keyon_seq;  /* bumped on each key on */
  /* mixer */
  short dec[28];           /* cur_addr's block, decoded */
  short prev;              /* last sample of the block before it */
  int dec_valid;
} PsxSpuVoice;

typedef struct {
  unsigned char ram[PSX_SPU_RAM_SIZE];
  PsxSpuVoice voice[PSX_SPU_VOICES];
  short mvol_l, mvol_r, mvolmode_l, mvolmode_r;
  short cd_vol_l, cd_vol_r;
  int cd_reverb, cd_mix;
  short ext_vol_l, ext_vol_r;
  int ext_reverb, ext_mix;
  unsigned int transfer_addr;
  int transfer_mode;
} PsxSpu;

extern PsxSpu g_psx_spu;

/* Moves a keyed voice on by `samples` output samples at 44100 Hz, following
 * the ADPCM block flags (loop start, loop end, repeat). */
void psx_spu_voice_advance(PsxSpuVoice *v, unsigned int samples);

/* The registers the mixer reads besides the voices'. */
typedef struct {
  short mvol_l, mvol_r, cd_vol_l, cd_vol_r;
  int cd_on;                  /* SPU CD mix on and the drive not muted */
  unsigned char cd_matrix[4]; /* CdMix val0..3 (PsxCdStream.mix) */
} PsxSpuMixRegs;

/* psx_spu_vblank() is these three: the registers now, one vblank's mix of
 * `voice` (which it advances, decoder state too; CD input from
 * psx_xa_next), and psx_audio_out. A platform that mixes on another thread
 * (src/platform/vita/mixer.c) mixes copies of the voices there, and runs
 * psx_spu_step() on g_psx_spu instead so what the game reads back (key
 * status, envelopes) is what the mix leaves. */
void psx_spu_mix_regs(PsxSpuMixRegs *r);
void psx_spu_mix(PsxSpuVoice voice[PSX_SPU_VOICES], const PsxSpuMixRegs *reg,
                 short out[PSX_SPU_VBLANK_SAMPLES * 2]);
void psx_spu_step(void);

/* libspu.c's step past the end of cur_addr's block (the block flags: loop
 * start, loop end, repeat); 0 if the voice went silent. Position only. */
int psx_spu_voice_next_pos(PsxSpuVoice *v);

/* The native mix (spu_native.c, D84): psx_spu_mix's output from SPU RAM
 * decoded to PCM once, voice by voice over the vblank. g_psx_spu_native
 * picks it in psx_spu_vblank (and keeps the PCM cache up to date in
 * SpuWrite); g_psx_spu_simd 0 takes the C path on ARM too (tests). */
extern int g_psx_spu_native, g_psx_spu_simd;
void psx_spu_mix_native(PsxSpuVoice voice[PSX_SPU_VOICES], const PsxSpuMixRegs *reg,
                        short out[PSX_SPU_VBLANK_SAMPLES * 2]);
void psx_spu_pcm_write(unsigned addr, unsigned size); /* SPU RAM written: forget its PCM */
void psx_spu_pcm_reset(void);                         /* forget every block (SpuInit, a state load) */
typedef struct {
  unsigned long long hits[2], misses, written; /* blocks: from each way, decoded in the mix, forgotten at SpuWrite */
} PsxSpuNativeStats;
extern PsxSpuNativeStats g_psx_spu_native_stats;

/* Provided by the platform: the mixer's output, 44.1 kHz s16 interleaved
 * stereo, pushed once per vblank from psx_spu_vblank(). */
void psx_audio_out(const short *pcm, int frames);

#ifdef __cplusplus
}
#endif

#endif
