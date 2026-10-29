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

/* Provided by the platform: the mixer's output, 44.1 kHz s16 interleaved
 * stereo, pushed once per vblank from psx_spu_vblank(). */
void psx_audio_out(const short *pcm, int frames);

#ifdef __cplusplus
}
#endif

#endif
