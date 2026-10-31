/* libcd over files and the minimal libspu (docs/porting-plan.md §6), and
 * over a disc image (cdimage.c, decisions D60).
 * usage: test_cd <disc/files> [<disc/image> [<marker file>]]. SPU cases
 * always run; CD cases need the extracted disc and the test exits 77
 * (skipped) without it. With a folder holding the user's disc image, the
 * image's file table must render as the same manifest.json, and the same
 * read cases pass with the image open instead of the files. */
#include "psx_cd.h"
#include "psx_cdimage.h"
#include "psx_hooks.h"
#include "psx_spu.h"

#include <libcd.h>
#include <libspu.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

/* ---- the disc image (D60) ---- */

/* extract_disc.py's KNOWN: data files hashed as their bytes, XA files as
 * 2336-byte sectors. */
static const struct {
  const char *path, *sha1;
  int xa;
} k_known[] = {
    {"SYSTEM.CNF", "913fe0d270ad295117e6a91eb667284845df8918", 0},
    {"WAD.WAD", "ab68001377ea93fb309a5c2a35e772c9d1f57e0b", 0},
    {"SCUS_942.28", "84e3728ab94720d0873e2514adf4aade4935e0c5", 0},
    {"PETEXA0.STR", "045a1fedd5d4a224c95a65f3802488bf9c952c60", 1},
    {"PETEXA1.STR", "4fad48fbc15616998efb9379e976f5891d7c97e4", 1},
    {"PETEXA2.STR", "19ed8ddac4a83182d1744139b23a5bd284e5cba1", 1},
    {"PETEXA3.STR", "e51baa4cefc3f85e454943d964a3690b9f34be52", 1},
    {"PETEXA4.STR", "6c6b387bcd7b61226281d2adf48adbab547fa089", 1},
    {"PETEXA5.STR", "aa4a2c6b2eac867751d30a761ca9ab97877c3ce4", 1},
    {"S0/CRASH.EXE", "82d1047a5bf6b6563ea0107706e02b9e01dbcc2a", 0},
    {"S0/S000000D.NSD", "fb9788f83e39c71b61064ace91e41c2c5d79b58b", 0},
    {"S0/S000000D.NSF", "4203d38457b960452ba99e7f23955d40f3ba04e9", 0},
    {"S0/S000003C.NSD", "aa2d89c6e8b630df93120a332af2132858b604d8", 0},
    {"S0/S000003C.NSF", "c5105ab984dbcee6a787eb2a178f2764c37c36c1", 0},
    {"S0/WARP.BIN", "b5895f4fe129f9afdba70cfd1fdef794032be99f", 0},
    {"SOURCE/SOURCE.TRD", "498601c3f0b08dd66b53b16c2be9ac10d171721f", 0},
};

static double now_s(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return t.tv_sec + t.tv_nsec / 1e9;
}

/* Every sector of every file through psx_cd_read_raw, whichever backend is
 * open, against a good dump. */
static void test_known_hashes(const char *what) {
  static unsigned char raw[PSX_CD_RAW_SECTOR];
  double t0 = now_s();
  int ok = 0;
  for (size_t k = 0; k < sizeof k_known / sizeof *k_known; k++) {
    char name[64], hex[41];
    CdlFILE fp;
    PsxSha1 s;
    int lba, left;
    snprintf(name, sizeof name, "\\%s;1", k_known[k].path);
    for (char *c = name; *c; c++)
      if (*c == '/')
        *c = '\\';
    if (!CdSearchFile(&fp, name)) {
      fprintf(stderr, "FAIL %s: %s not found\n", what, k_known[k].path);
      failures++;
      continue;
    }
    psx_sha1_init(&s);
    lba = CdPosToInt(&fp.pos);
    for (left = (int)fp.size; left > 0; left -= 2048, lba++) {
      CHECK(psx_cd_read_raw(lba, raw) == 0);
      if (k_known[k].xa)
        psx_sha1_update(&s, raw, PSX_CD_RAW_SECTOR);
      else
        psx_sha1_update(&s, raw + 8, left < 2048 ? (unsigned)left : 2048);
    }
    psx_sha1_hex(&s, hex);
    if (strcmp(hex, k_known[k].sha1) != 0) {
      fprintf(stderr, "FAIL %s: %s SHA-1 %s, want %s\n", what, k_known[k].path, hex,
              k_known[k].sha1);
      failures++;
    } else {
      ok++;
    }
  }
  printf("%s: %d/%d files match a good dump through psx_cd_read_raw (%.1f s)\n", what, ok,
         (int)(sizeof k_known / sizeof *k_known), now_s() - t0);
}

/* The image's table, written as extract_disc.py's json.dump(indent=2), is
 * byte for byte the manifest.json it wrote from the same dump. */
static void test_image_manifest(const char *files_dir, const char *image_dir) {
  PsxCdImageFile *v = NULL;
  PsxCdError err = {0};
  char path[512], *have, *want;
  int n = 0, len = 0, got;
  FILE *f;
  CHECK(psx_cdimage_open(image_dir, &v, &n, &err) == 0);
  if (err.code)
    fprintf(stderr, "image: %s: %s\n", err.title, err.text);
  have = malloc(256 * (size_t)(n + 1));
  want = malloc(1 << 16);
  len += sprintf(have + len, "[");
  for (int i = 0; i < n; i++)
    len += sprintf(have + len,
                   "%s\n  {\n    \"path\": \"%s\",\n    \"lba\": %d,\n    \"size\": %d,\n"
                   "    \"type\": \"%s\"\n  }",
                   i ? "," : "", v[i].path, v[i].lba, v[i].size, v[i].xa ? "xa" : "data");
  len += sprintf(have + len, "\n]");
  snprintf(path, sizeof path, "%s/manifest.json", files_dir);
  f = fopen(path, "rb");
  got = f ? (int)fread(want, 1, (1 << 16) - 1, f) : -1;
  if (f)
    fclose(f);
  CHECK(got == len && memcmp(have, want, (size_t)len) == 0);
  printf("image: %d files; table %s %s\n", n, got == len && !memcmp(have, want, (size_t)len)
                                                   ? "identical to" : "DIFFERS from", path);
  free(have);
  free(want);
  free(v);
  psx_cdimage_close();
}

static int s_progress_calls, s_progress_last;
static void on_progress(int pct) {
  s_progress_calls++;
  s_progress_last = pct;
}

/* WAD.WAD is hashed on the first open with a marker, then skipped. */
static void test_marker(const char *image_dir, const char *marker) {
  double t0, t1, t2;
  FILE *f;
  remove(marker);
  psx_cd_set_verify(marker, on_progress);
  t0 = now_s();
  CHECK(psx_cd_open(image_dir) == 0);
  t1 = now_s();
  CHECK(s_progress_calls > 0 && s_progress_last == 99);
  f = fopen(marker, "rb");
  CHECK(f != NULL);
  if (f)
    fclose(f);
  s_progress_calls = 0;
  CHECK(psx_cd_open(image_dir) == 0);
  t2 = now_s();
  CHECK(s_progress_calls == 0);
  printf("image: open with the WAD.WAD check %.2f s, again with the marker %.2f s\n", t1 - t0,
         t2 - t1);
  psx_cd_set_verify(NULL, NULL);
  remove(marker);
}

/* The platform's audio sink; this test only needs SPU state. */
void psx_audio_out(const short *pcm, int frames) { (void)pcm; (void)frames; }

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
  CHECK(strcmp(psx_cd_source(), "files") == 0);
  test_cd_read(dir);
  test_cd_xa(dir);
  test_known_hashes("files");

  /* the same cases with the disc image open, against the extracted files */
  if (argc > 2) {
    const char *image = argv[2];
    if (psx_cd_open(image) != 0) {
      printf("SKIP image cases: %s: %s\n", image, psx_cd_error()->title);
    } else {
      CHECK(strcmp(psx_cd_source(), "files") != 0);
      test_image_manifest(dir, image);
      CHECK(psx_cd_open(image) == 0);
      test_cd_read(dir);
      test_cd_xa(dir);
      test_known_hashes("image");
      if (argc > 3)
        test_marker(image, argv[3]);
    }
  }

  printf(failures ? "%d failure(s)\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
