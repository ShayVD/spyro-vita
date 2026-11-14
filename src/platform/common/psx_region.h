/* The disc's region (docs/regions-plan.md §7, decisions D132): one table of
 * the releases Spyro Vita knows, by the executable SYSTEM.CNF boots and its
 * SHA-1. psx_cd_open (libcd.c, cdimage.c) finds the region before the game
 * runs and sets g_Region once; nothing changes it afterwards.
 *
 * Only addresses, sizes, names and hashes are here: never the discs' data. */
#ifndef PSX_REGION_H
#define PSX_REGION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PSX_REGION_US, PSX_REGION_PAL, PSX_REGION_JP, PSX_REGION_COUNT };

typedef struct {
  int id;              /* PSX_REGION_* */
  const char *area;    /* "USA", "Europe", "Japan": "Spyro the Dragon (Europe)" */
  const char *serial;  /* "SCES-01438" */
  const char *exe;     /* SYSTEM.CNF's BOOT file, "SCES_014.38" */
  const char *exe_sha1;
  const char *wad_sha1;
  int wad_lba;         /* the sector WadInitialize reads the header from */
  int wad_entries;     /* OffsetLength entries in that header (8 bytes each) */
  const char *music[6]; /* the XA music files */
  const char *card;    /* the memory-card file the game names */
  int video_hz;        /* 60 (NTSC) or 50 (PAL) */
  const char *save_dir; /* under the save folder: "" (US, the folder itself), "pal", "jp" */
  int supported;       /* 0: recognised, refused with "not supported yet" (JP; US, PAL 1) */
  /* the game's layout (regions plan §2.3, §3.4; D133) */
  unsigned overlay_base;  /* where a level's overlay is loaded; 0: not measured yet */
  int level_header_size;  /* LevelHeader's bytes: 464, PAL 656 (m_Dragons[5][6]) */
  struct {                /* WAD.WAD header entries; -1: none */
    int title_overlay, cutscenes, ncutscenes, gameover_skybox, pete, levels, credits_overlay,
        credits_data, font, credits_text;
  } wad;
} PsxRegion;

/* PSX_REGION_*: the open disc's. PSX_REGION_US until psx_cd_open finds
 * another, which it does before the game's first instruction. */
extern int g_Region;
/* 1 when RT()'s PAL values apply (include/region.h, D133): the PAL disc at
 * its own 50 Hz, a developer's switch since D150. 0 for US and JP, and for
 * PAL as players run it. */
extern int g_RegionTiming;
/* PAL at its own 50 Hz (the host's --pal50, the Vita's pal50=1; developers
 * only, D150), set before psx_cd_open. Without it the PAL disc runs with
 * every RT() site at its US value, at the US 60 Hz vblank ("PAL 60",
 * regions plan §7.4, D138): PAL's text, voices and picture at NTSC speed,
 * which since D150 is how players run it. The PAL attract demos match the
 * PS1 tick for tick only with it (D149). */
extern int g_RegionPal50;

/* The game's vblank rate, from g_RegionTiming: 50 (PAL at its own timing) or
 * 60. The CD drive, the SPU's samples a vblank and the Vita's real-time clock
 * follow it (regions plan §6.3, D138). */
int psx_region_vblank_hz(void);

/* Developers only (host --region-dev, D133): psx_cd_open opens a release
 * that isn't supported yet (JP; PAL is supported since D154) instead of
 * refusing it, so later stages can be built against it. Never on for
 * players. */
extern int g_RegionDev;

const PsxRegion *psx_region_info(int id); /* NULL past the table */
const PsxRegion *psx_region(void);        /* g_Region's */
/* The region whose executable is named exe (case ignored), or NULL. */
const PsxRegion *psx_region_by_exe(const char *exe);
/* The region whose serial a PBP's disc ID is ("SCUS94228" for SCUS-94228;
 * case ignored), or NULL (D157). */
const PsxRegion *psx_region_by_disc_id(const char *disc_id);
/* The release a disc is: the one whose executable SYSTEM.CNF boots (boot_exe
 * from psx_region_boot_exe, or "" / NULL without one), else the first whose
 * executable has_file(user, name) finds on it, else NULL. The one rule for
 * the image, the extracted files and the digital copy (D158). */
const PsxRegion *psx_region_of_disc(const char *boot_exe, int (*has_file)(void *user, const char *name),
                                    void *user);

/* SYSTEM.CNF's BOOT file ("cdrom:\SCES_014.38;1" -> "SCES_014.38", any
 * folder dropped, upper case), from its text. 0, or -1 without one. */
int psx_region_boot_exe(const char *cnf, size_t len, char *exe, size_t cap);
/* A boot file's serial ("SCES_014.38" -> "SCES-01438") and the area its
 * prefix says ("USA", "PAL, Europe", "Japan" or ""). */
void psx_region_serial(const char *exe, char *serial, size_t cap, const char **area);

/* root with the region's save folder: root itself for the US disc, root/pal
 * and root/jp for the others (regions plan §7.2), then /leaf if leaf isn't
 * NULL. mkdir_too creates the region's folder (not root). Returns out. */
char *psx_region_path(char *out, size_t cap, const char *root, const char *leaf, int mkdir_too);

#ifdef __cplusplus
}
#endif

#endif
