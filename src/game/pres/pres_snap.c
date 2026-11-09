/* The presenter's snapshot (stage 7e, decisions D125; pres_snap.h).
 *
 * Ordinary game code: it reads the game's state by the game's names and
 * writes the presenter's copies by theirs (g_pres_snap_*, g_pres_* of
 * render_ctx.h), which the presenter's objects read. */
#include "pres_snap.h"
#include "pres_draw.h"

#include "present.h"
#include "render_ctx.h"
#ifndef RENDER_CTX_READABLE
#define RENDER_CTX_READABLE 0 /* before 7d2 */
#endif
#include "psx_arena.h"

#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <stddef.h>
#include <string.h>

#include "buffers.h"
#include "camera.h"
#include "common.h"
#include "graphics.h"
#include "hud.h"
#include "moby.h"
#include "renderers.h"
#include "spyro.h"

#define DATA_LO 0x8006BBE0u
#define MAX_MOBYS 1024
#define PARTICLE_SPACE 0x2000
#define OT_BYTES 0x4000
/* the area's first 2 MB (render_ctx.h PRES_SECTORS_AT): the buffers'
 * mirror from its start, the mobys and the particles above it */
#define AREA_MOBYS 0x100000u
#define AREA_PARTICLES 0x180000u

int g_pres_audit;
PresSnapRegion g_pres_regions[PRES_REGIONS];

#if !SPYRO_PRESENTER
/* the moved arena (cmake/presenter.cmake): no presenter */
void pres_snap_from_live(void) {}
int pres_snap_take(int blend) { return (void)blend, 0; }
uint32_t pres_snap_rebase(uint32_t a) { return a; }
void pres_snap_audit_report(void (*log)(const char *fmt, ...)) { (void)log; }
#else

/* the presenter's copies the snapshot sets by name (pres_snap_names.h;
 * render_ctx.h; lod_pres.c) */
extern Camera g_pres_snap_g_Camera;
extern Spyro g_pres_snap_g_Spyro;
extern Moby *g_pres_snap_g_LevelMobys;
extern DB *g_pres_snap_g_CurDB;
extern DB g_pres_snap_g_DB[2];
extern Moby *g_pres_level_mobys;
extern char D_8006FCF4[], D_80077DD8[];

static unsigned char *game_labels(void) { return (unsigned char *)PSX_ARENA(DATA_LO); }

void pres_snap_from_live(void) {
  const unsigned char *g = game_labels();
  int i;
  for (i = 0; i < g_pres_snap_nlabels; i++)
    memcpy(g_pres_mirror + g_pres_snap_labels[i].off, g + g_pres_snap_labels[i].off,
           g_pres_snap_labels[i].len);
}

static int in_arena(const void *p, uint32_t len) {
  uintptr_t a = (uintptr_t)p;
  return a >= g_psx_arena_base && len <= PSX_MEM_SIZE && a - g_psx_arena_base <= PSX_MEM_SIZE - len;
}

static void region(int k, const void *lo, uint32_t len, uintptr_t to) {
  g_pres_regions[k].lo = (uint32_t)(uintptr_t)lo;
  g_pres_regions[k].len = len;
  g_pres_regions[k].to = (uint32_t)to;
}

/* A game address in a region moved onto its copy; a region's end too (a
 * buffer's end pointer). */
uint32_t pres_snap_rebase(uint32_t a) {
  int k;
  for (k = 0; k < PRES_REGIONS; k++) {
    const PresSnapRegion *r = &g_pres_regions[k];
    if (r->len && a - r->lo < r->len)
      return a - r->lo + r->to;
  }
  /* an end, which isn't another's start (the particle space begins where
   * the world OT ends) */
  for (k = 0; k < PRES_REGIONS; k++) {
    const PresSnapRegion *r = &g_pres_regions[k];
    if (r->len && a - r->lo == r->len)
      return a - r->lo + r->to;
  }
  return a;
}
#define REBASE_PTR(p) ((p) = (void *)(uintptr_t)pres_snap_rebase((uint32_t)(uintptr_t)(p)))

static int count_mobys(void) {
  int n = 0;
  if (!in_arena(g_LevelMobys, sizeof(Moby)))
    return 0;
  while (n < MAX_MOBYS && (signed char)g_LevelMobys[n].m_State != -1)
    n++;
  return n;
}

/* ---- --pres-audit: which words of the copies point into the mobys or
 * the particles ---- */
enum { AUDIT_MAX = 256 };
static struct {
  const char *where; /* a label's name, "moby", "particle" */
  uint32_t off;      /* in the label, or in a moby or particle */
  int to;            /* PRES_REGION_MOBYS or _PARTICLES */
  unsigned long n;
} s_audit[AUDIT_MAX];
static unsigned long s_audit_snaps, s_audit_lost;

static uint32_t s_work_lo = ~0u, s_work_hi; /* the work area's hits, as a range */
static unsigned long s_work_n;
static void audit_hit(const char *where, uint32_t off, int to) {
  int i;
  if (where[0] == 'D' && !strncmp(where, "D_8006FCF4", 10)) {
    s_work_lo = off < s_work_lo ? off : s_work_lo;
    s_work_hi = off > s_work_hi ? off : s_work_hi;
    s_work_n++;
    return;
  }
  for (i = 0; i < AUDIT_MAX && s_audit[i].where; i++)
    if (s_audit[i].where == where && s_audit[i].off == off && s_audit[i].to == to) {
      s_audit[i].n++;
      return;
    }
  if (i == AUDIT_MAX) {
    s_audit_lost++;
    return;
  }
  s_audit[i].where = where;
  s_audit[i].off = off;
  s_audit[i].to = to;
  s_audit[i].n = 1;
}

static void audit_words(const char *where, const unsigned char *p, uint32_t len, uint32_t stride) {
  uint32_t i;
  for (i = 0; i + 4 <= len; i += 4) {
    uint32_t w;
    int k;
    memcpy(&w, p + i, 4);
    for (k = PRES_REGION_MOBYS; k <= PRES_REGION_PARTICLES; k++)
      if (g_pres_regions[k].len && w - g_pres_regions[k].lo < g_pres_regions[k].len)
        audit_hit(where, stride ? i % stride : i, k);
  }
}

static void audit(int nmobys) {
  const unsigned char *g = game_labels();
  int i;
  s_audit_snaps++;
  for (i = 0; i < g_pres_snap_nlabels; i++)
    audit_words(g_pres_snap_labels[i].name, g + g_pres_snap_labels[i].off, g_pres_snap_labels[i].len, 0);
  if (nmobys)
    audit_words("moby", (const unsigned char *)g_LevelMobys, (uint32_t)nmobys * sizeof(Moby), sizeof(Moby));
  if (in_arena(g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE))
    audit_words("particle space", g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE, 0x20);
  /* the renderers' copies (render_ctx.h) the presenter takes from the game */
  audit_words("D_8006FCF4 (work area)", (const unsigned char *)D_8006FCF4, PRES_WORK, 0);
  audit_words("g_Glows", (const unsigned char *)g_Glows, PRES_GLOWS, 0x24);
  audit_words("g_Sparkles", (const unsigned char *)g_Sparkles, PRES_SPARKLES, 0x18);
  audit_words("g_SpyroFlame", (const unsigned char *)&g_SpyroFlame, PRES_FLAME, 0);
  audit_words("g_MobyShadows", (const unsigned char *)&g_MobyShadows, PRES_SHADOWS, 0);
#if !RENDER_CTX_READABLE
  audit_words("D_80077DD8 (spill area)", (const unsigned char *)D_80077DD8, PRES_SPILL, 0);
#endif
}

void pres_snap_audit_report(void (*log)(const char *fmt, ...)) {
  int i;
  if (!log)
    return;
  log("pres audit: %lu snapshots; words pointing into the moby array or the particle space:", s_audit_snaps);
  for (i = 0; i < AUDIT_MAX && s_audit[i].where; i++)
    log("pres audit:   %s +0x%X -> %s: %lu", s_audit[i].where, s_audit[i].off,
        s_audit[i].to == PRES_REGION_MOBYS ? "mobys" : "particles", s_audit[i].n);
  if (s_work_n)
    log("pres audit:   D_8006FCF4 (the work area, the presenter's own: its draw rebuilds it) "
        "+0x%X..+0x%X: %lu", s_work_lo, s_work_hi, s_work_n);
  if (s_audit_lost)
    log("pres audit:   (%lu more places not listed)", s_audit_lost);
}

/* ---- the snapshot ---- */

/* n words, stride bytes apart from p, that point into the moby array or
 * the particle space moved onto the copies (the others left as they are:
 * a glow may follow a position in the level's data) */
void pres_snap_fields(unsigned char *p, uint32_t n, uint32_t stride) {
  uint32_t i;
  for (i = 0; i < n; i++, p += stride) {
    uint32_t w;
    int k;
    memcpy(&w, p, 4);
    for (k = PRES_REGION_MOBYS; k <= PRES_REGION_PARTICLES; k++)
      if (g_pres_regions[k].len && w - g_pres_regions[k].lo < g_pres_regions[k].len) {
        w = w - g_pres_regions[k].lo + g_pres_regions[k].to;
        memcpy(p, &w, 4);
        break;
      }
  }
}

int pres_snap_take(int blend) {
  const uintptr_t area = render_ctx_area();
  const unsigned char *g = game_labels();
  int i, nmobys;
  uint32_t buf_lo = 0, buf_len = 0;
  if (!area)
    return 0;
  memset(g_pres_regions, 0, sizeof g_pres_regions);
  /* the regions: the buffers (lower buffer to the world OT's end), the
   * mobys (up to the one that ends the array), the particle space */
  if (in_arena(g_Buffers.m_LowerPolyBuffer, 1) && in_arena(g_Buffers.m_WorldOTStart, OT_BYTES) &&
      (uintptr_t)g_Buffers.m_WorldOTStart > (uintptr_t)g_Buffers.m_LowerPolyBuffer) {
    buf_lo = (uint32_t)(uintptr_t)g_Buffers.m_LowerPolyBuffer;
    buf_len = (uint32_t)((uintptr_t)g_Buffers.m_WorldOTStart + OT_BYTES - buf_lo);
    if (buf_len <= AREA_MOBYS)
      region(PRES_REGION_BUFFERS, g_Buffers.m_LowerPolyBuffer, buf_len, area);
    else
      buf_len = 0;
  }
  nmobys = count_mobys();
  if (nmobys || in_arena(g_LevelMobys, sizeof(Moby)))
    region(PRES_REGION_MOBYS, g_LevelMobys, (uint32_t)(nmobys + 1) * sizeof(Moby), area + AREA_MOBYS);
  if (in_arena(g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE))
    region(PRES_REGION_PARTICLES, g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE, area + AREA_PARTICLES);
  if (g_pres_audit)
    audit(nmobys);
  /* the labels; then the mirror is a region too, for pointers between
   * labels (g_CurDB into g_DB) */
  for (i = 0; i < g_pres_snap_nlabels; i++)
    memcpy(g_pres_mirror + g_pres_snap_labels[i].off, g + g_pres_snap_labels[i].off,
           g_pres_snap_labels[i].len);
  /* the copies */
  if (g_pres_regions[PRES_REGION_MOBYS].len)
    memcpy((void *)(area + AREA_MOBYS), g_LevelMobys, g_pres_regions[PRES_REGION_MOBYS].len);
  if (g_pres_regions[PRES_REGION_PARTICLES].len)
    memcpy((void *)(area + AREA_PARTICLES), g_Buffers.m_ParticleSpaceStart, PARTICLE_SPACE);
  /* the presenter's OTs start empty, as the game's do (func_80016784
   * clears every bucket it links) */
  if (buf_len)
    memset((void *)(area + ((uint32_t)(uintptr_t)g_Buffers.m_HudOTStart - buf_lo)), 0,
           (uint32_t)((uintptr_t)g_Buffers.m_WorldOTStart + OT_BYTES - (uintptr_t)g_Buffers.m_HudOTStart));
  /* the pointers into the copies (the audit, --pres-audit, found which;
   * D125 has its table) */
  REBASE_PTR(g_pres_snap_g_LevelMobys);
  pres_snap_fields((unsigned char *)&g_pres_snap_g_Camera + 0xD0, 1, 0);     /* the camera's moby */
  pres_snap_fields((unsigned char *)&g_pres_snap_g_Spyro + 0x21C, 1, 0);     /* the moby Spyro uses */
  if (g_pres_regions[PRES_REGION_MOBYS].len) /* each moby's next in its collision chain */
    pres_snap_fields((unsigned char *)(area + AREA_MOBYS) + 0x4, (uint32_t)nmobys, sizeof(Moby));
  if (g_pres_regions[PRES_REGION_PARTICLES].len) /* each particle's moby */
    pres_snap_fields((unsigned char *)(area + AREA_PARTICLES) + 0x18, PARTICLE_SPACE / 0x20, 0x20);
  for (i = 0; i < 2; i++) {
    REBASE_PTR(g_pres_snap_g_DB[i].m_PolyBuf);
    REBASE_PTR(g_pres_snap_g_DB[i].m_WorldOT);
    REBASE_PTR(g_pres_snap_g_DB[i].m_HudOT);
  }
  region(PRES_REGION_MIRROR, game_labels(), g_pres_mirror_len, (uintptr_t)g_pres_mirror);
  REBASE_PTR(g_pres_snap_g_CurDB);
  g_pres_regions[PRES_REGION_MIRROR].len = 0;
  g_pres_level_mobys = g_pres_snap_g_LevelMobys;
  /* the particle list's pointer is the renderers' (render_ctx.h) */
  {
    uint32_t p = (uint32_t)(uintptr_t)g_Particles;
    p = pres_snap_rebase(p);
    memcpy(g_pres_particles, &p, 4);
  }
  if (!blend)
    return 1;
  return port_present_blend_to(&g_pres_snap_g_Camera, &g_pres_snap_g_Spyro, g_pres_snap_g_LevelMobys,
                               g_pres_regions[PRES_REGION_PARTICLES].len
                                   ? (void *)(uintptr_t)pres_snap_rebase((uint32_t)(uintptr_t)g_Particles)
                                   : NULL);
}
#endif
