/* Extract the game's files from a PS1 Classic's EBOOT.PBP (the digital copy
 * from the PlayStation Store, as installed on a Vita in
 * ux0:pspemu/PSP/GAME/<ID>/ or on a PSP), the way tools/extract_disc.py
 * does from a disc image (decisions D157). The port's own reader
 * (src/platform/common/pbp.c, psx_pbp.h) does the work. The game doesn't
 * need this: it reads the EBOOT.PBP in place, as a disc (cdimage.c). The
 * tool is for players who want the files, and for the tests.
 *
 *   pbp_extract EBOOT.PBP OUT_DIR     writes OUT_DIR/manifest.json and the files
 *   pbp_extract --info EBOOT.PBP      what the PBP is, nothing written
 *   pbp_extract --sectors EBOOT.PBP LBA COUNT OUT.bin   raw sectors, for tests
 *   pbp_extract --bench EBOOT.PBP     decode every block, print the speed (D158)
 *
 * Built with the host debug build as build/linux32/tools/pbp_extract
 * (tools/CMakeLists.txt). */
#include "psx_pbp.h"
#include "psx_region.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static unsigned long long now_us(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (unsigned long long)ts.tv_sec * 1000000u + (unsigned long long)ts.tv_nsec / 1000u;
}

/* every sector in order, as a load would read them: the decoder's speed with
 * and without the EDC pass */
static int bench(const char *path) {
  PsxPbp *p;
  PsxPbpError err;
  unsigned char *buf = malloc(128 * PSX_PBP_SECTOR);
  int edc, lba, total;
  if (!buf || psx_pbp_open(&p, path, &err) != 0) {
    fprintf(stderr, "error: %s\n", buf ? err.text : "out of memory");
    free(buf);
    return 1;
  }
  psx_pbp_set_clock(p, now_us);
  total = psx_pbp_sectors(p);
  for (edc = 0; edc < 2; edc++) {
    PsxPbpStats st;
    unsigned long long t0 = now_us(), t;
    psx_pbp_set_edc(p, edc);
    for (lba = 0; lba < total; lba += 128)
      if (psx_pbp_read_sectors(p, lba, total - lba < 128 ? total - lba : 128, buf) != 0) {
        fprintf(stderr, "error: sectors %d.. can't be read\n", lba);
        psx_pbp_close(p);
        free(buf);
        return 1;
      }
    t = now_us() - t0;
    psx_pbp_stats(p, &st);
    printf("edc %s: %d sectors (%.1f MB raw) in %.2f s: %.1f MB/s; decode %.2f s, %u file reads of %.1f MB in %.2f s\n",
           edc ? "on " : "off", total, (double)total * PSX_PBP_SECTOR / 1048576.0, t / 1e6,
           (double)total * PSX_PBP_SECTOR / 1048576.0 / (t / 1e6), st.decode_us / 1e6, st.reads,
           st.read_bytes / 1048576.0, st.read_us / 1e6);
  }
  psx_pbp_close(p);
  free(buf);
  return 0;
}

static void progress(void *user, int percent, const char *file) {
  (void)user;
  if (percent % 5 == 0)
    fprintf(stderr, "\r%3d%%  %-40s", percent, file);
}

int main(int argc, char **argv) {
  PsxPbp *p;
  PsxPbpError err;
  int region;
  if (argc == 3 && strcmp(argv[1], "--info") == 0) {
    if (psx_pbp_open(&p, argv[2], &err) != 0) {
      fprintf(stderr, "error: %s\n", err.text);
      return 1;
    }
    printf("disc id %s, title \"%s\", %s, %d blocks of %d sectors (%d sectors, %.1f MB raw)\n",
           psx_pbp_disc_id(p), psx_pbp_title(p), psx_pbp_encrypted(p) ? "PGD-encrypted header (a Sony copy)" : "plain header",
           psx_pbp_sectors(p) / PSX_PBP_BLOCK_SECTORS, PSX_PBP_BLOCK_SECTORS, psx_pbp_sectors(p),
           (double)psx_pbp_sectors(p) * PSX_PBP_SECTOR / 1048576.0);
    psx_pbp_close(p);
    return 0;
  }
  if (argc == 3 && strcmp(argv[1], "--bench") == 0)
    return bench(argv[2]);
  if (argc == 6 && strcmp(argv[1], "--sectors") == 0) {
    long lba = strtol(argv[3], NULL, 10), count = strtol(argv[4], NULL, 10);
    unsigned char *buf;
    FILE *f;
    int r = 1, total;
    if (psx_pbp_open(&p, argv[2], &err) != 0) {
      fprintf(stderr, "error: %s\n", err.text);
      return 1;
    }
    /* the range against the image before any size is computed: at most its
     * sectors (under 800 MB of them), so count * 2352 can't wrap a size_t */
    total = psx_pbp_sectors(p);
    if (lba < 0 || count <= 0 || lba >= total || count > total - lba) {
      fprintf(stderr, "error: %ld sectors from %ld aren't in the image (it holds 0..%d)\n", count, lba, total - 1);
      psx_pbp_close(p);
      return 1;
    }
    buf = malloc((size_t)count * PSX_PBP_SECTOR);
    if (!buf) {
      fprintf(stderr, "error: out of memory for %ld sectors\n", count);
      psx_pbp_close(p);
      return 1;
    }
    psx_pbp_set_edc(p, 1); /* as a dump's sectors */
    if (psx_pbp_read_sectors(p, (int)lba, (int)count, buf) != 0) {
      fprintf(stderr, "error: sectors %ld..%ld can't be read\n", lba, lba + count - 1);
    } else if (!(f = fopen(argv[5], "wb"))) {
      fprintf(stderr, "error: %s can't be written\n", argv[5]);
    } else {
      size_t wrote = fwrite(buf, PSX_PBP_SECTOR, (size_t)count, f);
      r = (fclose(f) == 0 && wrote == (size_t)count) ? 0 : 1;
      if (r)
        fprintf(stderr, "error: %s can't be written\n", argv[5]);
    }
    psx_pbp_close(p);
    free(buf);
    return r;
  }
  if (argc != 3) {
    fprintf(stderr, "usage: pbp_extract EBOOT.PBP OUT_DIR | --info EBOOT.PBP | --sectors EBOOT.PBP LBA COUNT OUT.bin | --bench EBOOT.PBP\n");
    return 2;
  }
  if (psx_pbp_open(&p, argv[1], &err) != 0) {
    fprintf(stderr, "error: %s\n", err.text);
    return 1;
  }
  fprintf(stderr, "%s: disc id %s, \"%s\", %s\n", argv[1], psx_pbp_disc_id(p), psx_pbp_title(p),
          psx_pbp_encrypted(p) ? "a Sony copy (PGD header)" : "plain header");
  if (psx_pbp_extract(p, argv[2], &region, progress, NULL, &err) != 0) {
    fprintf(stderr, "\nerror: %s\n", err.text);
    psx_pbp_close(p);
    return 1;
  }
  fprintf(stderr, "\n%s and WAD.WAD match a good dump of %s (%s). Copy everything in %s (manifest.json too) to ux0:data/spyro/.\n",
          psx_region_info(region)->exe, psx_region_info(region)->serial, psx_region_info(region)->area, argv[2]);
  psx_pbp_close(p);
  return 0;
}
