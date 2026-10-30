/* libcd over files and the minimal libspu (docs/porting-plan.md §6).
 * usage: test_cd <disc/files>. SPU cases always run; CD cases need the
 * extracted disc and the test exits 77 (skipped) without it. */
#include "psx_cd.h"
#include "psx_hooks.h"
#include "psx_spu.h"

#include <libcd.h>
#include <libspu.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);             \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static int read_file(const char *dir, const char *name, long off, void *buf, int n) {
  char path[512];
  FILE *f;
  int got;
  snprintf(path, sizeof path, "%s/%s", dir, name);
  f = fopen(path, "rb");
  if (!f)
    return -1;
  fseek(f, off, SEEK_SET);
  got = (int)fread(buf, 1, (size_t)n, f);
  fclose(f);
  return got;
}

/* ---- SPU ---- */

static void test_spu_transfer(void) {
  static unsigned char out[0x8000], in[0x8000];
  for (int i = 0; i < (int)sizeof out; i++)
    out[i] = (unsigned char)(i * 7 + (i >> 8));
  SpuInit();
  CHECK(SpuSetTransferStartAddr(0x1000) == 0);
  CHECK(SpuSetTransferStartAddr(0x80000) == 0);
  CHECK(SpuSetTransferStartAddr(0x1010) == 0x1010);
  CHECK(SpuWrite(out, sizeof out) == sizeof out);
  CHECK(SpuIsTransferCompleted(SPU_TRANSFER_PEEK));
  CHECK(SpuSetTransferStartAddr(0x1010) == 0x1010);
  CHECK(SpuRead(in, sizeof in) == sizeof in);
  CHECK(memcmp(in, out, sizeof in) == 0);
  CHECK(memcmp(g_psx_spu.ram + 0x1010, out, sizeof out) == 0);
  {
    unsigned char *big = calloc(1, 0x90000);
    CHECK(SpuSetTransferStartAddr(0x1010) == 0x1010);
    CHECK(SpuWrite(big, 0x90000) == 0x7EFF0); /* PsyQ size cap */
    free(big);
  }
}

/* `blocks` ADPCM blocks at addr; the last has `last_flags`, the first `first_flags`. */
static void put_sample(unsigned int addr, int blocks, int first_flags, int last_flags) {
  static unsigned char buf[16 * 256];
  memset(buf, 0, sizeof buf);
  buf[1] = (unsigned char)first_flags;
  buf[(blocks - 1) * 16 + 1] |= (unsigned char)last_flags;
  SpuSetTransferStartAddr(addr);
  SpuWrite(buf, (unsigned int)blocks * 16);
}

static void key_on(int voice, unsigned int addr, unsigned short pitch) {
  SpuVoiceAttr a;
  memset(&a, 0, sizeof a);
  a.voice = 1u << voice;
  a.mask = SPU_VOICE_WDSA | SPU_VOICE_PITCH | SPU_VOICE_VOLL | SPU_VOICE_VOLR;
  a.addr = addr;
  a.pitch = pitch;
  a.volume.left = a.volume.right = 0x2000;
  SpuSetVoiceAttr(&a);
  SpuSetKey(SPU_ON, 1u << voice);
}

/* vblanks until the voice reports SPU_RESET (sample ended), or -1 */
static int vblanks_until_end(int voice, int limit) {
  for (int n = 1; n <= limit; n++) {
    psx_spu_vblank();
    int s = SpuGetKeyStatus(1u << voice);
    if (s == SPU_RESET)
      return n;
    if (s != SPU_ON)
      return -2;
  }
  return -1;
}

static void test_spu_voices(void) {
  char st[24];
  short l, r;
  SpuInit();
  CHECK(SpuGetKeyStatus(0) == -1);
  CHECK(SpuGetKeyStatus(1u << 5) == SPU_OFF);

  /* 100 blocks = 2800 samples. 0x1000 = 44100 Hz: 735 samples per vblank,
   * so it ends during vblank 4 (2940 >= 2800). */
  put_sample(0x2000, 100, 0, 1);
  key_on(5, 0x2000, 0x1000);
  CHECK(SpuGetKeyStatus(1u << 5) == SPU_ON);
  CHECK(vblanks_until_end(5, 100) == 4);
  SpuGetAllKeysStatus(st);
  CHECK(st[5] == SPU_RESET && st[4] == SPU_OFF);
  SpuSetKey(SPU_OFF, 1u << 5);
  CHECK(SpuGetKeyStatus(1u << 5) == SPU_OFF);

  /* half pitch: 5600 samples -> vblank 8 (5880) */
  key_on(7, 0x2000, 0x800);
  CHECK(vblanks_until_end(7, 100) == 8);

  /* key off mid-sample */
  key_on(3, 0x2000, 0x1000);
  psx_spu_vblank();
  SpuSetKey(SPU_OFF, 1u << 3);
  CHECK(SpuGetKeyStatus(1u << 3) == SPU_OFF);

  /* loop start on block 0, loop end + repeat on the last: never ends */
  put_sample(0x4000, 10, 4, 3);
  key_on(9, 0x4000, 0x3FFF);
  CHECK(vblanks_until_end(9, 300) == -1);
  CHECK(g_psx_spu.voice[9].loop_addr == 0x4000);

  SpuSetVoiceVolume(9, 0x1234, 0x0567);
  SpuGetVoiceVolume(9, &l, &r);
  CHECK(l == 0x1234 && r == 0x0567);
}

/* ---- CD ---- */

static int cb_count, cb_intr;
static volatile int is_reading;
static void read_done(u_char intr, u_char *result) {
  (void)result;
  cb_count++;
  cb_intr = intr;
  if (intr == CdlComplete)
    is_reading = 0;
}

static void test_cd_pos(void) {
  CdlLOC loc;
  CdIntToPos(37, &loc);
  CHECK(loc.minute == 0x00 && loc.second == 0x02 && loc.sector == 0x37);
  CdIntToPos(226384, &loc); /* PETEXA5.STR: 50:20:34 + 2 s */
  CHECK(loc.minute == 0x50 && loc.second == 0x20 && loc.sector == 0x34);
  for (int i = 0; i < 333000; i += 7) {
    CdIntToPos(i, &loc);
    if (CdPosToInt(&loc) != i) {
      CHECK(CdPosToInt(&loc) == i);
      break;
    }
  }
}

static void start_read(int lba, int sectors, void *buf) {
  u_char mode = CdlModeSpeed;
  CdlLOC loc;
  CdControl(CdlSetmode, &mode, 0);
  CdIntToPos(lba, &loc);
  CdControl(CdlSetloc, (u_char *)&loc, 0);
  is_reading = 1;
  CHECK(CdRead(sectors, (u_long *)buf, CdlModeSpeed) == 1);
}

static void test_cd_read(const char *dir) {
  static unsigned char buf[4 * 2048 + 16], ref[3 * 2048];

  CHECK(CdInit() == 1);
  CHECK(CdReadCallback(read_done) == NULL);
  CHECK(CdReadCallback(read_done) == read_done);

  /* WAD.WAD header: completes at the next vblank, via the callback */
  memset(buf, 0xAA, sizeof buf);
  cb_count = 0;
  start_read(37, 1, buf);
  CHECK(cb_count == 0 && is_reading);
  CHECK(CdLastCom() == CdlReadN);
  psx_cd_vblank();
  CHECK(cb_count == 1 && cb_intr == CdlComplete && !is_reading);
  CHECK(CdSync(1, 0) == CdlComplete);
  CHECK(read_file(dir, "WAD.WAD", 0, ref, 2048) == 2048);
  CHECK(memcmp(buf, ref, 2048) == 0);
  CHECK(buf[2048] == 0xAA); /* exactly one sector */

  /* 3 sectors at offset 1000: a CdSync(1) poll is a tick too */
  start_read(37 + 1000, 3, buf);
  CHECK(CdSync(1, 0) == CdlComplete && !is_reading);
  CHECK(read_file(dir, "WAD.WAD", 1000L * 2048, ref, 3 * 2048) == 3 * 2048);
  CHECK(memcmp(buf, ref, 3 * 2048) == 0);

  /* loaders.c:820 shape: CdSync(1) first, then the flag */
  {
    int spins = 0;
    start_read(37 + 5, 2, buf);
    while ((CdSync(1, 0) != CdlComplete || is_reading) && spins < 1000)
      spins++;
    CHECK(spins < 1000 && !is_reading);
  }

  /* cd.c:105 (CDLoadSync) shape: the flag short-circuits CdSync, so no lib
   * call happens in the loop. It ends only on a vblank (an interrupt) or
   * with latency 0. */
  {
    int spins = 0;
    psx_cd_set_read_latency(0);
    start_read(37 + 9, 4, buf);
    while ((is_reading || CdSync(1, 0) != CdlComplete) && spins < 1000)
      spins++;
    CHECK(spins == 0 && !is_reading);
    CHECK(read_file(dir, "WAD.WAD", 9L * 2048, ref, 2048) == 2048);
    CHECK(memcmp(buf, ref, 2048) == 0);
    psx_cd_set_read_latency(1);
  }

  /* CdSync(0) completes synchronously */
  start_read(37, 1, buf);
  CHECK(CdSync(0, 0) == CdlComplete && !is_reading);

  /* CdInit drops callbacks (CDLoadTime re-installs it) */
  CHECK(CdInit() == 1);
  CHECK(CdReadCallback(NULL) == NULL);
}

static void test_cd_xa(const char *dir) {
  const int start = 60000; /* PETEXA0.STR */
  u_char mode = CdlModeSF | CdlModeRT | CdlModeSpeed;
  u_char res[8];
  CdlFILTER filter = {1, 3, 0};
  CdlLOC loc;
  int ok = 1;

  CdInit();
  CdControlB(CdlSetmode, &mode, 0);
  CdControlB(CdlSetfilter, (u_char *)&filter, 0);
  CdIntToPos(start, &loc);
  CHECK(CdControlB(CdlReadS, (u_char *)&loc, 0) == 1);
  CHECK(psx_cd_stream()->active && psx_cd_stream()->chan == 3 && psx_cd_stream()->file == 1);

  /* music.c:76-84: GetlocP via CdControlF, read back by CdSync(1, buf) next frame */
  for (int k = 0; k <= 600; k++) {
    CdControlF(CdlGetlocP, 0);
    if (CdSync(1, res) != CdlComplete || CdLastCom() != CdlGetlocP ||
        CdPosToInt((CdlLOC *)&res[5]) != start + k * 5 / 2) {
      fprintf(stderr, "XA at vblank %d: pos %d, want %d\n", k,
              CdPosToInt((CdlLOC *)&res[5]), start + k * 5 / 2);
      ok = 0;
      break;
    }
    psx_cd_vblank();
  }
  CHECK(ok);

  /* GetlocL reports the subheader of the sector under the head */
  {
    unsigned char sub[8];
    int lba;
    CdControlF(CdlGetlocL, 0);
    CdSync(1, res);
    lba = CdPosToInt((CdlLOC *)res);
    CHECK(lba == start + 601 * 5 / 2);
    CHECK(read_file(dir, "PETEXA0.STR", (long)(lba - start) * PSX_CD_RAW_SECTOR, sub, 8) == 8);
    CHECK(memcmp(res + 4, sub, 4) == 0);
    printf("XA sector %d: file %d chan %d submode 0x%02x coding 0x%02x\n", lba, res[4],
           res[5], res[6], res[7]);
  }

  /* Pause stops the head; single speed is 1.25 sectors per vblank */
  CdControl(CdlPause, 0, 0);
  {
    int before, after;
    CdControlF(CdlGetlocP, 0);
    CdSync(1, res);
    before = CdPosToInt((CdlLOC *)&res[5]);
    for (int i = 0; i < 10; i++)
      psx_cd_vblank();
    CdControlF(CdlGetlocP, 0);
    CdSync(1, res);
    after = CdPosToInt((CdlLOC *)&res[5]);
    CHECK(before == after);
  }
  mode = CdlModeSF | CdlModeRT;
  CdControlB(CdlSetmode, &mode, 0);
  CdIntToPos(start, &loc);
  CdControlB(CdlReadS, (u_char *)&loc, 0);
  for (int i = 0; i < 60; i++)
    psx_cd_vblank();
  CdControlF(CdlGetlocP, 0);
  CdSync(1, res);
  CHECK(CdPosToInt((CdlLOC *)&res[5]) == start + 75);
  CdControl(CdlPause, 0, 0);
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : "disc/files";
  char path[512];
  FILE *f;

  test_spu_transfer();
  test_spu_voices();
  test_cd_pos();

  snprintf(path, sizeof path, "%s/manifest.json", dir);
  f = fopen(path, "rb");
  if (!f) {
    printf(failures ? "%d SPU failure(s)\n" : "SPU cases passed\n", failures);
    printf("SKIP CD cases: no extracted disc at %s (run tools/extract_disc.py)\n", dir);
    return failures ? 1 : 77;
  }
  fclose(f);
  CHECK(psx_cd_open(dir) == 0);
  test_cd_read(dir);
  test_cd_xa(dir);

  printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
