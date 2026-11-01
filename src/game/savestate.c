/* Save states (decisions D51): everything the game can see, written to
 * <save dir>/state<N>.bin and read back, at the top of the decomp's main
 * loop (PortPresentBeforeUpdate). There the host's C stack holds nothing of
 * the game's (main() keeps no locals), so memory alone is the game's state:
 * the 2 MB arena (all of PS1 RAM and the decomp's data labels, D23/D25),
 * scratchpad, kernel RAM, the few decomp globals outside the arena,
 * PsyCross's GPU environments and GTE registers, VRAM, the SPU, the XA
 * decoder, the CD drive, the memory card's pending result and the vblank
 * counter. A state is refused, never loaded, if it comes from another build
 * or an executable/arena at other addresses (its pointers would be wrong). */
#include "savestate.h"
#include "cheats_port.h"
#include "present.h"
#include "lod.h"

#include "psx_arena.h"
#include "psx_spu.h"
#include "psx_state.h"
#include "psx_walk.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "strings.h"

#include <gtereg.h>
#include <libgte.h>
#include <libgpu.h>

#ifndef SPYRO_BUILD_ID
#define SPYRO_BUILD_ID "unknown"
#endif

extern DRAWENV activeDrawEnv; /* PsyCross PsyX_GPU.cpp */
extern DISPENV activeDispEnv, currentDispEnv;
extern int g_GPUDisabledState, g_splitIndex;
extern void ClearSplits(void);
extern void (*vsync_callback)(void);    /* LIBETC.C */
extern void (*drawsync_callback)(void); /* LIBGPU.C */
extern int PsyX_Sys_GetVBlankCount(void);
extern unsigned char g_psx_kernel_ram[0x10000]; /* arena.c */
extern unsigned int g_mips_sp, g_mips_v1;       /* mips2c_rt.c */
extern void GamestateDraw(void);
extern char etext[], _edata[], _end[]; /* the linker's: end of text, data, bss */

#define VRAM_BYTES (1024 * 512 * 2)
#define FORMAT 1
#define SAVE_WAIT_TICKS 60 /* a save that can't be taken yet gives up after this */

typedef struct {
  char magic[8]; /* "SPYVSTAT" */
  uint32_t format, header_size;
  char build[64];
  uint32_t layout[6]; /* arena, text end, data end, bss end, two functions */
  int32_t vblank, gamestate, level;
  uint32_t nchunks;
} Header;

typedef struct {
  char tag[4];
  uint32_t len;
} ChunkHead;

/* ---------- the chunks ---------- */

static void chunk_arena(PsxStateIO *io) {
  psx_state_io(io, (void *)g_psx_arena_base, PSX_RAM_SIZE);
}

static void chunk_scratch(PsxStateIO *io) { psx_state_io(io, g_psx_scratch, sizeof g_psx_scratch); }

static void chunk_kram(PsxStateIO *io) {
  psx_state_io(io, g_psx_kernel_ram, sizeof g_psx_kernel_ram);
}

/* Decomp globals outside the arena that change (tools/savestate_globals.py
 * checks that no other one exists). */
static void chunk_globals(PsxStateIO *io) {
  psx_state_io(io, &g_mips_sp, sizeof g_mips_sp);
  psx_state_io(io, &g_mips_v1, sizeof g_mips_v1);
  port_cheats_state(io);
}

static void chunk_gpu(PsxStateIO *io) {
  psx_state_io(io, &activeDrawEnv, sizeof activeDrawEnv);
  psx_state_io(io, &activeDispEnv, sizeof activeDispEnv);
  psx_state_io(io, &currentDispEnv, sizeof currentDispEnv);
  psx_state_io(io, &g_GPUDisabledState, sizeof g_GPUDisabledState);
  psx_state_io(io, &vsync_callback, sizeof vsync_callback);
  psx_state_io(io, &drawsync_callback, sizeof drawsync_callback);
  psx_state_io(io, &gteRegs, sizeof gteRegs);
  if (io->mode == PSX_STATE_LOAD)
    ClearSplits(); /* nothing of the old timeline left to draw */
}

static void chunk_vram(PsxStateIO *io) {
  uint16_t *v = io->mode == PSX_STATE_SIZE ? NULL : savestate_plat_vram(io->mode);
  psx_state_io(io, v, VRAM_BYTES);
  if (v)
    savestate_plat_vram_done(io->mode);
}

static void chunk_spu(PsxStateIO *io) { psx_state_io(io, &g_psx_spu, sizeof g_psx_spu); }

static void chunk_plat(PsxStateIO *io) {
  int vblank = PsyX_Sys_GetVBlankCount();
  psx_state_io(io, &vblank, sizeof vblank);
  if (io->mode == PSX_STATE_LOAD)
    savestate_plat_set_vblank(vblank);
}

static const struct {
  char tag[5];
  void (*fn)(PsxStateIO *io);
} k_chunks[] = {
    {"ARNA", chunk_arena}, {"SCRP", chunk_scratch},  {"KRAM", chunk_kram},
    {"GLOB", chunk_globals}, {"GPU ", chunk_gpu},    {"VRAM", chunk_vram},
    {"SPU ", chunk_spu},   {"XA  ", psx_xa_state},   {"CD  ", psx_cd_state},
    {"MCRD", psx_mcrd_state}, {"PLAT", chunk_plat},
};
#define NCHUNKS (int)(sizeof k_chunks / sizeof k_chunks[0])

static size_t chunk_size(int i) {
  PsxStateIO io = {PSX_STATE_SIZE, NULL, 0};
  k_chunks[i].fn(&io);
  return io.n;
}

/* ---------- header ---------- */

static void make_header(Header *h) {
  memset(h, 0, sizeof *h);
  memcpy(h->magic, "SPYVSTAT", 8);
  h->format = FORMAT;
  h->header_size = sizeof *h;
  snprintf(h->build, sizeof h->build, "%s %s %s", SPYRO_BUILD_ID, __DATE__, __TIME__);
  h->layout[0] = (uint32_t)g_psx_arena_base;
  h->layout[1] = (uint32_t)(uintptr_t)etext;
  h->layout[2] = (uint32_t)(uintptr_t)_edata;
  h->layout[3] = (uint32_t)(uintptr_t)_end;
  h->layout[4] = (uint32_t)(uintptr_t)&savestate_point;
  h->layout[5] = (uint32_t)(uintptr_t)&GamestateDraw;
  h->vblank = PsyX_Sys_GetVBlankCount();
  h->gamestate = g_Gamestate;
  h->level = g_LevelId;
  h->nchunks = NCHUNKS;
}

/* 0 = usable here; -2 another build, -3 another layout, -4 not a state. */
static int check_header(const Header *h) {
  Header me;
  make_header(&me);
  if (memcmp(h->magic, me.magic, 8) || h->header_size != sizeof *h)
    return -4;
  if (h->format != me.format || strncmp(h->build, me.build, sizeof me.build))
    return -2;
  if (memcmp(h->layout, me.layout, sizeof me.layout))
    return -3;
  return 0;
}

/* ---------- files ---------- */

static char s_dir[512];
static int s_req_load, s_req_slot, s_req_ticks;
static char s_msg[64];
static int s_msg_serial;

const char *psx_mcrd_root(void); /* libmcrd.c */

void savestate_set_dir(const char *dir) { snprintf(s_dir, sizeof s_dir, "%s", dir); }

static void slot_path(int slot, const char *ext, char *out, size_t n) {
  snprintf(out, n, "%s/state%d.%s", s_dir[0] ? s_dir : psx_mcrd_root(), slot, ext);
}

static void message(const char *fmt, int slot) {
  snprintf(s_msg, sizeof s_msg, fmt, slot);
  s_msg_serial++;
  fprintf(stderr, "savestate: %s\n", s_msg);
}

const char *savestate_message(int *serial) {
  if (serial)
    *serial = s_msg_serial;
  return s_msg;
}

static unsigned char *s_buf;
static size_t s_cap;

static unsigned char *buffer(size_t n) {
  if (n > s_cap) {
    free(s_buf);
    s_buf = malloc(n);
    s_cap = s_buf ? n : 0;
  }
  return s_buf;
}

static int save(int slot) {
  char tmp[600], path[600], old[600];
  Header h;
  FILE *f;
  int i, ok = 1;
  slot_path(slot, "tmp", tmp, sizeof tmp);
  slot_path(slot, "bin", path, sizeof path);
  f = fopen(tmp, "wb");
  if (!f)
    return -1;
  make_header(&h);
  ok = fwrite(&h, sizeof h, 1, f) == 1;
  savestate_plat_audio(PSX_STATE_SAVE);
  for (i = 0; i < NCHUNKS && ok; i++) {
    ChunkHead c;
    PsxStateIO io = {PSX_STATE_SAVE, NULL, 0};
    c.len = (uint32_t)chunk_size(i);
    memcpy(c.tag, k_chunks[i].tag, 4);
    io.p = buffer(c.len);
    if (!io.p) {
      ok = 0;
      break;
    }
    k_chunks[i].fn(&io);
    ok = fwrite(&c, sizeof c, 1, f) == 1 && fwrite(io.p, 1, c.len, f) == c.len;
  }
  savestate_plat_audio_done(PSX_STATE_SAVE);
  free(s_buf); /* 2 MB not kept between saves */
  s_buf = NULL;
  s_cap = 0;
  if (fclose(f) != 0)
    ok = 0;
  if (!ok) {
    remove(tmp);
    return -1;
  }
  /* The Vita's rename doesn't replace: the old state steps aside first and
   * comes back if the new one can't take its place. */
  slot_path(slot, "old", old, sizeof old);
  remove(old);
  rename(path, old);
  if (rename(tmp, path) != 0) {
    rename(old, path);
    remove(tmp);
    return -1;
  }
  remove(old);
  return 0;
}

/* The whole file, checked; NULL with *err set if it can't be used: -1 no
 * file, -2 another build, -3 another layout, -4 damaged, -5 no memory. */
static unsigned char *read_state(int slot, size_t *len, int *err) {
  char path[600];
  FILE *f;
  long n;
  unsigned char *p;
  slot_path(slot, "bin", path, sizeof path);
  f = fopen(path, "rb");
  *err = -1;
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  n = ftell(f);
  fseek(f, 0, SEEK_SET);
  *err = -4;
  if (n < (long)sizeof(Header)) {
    fclose(f);
    return NULL;
  }
  if (!(p = malloc((size_t)n))) {
    *err = -5;
    fclose(f);
    return NULL;
  }
  if (fread(p, 1, (size_t)n, f) != (size_t)n) {
    fclose(f);
    free(p);
    return NULL;
  }
  fclose(f);
  *len = (size_t)n;
  *err = check_header((const Header *)p);
  if (*err) {
    free(p);
    return NULL;
  }
  return p;
}

/* Each chunk of the file, in k_chunks' order, with the length this build
 * reads; 0 = all there. */
static int find_chunks(const unsigned char *p, size_t len, const unsigned char **at) {
  size_t o = sizeof(Header);
  int i;
  for (i = 0; i < NCHUNKS; i++) {
    ChunkHead c;
    if (o + sizeof c > len)
      return -1;
    memcpy(&c, p + o, sizeof c);
    o += sizeof c;
    if (memcmp(c.tag, k_chunks[i].tag, 4) || c.len != chunk_size(i) || o + c.len > len)
      return -1;
    at[i] = p + o;
    o += c.len;
  }
  return 0;
}

static int load(int slot) {
  const unsigned char *at[NCHUNKS];
  size_t len;
  int err, i;
  unsigned char *p = read_state(slot, &len, &err);
  if (!p)
    return err;
  if (find_chunks(p, len, at)) {
    free(p);
    return -4;
  }
  savestate_plat_audio(PSX_STATE_LOAD);
  for (i = 0; i < NCHUNKS; i++) {
    PsxStateIO io = {PSX_STATE_LOAD, (unsigned char *)at[i], 0};
    k_chunks[i].fn(&io);
  }
  savestate_plat_audio_done(PSX_STATE_LOAD);
  port_present_reset();
  port_lod_reset();
  free(p);
  return 0;
}

int savestate_info(int slot, SaveStateInfo *info, const char **msg) {
  char path[600];
  Header h;
  FILE *f;
  int err;
  slot_path(slot, "bin", path, sizeof path);
  f = fopen(path, "rb");
  if (msg)
    *msg = "EMPTY";
  if (!f)
    return -1;
  err = fread(&h, sizeof h, 1, f) == 1 ? check_header(&h) : -4;
  fclose(f);
  if (err) {
    if (msg)
      *msg = err == -2 ? "OTHER BUILD" : err == -3 ? "OTHER LAYOUT" : "DAMAGED";
    return -2;
  }
  if (info) {
    info->vblank = h.vblank;
    info->gamestate = h.gamestate;
    info->level = h.level;
  }
  if (msg)
    *msg = "";
  return 0;
}

const char *savestate_label(const SaveStateInfo *info) {
  int world = info->level / 10 - 1, n = info->level % 10;
  if (info->gamestate == GS_TitleScreen)
    return "TITLE";
  if (info->gamestate == GS_Credits)
    return "CREDITS";
  if (world < 0 || world > 5 || n > 5)
    return "?";
  return n == 0 ? g_HomeworldNames[world] : g_LevelNames[world * 6 + n];
}

/* ---------- requests ---------- */

void savestate_request(int load, int slot) {
  if (slot < 1 || slot > SAVESTATE_SLOTS)
    return;
  s_req_load = load;
  s_req_slot = slot;
  s_req_ticks = 0;
}

int savestate_pending(void) { return s_req_slot != 0; }

void (*g_savestate_tick_hook)(void);

static void service(void) {
  int slot = s_req_slot, err;
  if (!slot)
    return;
  if (s_req_load) {
    s_req_slot = 0;
    err = load(slot);
    if (err == 0)
      message("STATE %d LOADED", slot);
    else if (err == -1)
      message("NO STATE IN SLOT %d", slot);
    else if (err == -2)
      message("STATE %d IS FROM ANOTHER BUILD", slot);
    else if (err == -3)
      message("STATE %d IS FROM ANOTHER LAYOUT", slot);
    else if (err == -5)
      message("NO MEMORY TO LOAD STATE %d", slot);
    else
      message("STATE %d IS DAMAGED", slot);
    return;
  }
  /* primitives still waiting to be drawn (a DrawPrim since the last
   * DrawOTag) aren't part of a state: wait for a tick without them */
  if (g_splitIndex > 0 && ++s_req_ticks < SAVE_WAIT_TICKS)
    return;
  s_req_slot = 0;
  if (g_splitIndex > 0)
    message("CANNOT SAVE STATE %d HERE", slot);
  else if (save(slot) == 0)
    message("STATE %d SAVED", slot);
  else
    message("CANNOT WRITE STATE %d", slot);
}

void savestate_point(void) {
  /* a state holds PsyCross's GPU state: an OT walk still running finishes
   * first (D69) */
  if (s_req_slot || g_savestate_tick_hook)
    psx_walk_join();
  service();
  if (g_savestate_tick_hook)
    g_savestate_tick_hook();
}

/* ---------- tests ---------- */

static uint32_t hash32(const unsigned char *p, size_t n, uint32_t h) {
  size_t i;
  for (i = 0; i + 4 <= n; i += 4) {
    uint32_t w;
    memcpy(&w, p + i, 4);
    h = (h ^ w) * 0x01000193u;
    h ^= h >> 15;
  }
  for (; i < n; i++)
    h = (h ^ p[i]) * 0x01000193u;
  return h;
}

/* The arena's mips2c stack (0x80010000-0x8001FFFF, port.c's EMU_STACK_TOP)
 * is hashed on its own as "STAK": at the loop top it is dead, and translated
 * code leaves host values there (host stack addresses) that differ between
 * processes without the game ever reading them. */
#define STACK_LO 0x10000u
#define STACK_HI 0x20000u

int savestate_hash(char tags[][5], uint32_t *hashes, int max) {
  int i, n = 0;
  uint32_t all = 0x811C9DC5u;
  savestate_plat_audio(PSX_STATE_SAVE);
  for (i = 0; i < NCHUNKS && n < max - 2; i++) {
    PsxStateIO io = {PSX_STATE_SAVE, NULL, 0};
    size_t len = chunk_size(i);
    io.p = buffer(len);
    if (!io.p)
      break;
    k_chunks[i].fn(&io);
    memcpy(tags[n], k_chunks[i].tag, 5);
    if (k_chunks[i].fn == chunk_arena) {
      hashes[n] = hash32(io.p + STACK_HI, len - STACK_HI, hash32(io.p, STACK_LO, 0x811C9DC5u));
      memcpy(tags[n + 1], "STAK", 5);
      hashes[n + 1] = hash32(io.p + STACK_LO, STACK_HI - STACK_LO, 0x811C9DC5u);
      n++;
    } else {
      hashes[n] = hash32(io.p, len, 0x811C9DC5u);
    }
    n++;
  }
  for (i = 0; i < n; i++)
    if (strcmp(tags[i], "STAK"))
      all = hash32((const unsigned char *)&hashes[i], 4, all);
  savestate_plat_audio_done(PSX_STATE_SAVE);
  memcpy(tags[n], "ALL ", 5);
  hashes[n++] = all;
  return n;
}
