/* The releases Spyro Vita knows (psx_region.h, decisions D132). The hashes
 * are of the files as tools/extract_disc.py writes them (its REGIONS table
 * holds the same); the WAD layouts and music names are docs/regions-plan.md
 * §2.3 and §5. PAL and JP are recognised but not run until their stages
 * land (regions plan §9). */
#include "psx_region.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int g_Region = PSX_REGION_US;
int g_RegionTiming;
int g_RegionDev;

/* Overlay bases: US from the matching build's psx.map, PAL from its
 * SetOverlayPointers and title overlay (regions plan §3.3); JP's isn't
 * measured yet (J0). */
static const PsxRegion s_regions[PSX_REGION_COUNT] = {
    {.id = PSX_REGION_US,
     .area = "USA",
     .serial = "SCUS-94228",
     .exe = "SCUS_942.28",
     .exe_sha1 = "84e3728ab94720d0873e2514adf4aade4935e0c5",
     .wad_sha1 = "ab68001377ea93fb309a5c2a35e772c9d1f57e0b",
     .wad_lba = 37,
     .wad_entries = 102,
     .music = {"PETEXA0.STR", "PETEXA1.STR", "PETEXA2.STR", "PETEXA3.STR", "PETEXA4.STR",
               "PETEXA5.STR"},
     .card = "BASCUS-94228SPYRO",
     .video_hz = 60,
     .save_dir = "",
     .supported = 1,
     .overlay_base = 0x8007AA38,
     .level_header_size = 464,
     .wad = {2, 3, 4, 7, 8, 9, 81, 82, -1, -1}},
    {.id = PSX_REGION_PAL,
     .area = "Europe",
     .serial = "SCES-01438",
     .exe = "SCES_014.38",
     .exe_sha1 = "fdd04c044cd1892ae28c43e9e619dd41a848058a",
     .wad_sha1 = "f1fc05d7cf197ba9cfc693738b44020d8798f99d",
     .wad_lba = 500,
     .wad_entries = 114,
     .music = {"MUSIC1.STR", "MUSIC2.STR", "MUSIC3.STR", "MUSIC4.STR", "MUSIC5.STR", "MUSIC6.STR"},
     .card = "BESCES-01438SPYRO",
     .video_hz = 50,
     .save_dir = "pal",
     .supported = 0,
     .overlay_base = 0x80081428,
     .level_header_size = 656,
     .wad = {2, 3, 16, 19, 20, 21, 93, 94, -1, -1}},
    {.id = PSX_REGION_JP,
     .area = "Japan",
     .serial = "SCPS-10083",
     .exe = "SCPS_100.83",
     .exe_sha1 = "3818ddb9520e2fe0114de48f5392972c33690399",
     .wad_sha1 = "812f63ff07366cf80f152ffd5bdd2e03e0582abb",
     .wad_lba = 500,
     .wad_entries = 116,
     .music = {"MUSIC1.STR", "MUSIC2.STR", "MUSIC3.STR", "MUSIC4.STR", "MUSIC5.STR", "MUSIC6.STR"},
     .card = "BISCPS-10083SPYRO",
     .video_hz = 60,
     .save_dir = "jp",
     .supported = 0,
     .overlay_base = 0,
     .level_header_size = 464,
     .wad = {3, 4, 16, 20, 21, 22, 95, 96, 2, 94}},
};

const PsxRegion *psx_region_info(int id) {
  return id >= 0 && id < PSX_REGION_COUNT ? &s_regions[id] : NULL;
}

const PsxRegion *psx_region(void) {
  const PsxRegion *r = psx_region_info(g_Region);
  return r ? r : &s_regions[PSX_REGION_US];
}

static int upper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

const PsxRegion *psx_region_by_exe(const char *exe) {
  for (int i = 0; i < PSX_REGION_COUNT; i++) {
    const char *a = s_regions[i].exe, *b = exe;
    for (; *a && upper(*a) == upper(*b); a++, b++)
      ;
    if (!*a && !*b)
      return &s_regions[i];
  }
  return NULL;
}

int psx_region_boot_exe(const char *cnf, size_t len, char *exe, size_t cap) {
  size_t i, n = 0;
  exe[0] = 0;
  for (i = 0; i + 6 <= len; i++)
    if (!strncmp(cnf + i, "cdrom:", 6))
      break;
  if (i + 6 > len || cap < 2)
    return -1;
  for (i += 6; i < len && (cnf[i] == '\\' || cnf[i] == '/'); i++)
    ;
  for (; i < len && cnf[i] && cnf[i] != ';' && cnf[i] > ' '; i++) {
    if (cnf[i] == '\\' || cnf[i] == '/')
      n = 0; /* a folder: keep the last part */
    else if (n + 1 < cap)
      exe[n++] = (char)upper(cnf[i]);
  }
  exe[n] = 0;
  return n ? 0 : -1;
}

void psx_region_serial(const char *exe, char *serial, size_t cap, const char **area) {
  size_t n = 0;
  serial[0] = 0;
  *area = "";
  for (; *exe && n + 2 < cap; exe++) {
    if (*exe != '.' && *exe != '_')
      serial[n++] = (char)upper(*exe);
    if (n == 4)
      serial[n++] = '-';
  }
  serial[n] = 0;
  if (!strncmp(serial, "SCUS", 4) || !strncmp(serial, "SLUS", 4))
    *area = "USA";
  else if (serial[0] == 'S' && serial[2] == 'E')
    *area = "PAL, Europe";
  else if (serial[0] == 'S' && (serial[2] == 'P' || serial[2] == 'J'))
    *area = "Japan";
}

char *psx_region_path(char *out, size_t cap, const char *root, const char *leaf, int mkdir_too) {
  const char *sub = psx_region()->save_dir;
  int n = snprintf(out, cap, "%s%s%s", root, *sub ? "/" : "", sub);
  if (mkdir_too && *sub && n > 0 && (size_t)n < cap)
    mkdir(out, 0777);
  if (leaf && n > 0 && (size_t)n < cap)
    snprintf(out + n, cap - (size_t)n, "/%s", leaf);
  return out;
}
