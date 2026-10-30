/* XA ADPCM for streamed CD audio (docs/porting-plan.md §6, decisions D12),
 * written from the public format description (psx-spx "CDROM XA Audio
 * ADPCM Compression"). libcd feeds every sector the head passes while a
 * ReadS stream runs in real-time mode; sectors that are audio and match the
 * Setfilter file and channel (when CdlModeSF is set) are decoded into a FIFO
 * of 37.8 or 18.9 kHz stereo frames, which the SPU mixer resamples to 44.1 kHz
 * as its CD input. */
#include "psx_cd.h"
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
 * XA isn't used by Spyro and is skipped). */
static void decode_sector(const unsigned char *data, int stereo) {
  int g, blk, i;
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
        short s = decode_one(nib, shift, filter, s_hist[ch]);
        if (!stereo)
          mono[blk * 28 + i] = s;
        else if (ch == 0)
          left[(blk >> 1) * 28 + i] = s;
        else
          right[(blk >> 1) * 28 + i] = s;
      }
    }
    if (stereo)
      for (i = 0; i < 28 * 4; i++)
        push(left[i], right[i]);
    else
      for (i = 0; i < 28 * 8; i++)
        push(mono[i], mono[i]);
  }
}

int psx_xa_accept(int lba, unsigned char raw[PSX_CD_RAW_SECTOR]) {
  const PsxCdStream *st = psx_cd_stream();
  if (!(st->mode & CdlModeRT) || psx_cd_read_raw(lba, raw) != 0)
    return 0;
  if (!(raw[2] & 0x04) || !(raw[2] & 0x20)) /* audio, Form 2 */
    return 0;
  if ((st->mode & CdlModeSF) && (raw[0] != st->file || raw[1] != st->chan))
    return 0;
  if (raw[3] & 0x10) /* 8-bit samples */
    return 0;
  return 1;
}

void psx_xa_decode(const unsigned char raw[PSX_CD_RAW_SECTOR]) {
  int coding = raw[3];
  s_rate = (coding & 0x04) ? 18900 : 37800;
  decode_sector(raw + 8, coding & 1);
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
