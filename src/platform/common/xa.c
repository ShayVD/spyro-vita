/* XA ADPCM for streamed CD audio (docs/porting-plan.md §6, decisions D12),
 * written from the public format description (psx-spx "CDROM XA Audio
 * ADPCM Compression"). libcd feeds every sector the head passes while a
 * ReadS stream runs in real-time mode; sectors that are audio and match the
 * Setfilter file and channel (when CdlModeSF is set) are decoded into a FIFO
 * of 37.8 or 18.9 kHz stereo frames, which the SPU mixer resamples to 44.1 kHz
 * as its CD input. */
#include "psx_cd.h"
#include "psx_state.h"
#include "psx_xa.h"

#include <libcd.h>
#include <string.h>

#define FIFO 16384 /* frames; a sector is at most 4032 (mono, 18.9 kHz) */

static short s_fifo[FIFO][2];
static unsigned s_head, s_tail; /* read and write positions, free-running */
static int s_rate = 37800;
static int s_hist[2][2]; /* per channel: older, old */
static unsigned s_frac;  /* resampler phase, 16.16 input frames */
static short s_prev[2], s_cur[2];

static const int k_f0[4] = {0, 60, 115, 98};
static const int k_f1[4] = {0, 0, -52, -55};

void psx_xa_reset(void) {
  s_head = s_tail = 0;
  s_frac = 0;
  memset(s_hist, 0, sizeof s_hist);
  memset(s_prev, 0, sizeof s_prev);
  memset(s_cur, 0, sizeof s_cur);
}

/* Save states (D51). */
void psx_xa_state(PsxStateIO *io) {
  psx_state_io(io, s_fifo, sizeof s_fifo);
  psx_state_io(io, &s_head, sizeof s_head);
  psx_state_io(io, &s_tail, sizeof s_tail);
  psx_state_io(io, &s_rate, sizeof s_rate);
  psx_state_io(io, s_hist, sizeof s_hist);
  psx_state_io(io, &s_frac, sizeof s_frac);
  psx_state_io(io, s_prev, sizeof s_prev);
  psx_state_io(io, s_cur, sizeof s_cur);
}

static void push(short l, short r) {
  if (s_tail - s_head >= FIFO)
    s_head++; /* overrun: drop the oldest */
  s_fifo[s_tail % FIFO][0] = l;
  s_fifo[s_tail % FIFO][1] = r;
  s_tail++;
}

static short decode_one(int nibble, int shift, int filter, int *h) {
  int s = (short)(nibble << 12) >> shift;
  s += (h[1] * k_f0[filter] + h[0] * k_f1[filter] + 32) >> 6;
  if (s > 32767)
    s = 32767;
  if (s < -32768)
    s = -32768;
  h[0] = h[1];
  h[1] = s;
  return (short)s;
}

/* Decode the 18 sound groups of one Form 2 audio sector (4-bit only: 8-bit
 * XA isn't used by Spyro and is skipped) into frames, from and to the
 * history hist; returns the frame count. */
static int decode_sector(const unsigned char *data, int stereo, int hist[2][2],
                         short frames[PSX_XA_SECTOR_FRAMES][2]) {
  int g, blk, i, nf = 0;
  short left[28 * 4], right[28 * 4], mono[28 * 8];
  for (g = 0; g < 18; g++) {
    const unsigned char *grp = data + g * 128;
    for (blk = 0; blk < 8; blk++) {
      int param = grp[4 + blk];
      int shift = param & 15, filter = (param >> 4) & 3;
      int ch = stereo ? (blk & 1) : 0;
      if (shift > 12)
        shift = 9;
      for (i = 0; i < 28; i++) {
        int b = grp[16 + i * 4 + (blk >> 1)];
        int nib = (blk & 1) ? b >> 4 : b & 15;
        short s = decode_one(nib, shift, filter, hist[ch]);
        if (!stereo)
          mono[blk * 28 + i] = s;
        else if (ch == 0)
          left[(blk >> 1) * 28 + i] = s;
        else
          right[(blk >> 1) * 28 + i] = s;
      }
    }
    if (stereo)
      for (i = 0; i < 28 * 4; i++, nf++)
        frames[nf][0] = left[i], frames[nf][1] = right[i];
    else
      for (i = 0; i < 28 * 8; i++, nf++)
        frames[nf][0] = frames[nf][1] = mono[i];
  }
  return nf;
}

static void push_frames(const short (*frames)[2], int n) {
  int i;
  for (i = 0; i < n; i++)
    push(frames[i][0], frames[i][1]);
}

int psx_xa_accept(int lba, unsigned char raw[PSX_CD_RAW_SECTOR]) {
  const PsxCdStream *st = psx_cd_stream();
  if (!(st->mode & CdlModeRT) || psx_cd_read_raw(lba, raw) != 0)
    return 0;
  return psx_xa_check(raw);
}

int psx_xa_check(const unsigned char raw[PSX_CD_RAW_SECTOR]) {
  const PsxCdStream *st = psx_cd_stream();
  return psx_xa_check_filter(raw, st->mode, st->file, st->chan);
}

int psx_xa_check_filter(const unsigned char raw[PSX_CD_RAW_SECTOR], int mode, int file, int chan) {
  if (!(raw[2] & 0x04) || !(raw[2] & 0x20)) /* audio, Form 2 */
    return 0;
  if ((mode & CdlModeSF) && (raw[0] != file || raw[1] != chan))
    return 0;
  if (raw[3] & 0x10) /* 8-bit samples */
    return 0;
  return 1;
}

void psx_xa_decode(const unsigned char raw[PSX_CD_RAW_SECTOR]) {
  static short frames[PSX_XA_SECTOR_FRAMES][2];
  int coding = raw[3];
  s_rate = (coding & 0x04) ? 18900 : 37800;
  push_frames((const short (*)[2])frames, decode_sector(raw + 8, coding & 1, s_hist, frames));
}

/* psx_xa_decode ahead of time, from an assumed history (D84). */
void psx_xa_predecode(const unsigned char raw[PSX_CD_RAW_SECTOR], const int hist[2][2],
                      PsxXaPcm *out) {
  int coding = raw[3];
  memcpy(out->key, hist, sizeof out->key);
  memcpy(out->exit, hist, sizeof out->exit);
  out->rate = (coding & 0x04) ? 18900 : 37800;
  out->frames = decode_sector(raw + 8, coding & 1, out->exit, out->pcm);
}

/* psx_xa_decode from a predecoded sector, if it was decoded from the
 * history the stream has now: then its frames are the same. 0 if not (the
 * caller decodes the sector itself). */
int psx_xa_decoded(const PsxXaPcm *pcm) {
  if (memcmp(pcm->key, s_hist, sizeof s_hist))
    return 0;
  s_rate = pcm->rate;
  push_frames((const short (*)[2])pcm->pcm, pcm->frames);
  memcpy(s_hist, pcm->exit, sizeof s_hist);
  return 1;
}

void psx_xa_sector(int lba) {
  unsigned char raw[PSX_CD_RAW_SECTOR];
  if (psx_xa_accept(lba, raw))
    psx_xa_decode(raw);
}

/* One 44.1 kHz stereo frame of CD input (linear interpolation). */
void psx_xa_next(int *l, int *r) {
  unsigned step = (unsigned)(((unsigned long long)s_rate << 16) / 44100);
  while (s_frac >= 0x10000) {
    s_frac -= 0x10000;
    s_prev[0] = s_cur[0];
    s_prev[1] = s_cur[1];
    if (s_head != s_tail) {
      s_cur[0] = s_fifo[s_head % FIFO][0];
      s_cur[1] = s_fifo[s_head % FIFO][1];
      s_head++;
    } else {
      s_cur[0] = s_cur[1] = 0; /* underrun: silence */
    }
  }
  *l = s_prev[0] + (int)(((long long)(s_cur[0] - s_prev[0]) * s_frac) >> 16);
  *r = s_prev[1] + (int)(((long long)(s_cur[1] - s_prev[1]) * s_frac) >> 16);
  s_frac += step;
}

/* n frames of psx_xa_next, interleaved (the native mix, D84). */
void psx_xa_take(short *lr, int n) {
  unsigned step = (unsigned)(((unsigned long long)s_rate << 16) / 44100);
  int i;
  for (i = 0; i < n; i++) {
    while (s_frac >= 0x10000) {
      s_frac -= 0x10000;
      s_prev[0] = s_cur[0];
      s_prev[1] = s_cur[1];
      if (s_head != s_tail) {
        s_cur[0] = s_fifo[s_head % FIFO][0];
        s_cur[1] = s_fifo[s_head % FIFO][1];
        s_head++;
      } else {
        s_cur[0] = s_cur[1] = 0;
      }
    }
    lr[i * 2] = (short)(s_prev[0] + (int)(((long long)(s_cur[0] - s_prev[0]) * s_frac) >> 16));
    lr[i * 2 + 1] = (short)(s_prev[1] + (int)(((long long)(s_cur[1] - s_prev[1]) * s_frac) >> 16));
    s_frac += step;
  }
}
