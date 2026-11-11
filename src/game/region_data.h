/* A release's data, read from the player's own executable (docs/regions-plan.md
 * §6.2, decisions D133). Nothing of the PAL or JP discs is in the repo or the
 * VPK: a committed map (region_map_<region>.c, written by
 * tools/regions/make_map.py) says where each needed block is in that
 * release's executable, which words in it are pointers, and which tables keep
 * their US shape and go over the US labels. Once psx_cd_open has checked the
 * executable's SHA-1 (D132), the loader copies the blocks into memory of its
 * own and relocates their pointers to the copies.
 *
 * The blocks' pointer words stay 4 bytes, as in the decomp's data: the game's
 * tables are PS1-shaped, and the port is 32-bit on every host (CLAUDE.md;
 * region_data.c checks it). */
#ifndef REGION_DATA_H
#define REGION_DATA_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const char *name;
  uint32_t addr, size;     /* in the release's executable, as loaded */
  const uint16_t *relocs;  /* word offsets (bytes / 4) of the pointers to relocate */
  int nrelocs;
} RegionBlock;

typedef struct {
  const char *label;       /* the US label it goes over ("D_8006EEDC") */
  uint32_t addr, size;     /* in the release's executable; size is the label's */
  int timing;              /* 1: only at the release's own timing (g_RegionTiming); PAL 60 keeps US's (D143) */
} RegionFill;

typedef struct {
  int region;              /* PSX_REGION_* */
  const char *exe_sha1;
  uint32_t exe_size;
  const RegionBlock *blocks;
  int nblocks;
  const RegionFill *fills;
  int nfills;
  void *(*fill_dst)(int i); /* fills[i]'s label: a function, since a moved arena's labels are set at start-up */
} RegionMap;

/* The map of a release, or NULL (US needs none). */
const RegionMap *region_map(int region);

typedef struct {
  int blocks, bytes, relocs, fills, fill_bytes;
  int to_strings;          /* relocated pointers into the first block */
  int strings_ok;          /* of those, aimed at a string that ends inside it */
  int not_relocated;       /* words in a block aimed at the executable's code */
} RegionDataStats;

/* Copy and relocate map's blocks from exe (size bytes, the release's whole
 * executable file, header included). fills: also copy the fill tables over
 * their US labels (the game's memory must be set up). 0, or -1 with why in err. */
int region_data_load(const RegionMap *map, const unsigned char *exe, int size, int fills,
                     RegionDataStats *stats, char *err, size_t cap);
/* The open disc's executable, through libcd, for g_Region's map. */
int region_data_load_disc(int fills, RegionDataStats *stats, char *err, size_t cap);
/* An address of the release's executable inside a loaded block: its copy, or
 * NULL. */
void *region_data_ptr(uint32_t addr);

#endif
