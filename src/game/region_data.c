/* The region data loader (region_data.h, decisions D133). */
#include "region_data.h"

#include "psx_cd.h"
#include "psx_cdimage.h"
#include "psx_region.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The pointer words are 4 bytes in the release's tables and in the port's
 * (the decomp's data is PS1-shaped; region_data.h). */
_Static_assert(sizeof(void *) == 4, "the region tables hold 4-byte pointers");

extern const RegionMap g_RegionMapPal; /* region_map_pal.c */

#define MAX_BLOCKS 8

static struct {
  const RegionMap *map;
  unsigned char *copy[MAX_BLOCKS];
} s_loaded;

const RegionMap *region_map(int region) {
  return region == PSX_REGION_PAL ? &g_RegionMapPal : NULL;
}

static int fail(char *err, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(char *err, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, cap, fmt, ap);
  va_end(ap);
  return -1;
}

static uint32_t rd32(const unsigned char *p) {
  return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void wr32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)v;
  p[1] = (unsigned char)(v >> 8);
  p[2] = (unsigned char)(v >> 16);
  p[3] = (unsigned char)(v >> 24);
}

static int block_of(const RegionMap *m, uint32_t addr) {
  for (int i = 0; i < m->nblocks; i++)
    if (addr - m->blocks[i].addr < m->blocks[i].size)
      return i;
  return -1;
}

static void unload(void) {
  for (int i = 0; i < MAX_BLOCKS; i++) {
    free(s_loaded.copy[i]);
    s_loaded.copy[i] = NULL;
  }
  s_loaded.map = NULL;
}

int region_data_load(const RegionMap *m, const unsigned char *exe, int size, int fills,
                     RegionDataStats *st, char *err, size_t cap) {
  RegionDataStats dummy;
  uint32_t t_addr, t_size, text_end;
  const RegionBlock *b0;
  if (!st)
    st = &dummy;
  memset(st, 0, sizeof *st);
  unload();
  if (!m || m->nblocks < 1 || m->nblocks > MAX_BLOCKS)
    return fail(err, cap, "no map for this release");
  if (size < 0x800 || memcmp(exe, "PS-X EXE", 8) != 0)
    return fail(err, cap, "not a PS-X EXE");
  if ((uint32_t)size != m->exe_size)
    return fail(err, cap, "the executable is %d bytes, the map's %u", size, (unsigned)m->exe_size);
  t_addr = rd32(exe + 0x18);
  t_size = rd32(exe + 0x1C);
  text_end = t_addr + t_size;
  if (t_size > (uint32_t)size - 0x800)
    return fail(err, cap, "the executable's header gives %u bytes of text in a %d-byte file",
                (unsigned)t_size, size);
  /* copy: every block inside the executable's text */
  for (int i = 0; i < m->nblocks; i++) {
    const RegionBlock *b = &m->blocks[i];
    if (b->addr < t_addr || b->size > text_end - b->addr) {
      unload();
      return fail(err, cap, "block %s (%08x, %u bytes) is outside the executable", b->name,
                  (unsigned)b->addr, (unsigned)b->size);
    }
    if (!(s_loaded.copy[i] = malloc(b->size + 1))) {
      unload();
      return fail(err, cap, "no memory for block %s", b->name);
    }
    memcpy(s_loaded.copy[i], exe + 0x800 + (b->addr - t_addr), b->size);
    s_loaded.copy[i][b->size] = 0; /* a string at the block's end stops there */
    st->blocks++;
    st->bytes += (int)b->size;
  }
  /* relocate: each listed word must aim inside a block */
  b0 = &m->blocks[0];
  for (int i = 0; i < m->nblocks; i++) {
    const RegionBlock *b = &m->blocks[i];
    unsigned char *c = s_loaded.copy[i];
    for (int k = 0; k < b->nrelocs; k++) {
      uint32_t off = (uint32_t)b->relocs[k] * 4, v;
      int j;
      if (off + 4 > b->size) {
        unload();
        return fail(err, cap, "block %s: pointer %u is past its end", b->name, (unsigned)off);
      }
      v = rd32(c + off);
      if ((j = block_of(m, v)) < 0) {
        unload();
        return fail(err, cap, "block %s+%u: %08x isn't in a mapped block", b->name, (unsigned)off,
                    (unsigned)v);
      }
      if (j == 0) {
        st->to_strings++;
        if (memchr(s_loaded.copy[0] + (v - b0->addr), 0, b0->addr + b0->size - v))
          st->strings_ok++;
      }
      wr32(c + off, (uint32_t)(uintptr_t)(s_loaded.copy[j] + (v - m->blocks[j].addr)));
      st->relocs++;
    }
    /* words left alone that look like pointers into the executable's code */
    if (b->nrelocs)
      for (uint32_t off = 0; off + 4 <= b->size; off += 4) {
        uint32_t v = rd32(exe + 0x800 + (b->addr - t_addr) + off);
        if (v >= t_addr && v < text_end && block_of(m, v) < 0)
          st->not_relocated++;
      }
  }
  /* the tables that keep their US shape */
  for (int i = 0; i < m->nfills; i++) {
    const RegionFill *f = &m->fills[i];
    if (f->addr < t_addr || f->size > text_end - f->addr) {
      unload();
      return fail(err, cap, "table %s (%08x) is outside the executable", f->label,
                  (unsigned)f->addr);
    }
    if (fills && (!f->timing || g_RegionTiming)) {
      void *dst = m->fill_dst(i);
      if (!dst) {
        unload();
        return fail(err, cap, "no US label %s", f->label);
      }
      memcpy(dst, exe + 0x800 + (f->addr - t_addr), f->size);
    }
    st->fills++;
    st->fill_bytes += (int)f->size;
  }
  s_loaded.map = m;
  return 0;
}

int region_data_load_disc(int fills, RegionDataStats *st, char *err, size_t cap) {
  const PsxRegion *r = psx_region();
  const RegionMap *m = region_map(r->id);
  unsigned char *exe;
  char hex[41];
  PsxSha1 sha;
  int size, rc;
  if (!m)
    return fail(err, cap, "%s needs no region data", r->serial);
  if ((size = psx_cd_read_file(r->exe, NULL, 0)) <= 0)
    return fail(err, cap, "%s isn't on the disc", r->exe);
  if (!(exe = malloc((size_t)size)))
    return fail(err, cap, "no memory for %s", r->exe);
  if (psx_cd_read_file(r->exe, exe, size) != size) {
    free(exe);
    return fail(err, cap, "%s can't be read", r->exe);
  }
  psx_sha1_init(&sha);
  psx_sha1_update(&sha, exe, (unsigned)size);
  psx_sha1_hex(&sha, hex);
  if (strcmp(hex, m->exe_sha1) != 0) {
    free(exe);
    return fail(err, cap, "%s has SHA-1 %.8s, the map is for %.8s", r->exe, hex, m->exe_sha1);
  }
  rc = region_data_load(m, exe, size, fills, st, err, cap);
  free(exe);
  return rc;
}

void *region_data_ptr(uint32_t addr) {
  const RegionMap *m = s_loaded.map;
  int i;
  if (!m || (i = block_of(m, addr)) < 0)
    return NULL;
  return s_loaded.copy[i] + (addr - m->blocks[i].addr);
}
