/* Collision (asm/collision.s): rays, spheres and floor probes against the
 * level's collision triangles and the mobys' collision models, written by
 * hand from the asm so they read as C (docs/native-plan.md stage 3,
 * decisions D83). Replaces the mips2c unit (cmake/mips2c.cmake
 * M2C_HAND_MULTI); checked against PCSX-Redux with the oracle replay and
 * against the unit with differential sweeps (docs/translation-log.md).
 *
 * The asm keeps its working state in the scratchpad (the cells to visit,
 * the bounds of the ray or sphere, a moby's transformed vertices, the best
 * hit so far) and does its vector maths on the GTE. Here those are locals
 * and integer maths, and neither the scratchpad nor the GTE's registers
 * are touched: nothing reads what the asm left there (D83). The GTE
 * commands' arithmetic is kept exactly: a GTE register holds 16 bits
 * (S16), products are 32-bit, sums 64-bit and the MAC their low 32 bits.
 *
 * The asm's add and sub trap on signed overflow; the game never reaches
 * one, and the C assumes none.
 *
 * M2C_<name>=1 (cmake SPYRO_M2C_FUNCS) takes that function from the mips2c
 * unit instead, linked as <name>_gen, for A/B tests. */
#include <assert.h> /* PsyCross headers use C11 static_assert */

#include "region.h"
#include "common.h"

#include "collision.h"
#include "environment.h"
#include "gte_int.h"
#include "moby.h"
#include "psx_arena.h"
#include "psx_mem.h"
#include "spyro.h"
#include "variables.h"
#include "vector.h"

extern char D_8006FCF4[];   /* ColSphere's contact list (a shared work area) */
extern int D_80075774;      /* Contacts in that list after the last ColSphere */
extern Moby *D_800757E4;    /* The moby hit last, NULL for the terrain */
extern Vector3D D_80077858; /* The point ColSphere pushed away from */

/* The mips2c unit's entries, linked as <name>_gen for A/B tests and the
 * shadow below (cmake M2C_HAND_MULTI). */
u_int ColTraceRay_gen();
u_int ColSphere_gen();
u_int ColFindFloor_gen();
u_int func_8004DF24_gen();
u_int ColSpyroInHitbox_gen();
u_int ColMoby_gen();

/* Oracle replays and differential sweeps (host builds with the mips2c
 * unit linked, cmake SPYRO_M2C_REF): with g_col_shadow set, each call also
 * runs the unit from the same state first, then the hand C; RAM and the
 * result must match (mismatches are printed and counted in
 * g_col_shadow_fail), and the call leaves the scratchpad and the GTE as
 * the unit left them, so a sample's whole state still compares with the
 * emulator's (decisions D83). */
#define COL_SHADOW (MIPS2C_REPLAY && M2C_REF)
#if COL_SHADOW
#include "mips2c_rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int g_col_shadow, g_col_shadow_fail;
static int s_in_shadow;
static u_char s_before[PSX_RAM_SIZE], s_gen[PSX_RAM_SIZE];
static GTERegisters s_gte_before, s_gte_gen;
static u32 s_sp;

static int ShadowBegin(void) {
  if (!g_col_shadow || s_in_shadow)
    return 0;
  s_in_shadow = 1;
  s_sp = g_mips_sp;
  memcpy(s_before, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  s_gte_before = gteRegs;
  return 1;
}

static void ShadowSwap(void) {
  memcpy(s_gen, (void *)g_psx_arena_base, PSX_RAM_SIZE);
  s_gte_gen = gteRegs;
  memcpy((void *)g_psx_arena_base, s_before, PSX_RAM_SIZE);
  gteRegs = s_gte_before;
  g_mips_sp = s_sp;
}

/* RAM the comparison skips: the scratchpad (the arena's first KB, which
 * the unit's state replaces), the asm's register spill area, and the
 * emulated stack below the call (the unit's argument copies). */
static int ShadowSkip(u32 off) {
  return off < 0x10000u || (off >= 0x77DD8u && off < 0x77EA0u) ||
         (off < s_sp - PSX_RAM_BASE && off >= s_sp - PSX_RAM_BASE - 0x10000u);
}

static int ShadowEnd(const char *name, int gen, int hand) {
  const u32 *h = (const u32 *)g_psx_arena_base, *g = (const u32 *)s_gen;
  u32 i;
  int shown = 0;
  for (i = 0; i < PSX_RAM_SIZE / 4; i++)
    if (h[i] != g[i] && !ShadowSkip(i * 4)) {
      g_col_shadow_fail++;
      if (shown++ < 8)
        printf("  colshadow %s ram %08x unit %08x hand %08x\n", name, PSX_RAM_BASE + i * 4, g[i], h[i]);
    }
  if (gen != hand) {
    g_col_shadow_fail++;
    printf("  colshadow %s result unit %08x hand %08x\n", name, gen, hand);
  }
  memcpy((void *)g_psx_arena_base, s_gen, 0x400);
  gteRegs = s_gte_gen;
  s_in_shadow = 0;
  return hand;
}

#define SHADOW(name, gen_call, hand_call)                                     \
  do {                                                                         \
    if (ShadowBegin()) {                                                       \
      int gen_ = (int)(gen_call);                                              \
      ShadowSwap();                                                            \
      return ShadowEnd(#name, gen_, hand_call);                               \
    }                                                                          \
  } while (0)

/* Differential fuzzing (SPYRO_COL_FUZZ=N with the shadow on): before a
 * call, N variants of it with the arguments jittered (positions by up to
 * 16, 256 or 4096 units per axis, radii, sizes, flags), each run through
 * the unit and the hand C from the same state and compared as above, then
 * the state put back. Deterministic: the jitter is seeded from the
 * arguments. */
static int s_fuzz = -1;
static u_int s_rng;

static int FuzzCount(u_int seed) {
  if (s_fuzz < 0)
    s_fuzz = getenv("SPYRO_COL_FUZZ") ? atoi(getenv("SPYRO_COL_FUZZ")) : 0;
  s_rng = seed * 2654435761u + 1;
  return g_col_shadow && !s_in_shadow ? s_fuzz : 0;
}

static u_int FuzzRand(void) {
  s_rng = s_rng * 1103515245u + 12345u;
  return s_rng >> 8;
}

static int FuzzRange(void) {
  static const int ranges[3] = {16, 256, 4096};
  return ranges[FuzzRand() % 3];
}

static Vector3D FuzzNear(const Vector3D *v) {
  int r = FuzzRange();
  Vector3D o = *v;
  o.x += (int)(FuzzRand() % (2 * r + 1)) - r;
  o.y += (int)(FuzzRand() % (2 * r + 1)) - r;
  o.z += (int)(FuzzRand() % (2 * r + 1)) - r;
  return o;
}

static int FuzzRadius(int r) {
  switch (FuzzRand() % 4) {
  case 0:
    return r;
  case 1:
    return r / 2;
  case 2:
    return r * 2;
  default:
    return 0x40 + FuzzRand() % 0x400;
  }
}

/* One variant: the unit, then the hand C, from the same state, compared;
 * then the state from before both. */
#define FUZZ_ONE(name, gen_call, hand_call)                                    \
  do {                                                                         \
    int gen_;                                                                  \
    ShadowBegin();                                                             \
    gen_ = (int)(gen_call);                                                    \
    ShadowSwap();                                                              \
    ShadowEnd("fuzz " #name, gen_, hand_call);                                \
    memcpy((void *)g_psx_arena_base, s_before, PSX_RAM_SIZE);                  \
    gteRegs = s_gte_before;                                                    \
  } while (0)
#else
#define SHADOW(name, gen_call, hand_call) ((void)0)
#endif

/* ===================================================================== */
/* Integer versions of what the asm does on the GTE and in MIPS registers */
/* (gte_int.h), and collision's own                                      */
/* ===================================================================== */

/* One component of the same (the asm reads one MAC by the normal's
 * largest axis). */
static inline int CrossAxis(const Vector3D *d, const Vector3D *e, int axis) {
  Vector3D r = Cross(d, e);
  return axis == 0 ? r.x : axis == 1 ? r.y : r.z;
}

/* The axis of the largest of a normal's components, and that component:
 * the asm's `(a + b) ^ (a - b) < 0`, which holds when |b| > |a|. */
static inline int MainAxis(const Vector3D *n, int *value) {
  int axis = 0, m = n->x;
  if (((m + n->y) ^ (m - n->y)) < 0)
    m = n->y, axis = 1;
  if (((m + n->z) ^ (m - n->z)) < 0)
    m = n->z, axis = 2;
  *value = m;
  return axis;
}

static inline int AxisOf(const Vector3D *v, int axis) { return axis == 0 ? v->x : axis == 1 ? v->y : v->z; }

/* ===================================================================== */
/* The mobys' collision grid and models                                   */
/* ===================================================================== */

/* g_MobyCollisionChain + 0x1000 holds a 32 x 32 grid of 8192-unit cells,
 * each the head of a list of the mobys in it (m_CollisionChainNext).
 * NearMobyChains gives the non-empty ones of the cell of (x, y) and the
 * three beside it towards which (x, y) lies in its cell (bit 0x1000), in
 * the asm's order. A neighbour index with bits above 0x3FF set (positive)
 * is skipped; one of -1 is read, as the asm does. When the x neighbour is
 * skipped, the asm's test for the y side still holds that index's high
 * bits (not 0), so the y neighbour is always the cell after. */
#define MAX_CHAINS 4

static int NearMobyChains(int x, int y, Moby **heads) {
  Moby **grid = (Moby **)((u_char *)g_MobyCollisionChain + 0x1000);
  int cell = ((u_int)x >> 13) + (((u_int)y >> 13) << 5);
  int dx = x & 0x1000 ? 1 : -1, dy, n = 0;
  if (grid[cell])
    heads[n++] = grid[cell];
  if (((cell + dx) & -0x400) > 0) {
    dy = 0x20;
  } else {
    if (grid[cell + dx])
      heads[n++] = grid[cell + dx];
    dy = y & 0x1000 ? 0x20 : -0x20;
  }
  if (!(((cell + dy) & -0x400) > 0) && grid[cell + dy])
    heads[n++] = grid[cell + dy];
  if (!(((cell + dx + dy) & -0x400) > 0) && grid[cell + dx + dy])
    heads[n++] = grid[cell + dx + dy];
  return n;
}

/* A moby's collision model (m_CollisionGroup): its bounding radius, its
 * vertex count, then triangles (one word each: the vertex indices of A, B
 * and C in bits 24-29, 16-21 and 8-13, a surface byte, negative at the
 * end). The loader stores it with bit 31 clear (loaders.c,
 * PatchMobyModelPointers); the asm ORs it back, as here. */
typedef struct {
  u_short m_Radius;
  u_short m_VertexCount;
  int m_Triangles[1];
} CollisionModel;

static inline const CollisionModel *MobyCollisionModel(const Moby *m) {
  return (const CollisionModel *)MEM_COLL_PTR(m->m_CollisionGroup);
}

#define TRI_VERTEX_A(w) (((w) >> 24) & 0x3F)
#define TRI_VERTEX_B(w) (((w) >> 16) & 0x3F)
#define TRI_VERTEX_C(w) (((w) >> 8) & 0x3F)

/* The asm transforms a model's vertices into the scratchpad from 0x80 on,
 * 16 bytes each, and indexes them with 6 bits; 56 fit before its end. */
#define MAX_MODEL_VERTICES 64

/* RTPS (sf 1, no translation) on a vertex, read back as IR1-3: the moby's
 * rotation times V0, >> 12, clamped to 16 bits. */
static inline int RotRow(const short *row, int vx, int vy, int vz) {
  int v = (int)(((long long)(row[0] * vx) + row[1] * vy + row[2] * vz) >> 12);
  return v > 0x7FFF ? 0x7FFF : v < -0x8000 ? -0x8000 : v;
}

/* A vertex of a moby's current frame in world space. The frame word packs
 * z (bits 21-31), x (bits 11-21) and y (bits 0-11, doubled), each signed
 * and shifted up by the animation's scale. The asm loads V0 with VXY0 = x + (y << 16)
 * without masking x, so a negative x borrows one from y. The rotated
 * vertex's axes map to the world's as (IR3, -IR1, -IR2). */
static Vector3D MobyVertex(const Moby *m, u_int word, int scale) {
  int vz = S16(Shl((int)word >> 21, scale));
  int x = Shl((int)(word << 10) >> 21, scale);
  int y = Shl((int)(word << 20) >> 19, scale);
  int vxy = (int)((u_int)x + ((u_int)y << 16));
  int vx = S16(vxy), vy = S16(vxy >> 16);
  const SHORTMATRIX *r = &m->m_RotationMatrix;
  Vector3D v;
  v.x = m->m_Position.x + RotRow(r->m[2], vx, vy, vz);
  v.y = m->m_Position.y - RotRow(r->m[0], vx, vy, vz);
  v.z = m->m_Position.z - RotRow(r->m[1], vx, vy, vz);
  return v;
}

/* The words of a moby's current animation frame (the 21-bit offset in
 * RAM that AnimationFrame packs) and the animation's scale. */
static const u_int *MobyFrameWords(const Moby *m, int *scale) {
  const AnimationHeader *anim = g_Models[(u_short)m->m_Class]->m_Animations[m->m_AnimationState.m_Animation];
  *scale = anim->m_Scale;
  return (const u_int *)MEM_ANIM21(anim->m_Frames[m->m_AnimationState.m_Frame].m.m_Data);
}

/* All the vertices of a moby's collision model, as the asm's RTPS loop
 * leaves them in the scratchpad. */
static void MobyVertices(const Moby *m, const CollisionModel *model, Vector3D *out) {
  int scale, i, n = model->m_VertexCount;
  const u_int *words = MobyFrameWords(m, &scale);
  if (n > MAX_MODEL_VERTICES)
    n = MAX_MODEL_VERTICES;
  for (i = 0; i < n; i++)
    out[i] = MobyVertex(m, words[i], scale);
}

/* Whether all three of a, b, c are below lo or all above hi: the asm's
 * `(a - lo) & (b - lo) & (c - lo) < 0`. */
static inline int AllOutside(int a, int b, int c, int lo, int hi) {
  return ((a - lo) & (b - lo) & (c - lo)) < 0 || ((hi - a) & (hi - b) & (hi - c)) < 0;
}

/* ===================================================================== */
/* The level's collision triangles                                        */
/* ===================================================================== */

/* TerrainCollision.m_BlockTree: per level a u16 count, then that many
 * u16 byte offsets from the tree's start (0xFFFF: empty), z, then y, then
 * x by 4096-unit cells; the x level's entries index m_Blocks, lists of
 * triangle indices whose first entry has bit 15 set. */
static const u_short *CellTriangles(const TerrainCollision *tc, int cx, int cy, int cz) {
  const u_short *tree = tc->m_BlockTree, *node = tree;
  int c[3], i;
  c[0] = cz, c[1] = cy, c[2] = cx;
  for (i = 0; i < 3; i++) {
    u_int child;
    if (c[i] < 0 || node[0] - c[i] <= 0)
      return NULL;
    child = node[1 + c[i]];
    if (child == 0xFFFF)
      return NULL;
    if (i < 2)
      node = (const u_short *)((const u_char *)tree + child);
    else
      return tc->m_Blocks + child;
  }
  return NULL;
}

/* A triangle record (m_Triangles, 12 bytes): per axis a word with A's
 * coordinate in 16ths (bits 0-13) and the offsets to C (bits 23-31) and B
 * (bits 14-22), signed 9-bit; for z the offsets are unsigned bytes (C
 * bits 24-31, B bits 16-23) and bit 14 marks a triangle collision skips. */
#define TRI_A(w) ((int)((w) & 0x3FFF))
#define TRI_C(w) ((int)(w) >> 23)
#define TRI_B(w) ((int)((w) << 9) >> 23)
#define TRI_CZ(w) ((int)((u_int)(w) >> 24))
#define TRI_BZ(w) ((int)(((w) >> 16) & 0xFF))
#define TRI_NO_COLLISION 0x4000

/* A box in 16ths, lo inclusive and hi exclusive: (p >> 4) .. (q >> 4) + 1. */
typedef struct {
  int lo[3], hi[3];
} Box16;

/* Whether a triangle's box misses the box (the asm's tests, axis by
 * axis: for x and y, all of A, B and C at or above hi with one above, or
 * all below lo; for z, A above hi or both B and C below lo). */
static int TriangleOutside(const u_int *w, const Box16 *b) {
  int i, a;
  a = TRI_A(w[2]) - b->hi[2];
  if (a > 0)
    return 1;
  a = TRI_A(w[2]) - b->lo[2];
  if (((a + TRI_CZ(w[2])) & (a + TRI_BZ(w[2]))) < 0)
    return 1;
  for (i = 1; i >= 0; i--) {
    a = TRI_A(w[i]) - b->hi[i];
    if ((a | (a + TRI_B(w[i])) | (a + TRI_C(w[i]))) > 0)
      return 1;
    a = TRI_A(w[i]) - b->lo[i];
    if ((a & (a + TRI_B(w[i])) & (a + TRI_C(w[i]))) < 0)
      return 1;
  }
  return 0;
}

/* A triangle in world units: A, C - A and B - A. */
static void TerrainTriangle(const u_int *w, Vector3D *a, Vector3D *ca, Vector3D *ba) {
  a->x = TRI_A(w[0]) << 4, a->y = TRI_A(w[1]) << 4, a->z = TRI_A(w[2]) << 4;
  ca->x = TRI_C(w[0]) << 4, ca->y = TRI_C(w[1]) << 4, ca->z = TRI_CZ(w[2]) << 4;
  ba->x = TRI_B(w[0]) << 4, ba->y = TRI_B(w[1]) << 4, ba->z = TRI_BZ(w[2]) << 4;
}

/* The surface flags of terrain triangle i (0xFF past the flag table). */
static inline int TerrainSurface(const TerrainCollision *tc, int i) {
  return tc->m_FlagCount - i > 0 ? tc->m_Flags[i] : 0xFF;
}

/* A moby triangle's surface: 0xFF if its low byte says so, else the
 * first word of the moby's properties. */
static inline int MobySurface(const Moby *m, int word) {
  return (word & 0xFF) == 0xFF ? 0xFF : *(const int *)m->m_Props;
}

/* The cells from cell a to cell b, at most one step apart per axis (the
 * asm visits only these two per axis, however far apart a and b are): x
 * varies fastest, then y, then z, except that with all three different the
 * second half has y before x. The asm stores them as halfwords, so each
 * coordinate keeps its low 16 bits (and one past 0x7FFF is skipped as
 * negative); it visits them last to first. */
static int CellList(const int *a, const int *b, int (*cells)[3]) {
  static const signed char all8[8][3] = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
                                         {0, 0, 1}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};
  int xs = a[0] != b[0], ys = a[1] != b[1], zs = a[2] != b[2], n = 0, i, j, k;
  for (k = 0; k <= zs; k++)
    for (j = 0; j <= ys; j++)
      for (i = 0; i <= xs; i++, n++) {
        const signed char *o = xs && ys && zs ? all8[n] : NULL;
        cells[n][0] = S16((o ? o[0] : i) ? b[0] : a[0]);
        cells[n][1] = S16((o ? o[1] : j) ? b[1] : a[1]);
        cells[n][2] = S16((o ? o[2] : k) ? b[2] : a[2]);
      }
  return n;
}

#if COL_SHADOW
/* For the fuzzing: a point on or near the collision model of a moby in
 * the grid cells around v (picked at random among those that have one),
 * else v jittered, so that variants reach the moby paths too. */
static Vector3D FuzzAtMoby(const Vector3D *v) {
  Moby *heads[MAX_CHAINS], *pick = NULL, *m;
  int n = 0, seen = 0, i, r;
  Vector3D o;
  if ((v->x | v->y | v->z) >= 0)
    n = NearMobyChains(v->x, v->y, heads);
  for (i = 0; i < n; i++)
    for (m = heads[i]; m; m = m->m_CollisionChainNext)
      if (m->m_CollisionGroup && FuzzRand() % ++seen == 0)
        pick = m;
  if (!pick)
    return FuzzNear(v);
  r = MobyCollisionModel(pick)->m_Radius + 1;
  o = pick->m_Position;
  o.x += (int)(FuzzRand() % (2 * r + 1)) - r;
  o.y += (int)(FuzzRand() % (2 * r + 1)) - r;
  o.z += (int)(FuzzRand() % (2 * r + 1)) - r;
  return o;
}
#endif

/* ===================================================================== */
/* ColTraceRay                                                            */
/* ===================================================================== */

#define NO_HIT 0x40000000

/* The asm's ray test against one triangle (A, C - A, B - A in world
 * units): the ray from `from` along d must cross the triangle's plane
 * from its front to its back, at a point inside the triangle closer to
 * `from` (squared) than *best. On a hit, *best is that distance, *n the
 * normal and *hit the point. */
static int RayTriangle(const Vector3D *from, const Vector3D *d, const Vector3D *a, const Vector3D *ca,
                       const Vector3D *ba, int *best, Vector3D *n, Vector3D *hit) {
  Vector3D c = Cross(ca, ba), r0, r1, p, h, hc, bc;
  int num, den, k, dist2, m, axis, e;
  n->x = c.x >> 10, n->y = c.y >> 10, n->z = c.z >> 10;
  r0 = Sub(from, a);
  r1 = Add(&r0, d);
  if (Dot(&r1, n) >= 0 || Dot(&r0, n) < 0)
    return 0;
  /* Where along d: (r0 . n) / -(d . n), normalised for the division. */
  num = Dot(&r0, n);
  den = Dot(d, n);
  k = LeadingSignBits(num) - 2;
  num = Shl(num, k);
  k -= 12;
  den = k >= 0 ? Shl(den, k) : Sar(den, -k);
  p = Scale(MipsDiv(num, den), d, 12);
  h = Sub(&r0, &p);
  dist2 = LengthSq(&p);
  if (dist2 - *best >= 0)
    return 0;
  /* Inside: each edge's cross product with the point, on the normal's
   * main axis, is 0 or of the other sign. */
  axis = MainAxis(n, &m);
  e = CrossAxis(&h, ca, axis);
  if (e && (e ^ m) >= 0)
    return 0;
  e = CrossAxis(ba, &h, axis);
  if (e && (e ^ m) >= 0)
    return 0;
  hc = Sub(&h, ca);
  bc = Sub(ba, ca);
  e = CrossAxis(&hc, &bc, axis);
  if (e && (e ^ m) >= 0)
    return 0;
  *best = dist2;
  *hit = Add(a, &h);
  return 1;
}

/* The ray against the mobys in the given chains: each moby whose collision
 * model's radius reaches within the ray's length (|dx| + |dy| + |dz|) of
 * the start, then each of its triangles that overlaps the ray's box. */
static int TraceMobys(const Vector3D *from, const Vector3D *to, Moby **heads, int nheads) {
  Vector3D d = Sub(to, from), v[MAX_MODEL_VERTICES], n, hit;
  int lo[3], hi[3], f[3] = {from->x, from->y, from->z}, t[3] = {to->x, to->y, to->z};
  int best = NO_HIT, len, i;
  for (i = 0; i < 3; i++) {
    lo[i] = f[i] - t[i] > 0 ? t[i] : f[i];
    hi[i] = f[i] - t[i] > 0 ? f[i] : t[i];
  }
  len = (d.x < 0 ? -d.x : d.x) + (d.y < 0 ? -d.y : d.y) + (d.z < 0 ? -d.z : d.z);
  for (i = 0; i < nheads; i++) {
    Moby *m;
    for (m = heads[i]; m; m = m->m_CollisionChainNext) {
      const CollisionModel *model;
      const int *tri;
      Vector3D rel = Sub(&m->m_Position, from);
      int reach;
      if (!m->m_CollisionGroup)
        continue;
      model = MobyCollisionModel(m);
      reach = model->m_Radius + len;
      if (LengthSq(&rel) - reach * reach >= 0)
        continue;
      MobyVertices(m, model, v);
      for (tri = model->m_Triangles; *tri >= 0; tri++) {
        const Vector3D *a = &v[TRI_VERTEX_A(*tri)], *b = &v[TRI_VERTEX_B(*tri)], *c = &v[TRI_VERTEX_C(*tri)];
        Vector3D ca, ba;
        if (AllOutside(c->x, b->x, a->x, lo[0], hi[0]) || AllOutside(c->y, b->y, a->y, lo[1], hi[1]) ||
            AllOutside(c->z, b->z, a->z, lo[2], hi[2]))
          continue;
        ca = Sub(c, a);
        ba = Sub(b, a);
        if (!RayTriangle(from, &d, a, &ca, &ba, &best, &n, &hit))
          continue;
        g_CollisionPoint = hit;
        g_CollisionNormal = n;
        D_800757E4 = m;
        g_CollisionTriangleIndex = -1;
      }
    }
  }
  return best != NO_HIT;
}

/* The ray against the level: the triangles of the cells of its two ends
 * (in 4096ths) that overlap its box (in 16ths). */
static int TraceTerrain(const Vector3D *from, const Vector3D *to) {
  const TerrainCollision *tc = g_Environment.m_TerrainCollision;
  const u_int *triangles = tc->m_Triangles;
  Vector3D d = Sub(to, from), n, hit;
  int f[3] = {from->x, from->y, from->z}, t[3] = {to->x, to->y, to->z};
  int ca_[3], cb_[3], cells[8][3], ncells, best = NO_HIT, i;
  Box16 box;
  for (i = 0; i < 3; i++) {
    int lo = f[i], hi = t[i];
    if (f[i] - t[i] > 0)
      lo = t[i], hi = f[i];
    box.lo[i] = (u_int)lo >> 4;
    box.hi[i] = ((u_int)hi >> 4) + 1;
    ca_[i] = (u_int)f[i] >> 12;
    cb_[i] = (u_int)t[i] >> 12;
  }
  ncells = CellList(ca_, cb_, cells);
  while (ncells-- > 0) {
    const u_short *list = CellTriangles(tc, cells[ncells][0], cells[ncells][1], cells[ncells][2]);
    int idx;
    if (!list)
      continue;
    for (idx = *list++ & 0x7FFF; idx >= 0; idx = (short)*list++) {
      const u_int *w = triangles + idx * 3;
      Vector3D a, ca, ba;
      if (TriangleOutside(w, &box) || (w[2] & TRI_NO_COLLISION))
        continue;
      TerrainTriangle(w, &a, &ca, &ba);
      if (!RayTriangle(from, &d, &a, &ca, &ba, &best, &n, &hit))
        continue;
      g_CollisionPoint = hit;
      g_CollisionNormal = n;
      D_800757E4 = NULL;
      g_CollisionTriangleIndex = idx;
    }
  }
  return best != NO_HIT;
}

#if !M2C_ColTraceRay
static int TraceRay(Vector3D *point1, Vector3D *point2) {
  Moby *heads[MAX_CHAINS];
  int n;
  if ((point1->x | point1->y | point1->z | point2->x | point2->y | point2->z) < 0)
    return 0;
  n = NearMobyChains(point1->x, point1->y, heads);
  if (n && TraceMobys(point1, point2, heads, n))
    return 1;
  return TraceTerrain(point1, point2);
}

/* Whether the segment from point1 to point2 hits a moby's collision model
 * or else the level: the nearest hit, with g_CollisionPoint, its normal,
 * D_800757E4 (the moby or NULL) and g_CollisionTriangleIndex (-1 for a
 * moby). Mobys are tried only near point1, and if one is hit the level
 * isn't tried. */
int ColTraceRay(Vector3D *point1, Vector3D *point2) {
#if COL_SHADOW
  for (int i = FuzzCount(point1->x ^ point2->y << 8 ^ point2->z); i > 0; i--) {
    int mode = (int)(FuzzRand() % 3);
    Vector3D a = FuzzNear(point1), b = mode == 0 ? FuzzNear(point2) : mode == 1 ? FuzzNear(point1) : FuzzAtMoby(point1);
    FUZZ_ONE(ColTraceRay, ColTraceRay_gen(&a, &b), TraceRay(&a, &b));
  }
#endif
  SHADOW(ColTraceRay, ColTraceRay_gen(point1, point2), TraceRay(point1, point2));
  return TraceRay(point1, point2);
}
#else
int ColTraceRay(Vector3D *point1, Vector3D *point2) { return ColTraceRay_gen(point1, point2); }
#endif

/* ===================================================================== */
/* ColSphere                                                              */
/* ===================================================================== */

/* One entry of ColSphere's contact list: a triangle's point nearest the
 * centre and its distance squared. */
typedef struct {
  Vector3D m_Point;
  int m_DistanceSq;
} SphereContact;

typedef struct {
  const Vector3D *pos;
  int radius;
  int best;       /* The nearest contact's distance squared */
  Vector3D point; /* and its point */
  SphereContact *contacts;
} SphereQuery;

/* The point of the segment from p0 to p1 nearest q (all relative to the
 * triangle's A): the asm works in 16ths here (p0 and p1 >> 4, so a moby
 * edge loses its low bits), projects q onto the segment and clamps to its
 * ends. */
static Vector3D NearestOnEdge(Vector3D p0, Vector3D p1, const Vector3D *q) {
  Vector3D e = Sub(&p1, &p0), t, d;
  int num, den, s, axis, m;
  num = Shl(Dot(q, &e), 4);
  den = Dot(&e, &e);
  t = Scale(MipsDiv(num, den), &e, 4);
  p0.x <<= 4, p0.y <<= 4, p0.z <<= 4;
  p1.x <<= 4, p1.y <<= 4, p1.z <<= 4;
  d = Sub(&p1, &p0);
  axis = MainAxis(&d, &m);
  s = AxisOf(&t, axis);
  if ((m ^ s) < 0) {
    t.x = t.y = t.z = 0;
  } else if (((m - s) ^ (m + s)) < 0) {
    t = d;
  }
  return Add(&t, &p0);
}

/* The asm's sphere test against one triangle (A, C - A, B - A in world
 * units): the sphere's centre must be in front of the triangle's plane
 * and within the radius of it; then the triangle's point nearest the
 * centre (the foot of the perpendicular when it is inside, else the
 * nearest edge or corner, picked by which edges the foot is outside of)
 * must be within the radius. */
static int SphereTriangle(const SphereQuery *q, const Vector3D *a, const Vector3D *ca, const Vector3D *ba,
                          Vector3D *n, Vector3D *point, int *dist2) {
  Vector3D c = Cross(ca, ba), rel = Sub(q->pos, a), p, foot, fc, bc, d;
  int r2 = q->radius * q->radius, m, axis, inside;
  n->x = c.x >> 10, n->y = c.y >> 10, n->z = c.z >> 10;
  if (Dot(&rel, n) <= 0)
    return 0;
  p = Scale(MipsDiv(Shl(Dot(&rel, n), 4), Dot(n, n) >> 6), n, 10);
  foot = Sub(&rel, &p);
  if (r2 - LengthSq(&p) <= 0)
    return 0;
  /* Bit k: inside edge k (A to C, A to B, C to B), the sign of the edge's
   * cross product with the foot on the normal's main axis against the
   * normal's. */
  axis = MainAxis(n, &m);
  fc = Sub(&foot, ca);
  bc = Sub(ba, ca);
  inside = (int)((u_int)(CrossAxis(&foot, ca, axis) ^ m) >> 31);
  inside |= (int)((u_int)(CrossAxis(ba, &foot, axis) ^ m) >> 31) << 1;
  inside |= (int)((u_int)(CrossAxis(&fc, &bc, axis) ^ m) >> 31) << 2;
  switch (inside) {
  case 0: /* (no point is outside all three) */
  case 1:
    *point = Add(ba, a); /* B */
    break;
  case 2:
    *point = Add(ca, a); /* C */
    break;
  case 3: { /* B to C */
    Vector3D b16 = Shr(ba, 4), c16 = Shr(ca, 4), b0 = {b16.x << 4, b16.y << 4, b16.z << 4};
    Vector3D fb = Sub(&foot, &b0);
    Vector3D e = NearestOnEdge(b16, c16, &fb);
    *point = Add(&e, a);
    break;
  }
  case 4:
    *point = *a;
    break;
  case 5: { /* A to B */
    Vector3D zero = {0, 0, 0}, e = NearestOnEdge(zero, Shr(ba, 4), &foot);
    *point = Add(&e, a);
    break;
  }
  case 6: { /* A to C */
    Vector3D zero = {0, 0, 0}, e = NearestOnEdge(zero, Shr(ca, 4), &foot);
    *point = Add(&e, a);
    break;
  }
  default: /* inside: the foot */
    *point = Add(&foot, a);
    break;
  }
  d = Sub(q->pos, point);
  *dist2 = LengthSq(&d);
  return r2 - *dist2 > 0;
}

/* A contact: recorded in the list, and kept if nearer than the best. */
static int SphereContactFound(SphereQuery *q, const Vector3D *point, int dist2) {
  q->contacts->m_Point = *point;
  q->contacts->m_DistanceSq = dist2;
  q->contacts++;
  if (q->best - dist2 <= 0)
    return 0;
  q->best = dist2;
  q->point = *point;
  return 1;
}

static void SphereMobys(SphereQuery *q, Moby **heads, int nheads) {
  const Vector3D *pos = q->pos;
  int r = q->radius, i;
  Vector3D v[MAX_MODEL_VERTICES], n, point;
  for (i = 0; i < nheads; i++) {
    Moby *m;
    for (m = heads[i]; m; m = m->m_CollisionChainNext) {
      const CollisionModel *model;
      const int *tri;
      Vector3D rel = Sub(&m->m_Position, pos);
      int reach, dist2;
      if (!m->m_CollisionGroup)
        continue;
      model = MobyCollisionModel(m);
      reach = model->m_Radius + r;
      if (LengthSq(&rel) - reach * reach >= 0)
        continue;
      MobyVertices(m, model, v);
      for (tri = model->m_Triangles; *tri >= 0; tri++) {
        const Vector3D *a = &v[TRI_VERTEX_A(*tri)], *b = &v[TRI_VERTEX_B(*tri)], *c = &v[TRI_VERTEX_C(*tri)];
        Vector3D ca, ba;
        if (AllOutside(c->x, b->x, a->x, pos->x - r, pos->x + r) ||
            AllOutside(c->y, b->y, a->y, pos->y - r, pos->y + r) ||
            AllOutside(c->z, b->z, a->z, pos->z - r, pos->z + r))
          continue;
        ca = Sub(c, a);
        ba = Sub(b, a);
        if (!SphereTriangle(q, a, &ca, &ba, &n, &point, &dist2) || !SphereContactFound(q, &point, dist2))
          continue;
        g_CollisionNormal = n;
        D_800757E4 = m;
        g_CollisionTriangleIndex = -1;
        g_SurfaceBelowFlags = MobySurface(m, *tri);
      }
    }
  }
}

static void SphereTerrain(SphereQuery *q) {
  const TerrainCollision *tc = g_Environment.m_TerrainCollision;
  const u_int *triangles = tc->m_Triangles;
  int p[3] = {q->pos->x, q->pos->y, q->pos->z}, r = q->radius;
  int ca_[3], cb_[3], cells[8][3], ncells, i, dist2;
  Vector3D n, point;
  Box16 box;
  for (i = 0; i < 3; i++) {
    box.hi[i] = ((u_int)(p[i] + r) >> 4) + 1;
    box.lo[i] = box.hi[i] - (int)(((u_int)r >> 3) + 1);
    ca_[i] = (u_int)p[i] >> 12;
    cb_[i] = (u_int)(p[i] + r) >> 12;
    if (cb_[i] == ca_[i])
      cb_[i] = (u_int)(p[i] - r) >> 12;
  }
  ncells = CellList(ca_, cb_, cells);
  while (ncells-- > 0) {
    const u_short *list = CellTriangles(tc, cells[ncells][0], cells[ncells][1], cells[ncells][2]);
    int idx;
    if (!list)
      continue;
    for (idx = *list++ & 0x7FFF; idx >= 0; idx = (short)*list++) {
      const u_int *w = triangles + idx * 3;
      Vector3D a, ca, ba;
      if (TriangleOutside(w, &box) || (w[2] & TRI_NO_COLLISION))
        continue;
      TerrainTriangle(w, &a, &ca, &ba);
      if (!SphereTriangle(q, &a, &ca, &ba, &n, &point, &dist2) || !SphereContactFound(q, &point, dist2))
        continue;
      g_CollisionNormal = n;
      D_800757E4 = NULL;
      g_CollisionTriangleIndex = idx;
      g_SurfaceBelowFlags = TerrainSurface(tc, idx);
    }
  }
}

/* Push the centre out of the nearest contact: g_CollisionPoint becomes
 * the contact plus the direction to the centre scaled to at least r,
 * dividing by the table root of the distance squared and then by one less
 * each round until it is long enough. */
static void SpherePushOut(const SphereQuery *q, int r) {
  Vector3D d = Sub(q->pos, &q->point), push;
  int root = TableSqrt(q->best);
  D_80077858 = q->point;
  do {
    Vector3D v = Scale(r, &d, 0);
    push.x = MipsDiv(v.x, root);
    push.y = MipsDiv(v.y, root);
    push.z = MipsDiv(v.z, root);
    root--;
  } while (LengthSq(&push) - r * r < 0);
  g_CollisionPoint = Add(&q->point, &push);
}

#if !M2C_ColSphere
static int Sphere(Vector3D *pPos, int pRadiusMobys, int pRadiusTerrain) {
  SphereQuery q;
  Moby *heads[MAX_CHAINS];
  int n, result = 0;
  q.pos = pPos;
  q.radius = pRadiusMobys;
  q.best = pRadiusMobys * pRadiusMobys;
  q.point.x = q.point.y = q.point.z = 0;
  q.contacts = (SphereContact *)D_8006FCF4;
  if (pPos->z - pRadiusMobys >= 0 && pPos->y - pRadiusMobys >= 0 && pPos->x - pRadiusMobys >= 0) {
    n = NearMobyChains(pPos->x, pPos->y, heads);
    if (n) {
      SphereMobys(&q, heads, n);
      q.radius = pRadiusTerrain;
    }
    if (n && q.best != 0 && q.best - pRadiusTerrain * pRadiusTerrain < 0) {
      result = 1;
    } else {
      SphereTerrain(&q);
      result = q.best != 0 && q.best - pRadiusTerrain * pRadiusTerrain < 0;
    }
    if (result)
      SpherePushOut(&q, pRadiusTerrain);
  }
  D_80075774 = (u_int)((u_char *)q.contacts - (u_char *)D_8006FCF4) >> 4;
  return result;
}

/* Whether a sphere at pPos touches a moby's collision model (radius
 * pRadiusMobys) or else the level: every triangle within reach is listed
 * in D_8006FCF4 (D_80075774 of them); the nearest sets g_CollisionNormal,
 * D_800757E4, g_CollisionTriangleIndex and g_SurfaceBelowFlags, and if it
 * is within pRadiusTerrain, g_CollisionPoint is where the centre should
 * be pushed to. The level is tried with pRadiusMobys when no moby is
 * near, else with pRadiusTerrain; a moby contact within that radius ends
 * the test. Every caller passes the same radius twice; with the moby
 * radius the smaller and no moby contact, the asm would push from a
 * point a previous call left in the scratchpad. */
int ColSphere(Vector3D *pPos, int pRadiusMobys, int pRadiusTerrain) {
#if COL_SHADOW
  for (int i = FuzzCount(pPos->x ^ pPos->y << 8 ^ pRadiusMobys); i > 0; i--) {
    Vector3D a = FuzzRand() & 1 ? FuzzNear(pPos) : FuzzAtMoby(pPos);
    int rm = FuzzRadius(pRadiusMobys), rt = FuzzRand() & 3 ? rm : rm - (int)(FuzzRand() % (rm + 1));
    FUZZ_ONE(ColSphere, ColSphere_gen(&a, rm, rt), Sphere(&a, rm, rt));
  }
#endif
  SHADOW(ColSphere, ColSphere_gen(pPos, pRadiusMobys, pRadiusTerrain), Sphere(pPos, pRadiusMobys, pRadiusTerrain));
  return Sphere(pPos, pRadiusMobys, pRadiusTerrain);
}
#else
int ColSphere(Vector3D *pPos, int pRadiusMobys, int pRadiusTerrain) {
  return ColSphere_gen(pPos, pRadiusMobys, pRadiusTerrain);
}
#endif

/* ===================================================================== */
/* ColFindFloor and func_8004DF24                                         */
/* ===================================================================== */

/* The height of a triangle's plane at (px, py), in the units the
 * triangle is given in, if the triangle faces up and (px, py) is inside
 * it seen from above (the asm's OPs, of which only MAC3 is read: the z of
 * each cross product). shift scales the normal: 2 in 16ths, 10 in world
 * units. */
static int FloorHeight(const Vector3D *a, const Vector3D *ca, const Vector3D *ba, int px, int py, int shift,
                       Vector3D *n, int *height) {
  Vector3D c = Cross(ca, ba), p, bc, pc;
  int s;
  if (c.z <= 0)
    return 0;
  n->x = c.x >> shift, n->y = c.y >> shift, n->z = c.z >> shift;
  p.x = px - a->x, p.y = py - a->y, p.z = 0;
  if (Cross(ca, &p).z < 0 || Cross(&p, ba).z < 0)
    return 0;
  bc = Sub(ba, ca);
  pc = Sub(&p, ca);
  if (Cross(&bc, &pc).z < 0)
    return 0;
  s = (int)((0u - (u_int)p.x * (u_int)n->x - (u_int)p.y * (u_int)n->y) << 4);
  *height = a->z + (MipsDiv(s, n->z) >> 4);
  return 1;
}

/* Whether a, b or c is at p or the three aren't all on one side of it:
 * the asm's test per axis, from A towards C, then B. */
static inline int Straddles(int a, int c, int b, int p) {
  int d = a - p;
  if (!d)
    return 1;
  if (d < 0)
    return c - p >= 0 || b - p >= 0;
  return c - p <= 0 || b - p <= 0;
}

/* The level's floor below (x, y, z) in 16ths, highest first, from cell
 * cz down to cell `last`; *floor rises to each floor found that is above
 * it and below z. Stops after the first cell where *floor rose. With
 * `camera` (func_8004DF24) the triangles marked no-collision count too,
 * and the occlusion group of the floor is kept instead of the collision
 * globals. */
static void FloorTerrain(int x, int y, int z, int last, int *floor, int camera) {
  const TerrainCollision *tc = g_Environment.m_TerrainCollision;
  const u_int *triangles = tc->m_Triangles;
  int start = *floor, cz;
  for (cz = z >> 8; cz != last; cz--) {
    const u_short *list;
    int idx;
    if (camera ? *floor > 0 : *floor != start)
      break;
    list = CellTriangles(tc, x >> 8, y >> 8, cz);
    if (!list)
      continue;
    for (idx = *list++ & 0x7FFF; idx >= 0; idx = (short)*list++) {
      const u_int *w = triangles + idx * 3;
      Vector3D a, ca, ba, n;
      int height, below;
      a.x = TRI_A(w[0]), a.y = TRI_A(w[1]), a.z = TRI_A(w[2]);
      ca.x = TRI_C(w[0]), ca.y = TRI_C(w[1]), ca.z = TRI_CZ(w[2]);
      ba.x = TRI_B(w[0]), ba.y = TRI_B(w[1]), ba.z = TRI_BZ(w[2]);
      if (!Straddles(a.x, a.x + ca.x, a.x + ba.x, x) || !Straddles(a.y, a.y + ca.y, a.y + ba.y, y))
        continue;
      if (a.z - z > 0)
        continue;
      below = *floor - a.z;
      if (((below - ca.z) | (below - ba.z)) > 0)
        continue;
      if (!camera && (w[2] & TRI_NO_COLLISION))
        continue;
      if (!FloorHeight(&a, &ca, &ba, x, y, 2, &n, &height) || z - height <= 0 || *floor - height >= 0)
        continue;
      *floor = height;
      if (camera) {
        if (REGION_IS(PAL)) /* PAL keeps the floor's normal too (PAL 0x800538B4, D134) */
          g_CollisionNormal = n;
        D_80075844 = tc->m_TriangleOcclusionAssignment[idx];
        continue;
      }
      g_CollisionNormal = n;
      D_800757E4 = NULL;
      g_CollisionTriangleIndex = idx;
      g_SurfaceBelowFlags = TerrainSurface(tc, idx);
    }
  }
}

static void FloorMobys(const Vector3D *pos, Moby **heads, int nheads, int *floor) {
  Vector3D v[MAX_MODEL_VERTICES], n;
  int i, height;
  for (i = 0; i < nheads; i++) {
    Moby *m;
    for (m = heads[i]; m; m = m->m_CollisionChainNext) {
      const CollisionModel *model;
      const int *tri;
      Vector3D rel;
      if (!m->m_CollisionGroup)
        continue;
      model = MobyCollisionModel(m);
      rel.x = m->m_Position.x - pos->x, rel.y = m->m_Position.y - pos->y, rel.z = 0;
      if (LengthSq(&rel) - model->m_Radius * model->m_Radius >= 0 ||
          m->m_Position.z - (pos->z + model->m_Radius) > 0 || m->m_Position.z - (*floor - model->m_Radius) < 0)
        continue;
      MobyVertices(m, model, v);
      for (tri = model->m_Triangles; *tri >= 0; tri++) {
        const Vector3D *a = &v[TRI_VERTEX_A(*tri)], *b = &v[TRI_VERTEX_B(*tri)], *c = &v[TRI_VERTEX_C(*tri)];
        Vector3D ca, ba;
        if (!Straddles(a->x, c->x, b->x, pos->x) || !Straddles(a->y, c->y, b->y, pos->y) ||
            ((c->z - *floor) & (b->z - *floor) & (a->z - *floor)) < 0 ||
            ((pos->z - c->z) & (pos->z - b->z) & (pos->z - a->z)) < 0)
          continue;
        ca = Sub(c, a);
        ba = Sub(b, a);
        if (!FloorHeight(a, &ca, &ba, pos->x, pos->y, 10, &n, &height) || pos->z - height <= 0 ||
            *floor - height >= 0)
          continue;
        *floor = height;
        g_CollisionNormal = n;
        D_800757E4 = m;
        g_CollisionTriangleIndex = -1;
        g_SurfaceBelowFlags = MobySurface(m, *tri);
      }
    }
  }
}

#if !M2C_ColFindFloor
static int FindFloor(Vector3D *pos, int max) {
  Moby *heads[MAX_CHAINS];
  int floor, start, n, result = 0;
  if ((pos->x | pos->y | pos->z) >= 0) {
    floor = pos->z - max;
    floor = floor < 0 ? 0 : floor & 0xFFF0;
    start = floor;
    n = NearMobyChains(pos->x, pos->y, heads);
    if (n)
      FloorMobys(pos, heads, n, &floor);
    floor = (u_int)floor >> 4;
    FloorTerrain((u_int)pos->x >> 4, (u_int)pos->y >> 4, (u_int)pos->z >> 4, (floor >> 8) - 1, &floor, 0);
    floor <<= 4;
    result = floor == start ? 0 : floor;
  }
  g_CollisionPoint.z = result;
  return result;
}

/* The highest floor below pos, at most max below it (mobys' collision
 * models first, then the level): its height, also in g_CollisionPoint.z,
 * with its normal, D_800757E4, g_CollisionTriangleIndex and
 * g_SurfaceBelowFlags; 0 if none. The lowest height looked at is
 * (pos.z - max) & 0xFFF0, so only its low 16 bits count. */
int ColFindFloor(Vector3D *pos, int max) {
#if COL_SHADOW
  for (int i = FuzzCount(pos->x ^ pos->y << 8 ^ max); i > 0; i--) {
    Vector3D a = FuzzRand() & 1 ? FuzzNear(pos) : FuzzAtMoby(pos);
    int m = FuzzRand() & 1 ? max : (int)(FuzzRand() % 0x12000);
    FUZZ_ONE(ColFindFloor, ColFindFloor_gen(&a, m), FindFloor(&a, m));
  }
#endif
  SHADOW(ColFindFloor, ColFindFloor_gen(pos, max), FindFloor(pos, max));
  return FindFloor(pos, max);
}
#else
int ColFindFloor(Vector3D *pos, int max) { return ColFindFloor_gen(pos, max); }
#endif

#if !M2C_func_8004DF24
static int CameraFloor(Vector3D *pos) {
  int floor = 0;
  if ((pos->x | pos->y | pos->z) >= 0)
    FloorTerrain((u_int)pos->x >> 4, (u_int)pos->y >> 4, (u_int)pos->z >> 4, -1, &floor, 1);
  if (floor > 0)
    return floor << 4;
  D_80075844 = -1;
  return 0;
}

/* The camera's occlusion group from the level's floor below pos, in
 * D_80075844 (-1 if there is none): the highest floor of the first cell
 * down from pos that has one. Returns that floor's height (0: none). */
int func_8004DF24(Vector3D *pos) {
#if COL_SHADOW
  for (int i = FuzzCount(pos->x ^ pos->y << 8 ^ pos->z); i > 0; i--) {
    Vector3D a = FuzzNear(pos);
    FUZZ_ONE(func_8004DF24, func_8004DF24_gen(&a), CameraFloor(&a));
  }
#endif
  SHADOW(func_8004DF24, func_8004DF24_gen(pos), CameraFloor(pos));
  return CameraFloor(pos);
}
#else
int func_8004DF24(Vector3D *pos) { return func_8004DF24_gen(pos); }
#endif

/* ===================================================================== */
/* ColSpyroInHitbox                                                       */
/* ===================================================================== */

#if !M2C_ColSpyroInHitbox
static int SpyroInHitbox(Vector3D *pPos, int pSize, int pDamageOutput) {
  int r = pSize + 0x164;
  Vector3D d = Sub(pPos, &g_Spyro.m_Position);
  if (d.x < 0)
    d.x = -d.x;
  if (d.x - r > 0)
    return 0;
  if (d.y < 0)
    d.y = -d.y;
  if (d.y - r > 0)
    return 0;
  if (d.z < 0)
    d.z = -d.z;
  if (d.z - r > 0)
    return 0;
  if (LengthSq(&d) - r * r > 0)
    return 0;
  g_Spyro.m_DamageFlags |= pDamageOutput;
  return 1;
}

/* Whether Spyro's centre is within pSize plus his own radius (0x164) of
 * pPos (box first, then sphere); if so, pDamageOutput is added to his
 * damage flags. */
int ColSpyroInHitbox(Vector3D *pPos, int pSize, int pDamageOutput) {
#if COL_SHADOW
  for (int i = FuzzCount(pPos->x ^ pPos->y << 8 ^ pSize); i > 0; i--) {
    Vector3D a = FuzzRand() & 1 ? FuzzNear(pPos) : FuzzNear(&g_Spyro.m_Position);
    int size = FuzzRadius(pSize), dmg = (int)FuzzRand();
    FUZZ_ONE(ColSpyroInHitbox, ColSpyroInHitbox_gen(&a, size, dmg), SpyroInHitbox(&a, size, dmg));
  }
#endif
  SHADOW(ColSpyroInHitbox, ColSpyroInHitbox_gen(pPos, pSize, pDamageOutput), SpyroInHitbox(pPos, pSize, pDamageOutput));
  return SpyroInHitbox(pPos, pSize, pDamageOutput);
}
#else
int ColSpyroInHitbox(Vector3D *pPos, int pSize, int pDamageOutput) {
  return ColSpyroInHitbox_gen(pPos, pSize, pDamageOutput);
}
#endif

/* ===================================================================== */
/* ColMoby                                                                */
/* ===================================================================== */

/* A moby's collision volumes follow its collision model's radius and
 * count (the same m_CollisionGroup the triangles use): entries of a word
 * (radius in bits 16-31, the kind in bits 8-15, flags), the kind's data,
 * and a damage word. */
#define VOLUME_LAST 1      /* The last entry */
#define VOLUME_PASS 2      /* Report the hit but keep looking */
#define VOLUME_NO_DAMAGE 4 /* Don't pass pApplyingDamage to the moby */

#define VOLUME_SPHERE 0   /* + damage */
#define VOLUME_CYLINDER 1 /* + (z offset << 16), damage: a sphere moved up or down */
#define VOLUME_CAPSULE 2  /* + (top << 16 | bottom), (end below << 16 | end above), end radius, damage */
#define VOLUME_VERTEX 3   /* + vertex index, damage: a sphere at a vertex of the frame */

/* Push pPos out of a volume the sphere is in: d (from the sphere to the
 * volume) is scaled by 1 + sqrt(limit / dist2), in 256ths, until it is at
 * least sqrt(limit) long, and the centre moves by what d grew. */
static void MobyPushOut(Vector3D *pos, Vector3D d, int dist2, int limit) {
  for (;;) {
    int shift = LeadingSignBits(limit) - 2, num, den, scale, len2;
    Vector3D nd;
    if (dist2 <= 0)
      dist2 = 1, d.z = -1;
    num = Shl(limit, shift);
    shift -= 16;
    if (shift >= 0) {
      den = Shl(dist2, shift);
    } else {
      den = (int)((u_int)dist2 >> ((-shift) & 31));
      if (den <= 0)
        den = 1;
    }
    scale = MipsDiv(num, den);
    if (scale)
      scale = TableSqrt(scale);
    nd = Scale(scale + 1, &d, 8);
    len2 = LengthSq(&nd);
    pos->x += d.x - nd.x;
    pos->y += d.y - nd.y;
    pos->z += d.z - nd.z;
    if (len2 - limit >= 0)
      return;
    dist2 = len2;
    d = nd;
  }
}

#if !M2C_ColMoby
static int MobyVolumes(Vector3D *pPos, int pRadius, int *pReceivingDamage, int pApplyingDamage, Moby *pIgnoreMoby,
            int arg5) {
  Moby **grid = (Moby **)g_MobyCollisionChain, **slots[MAX_CHAINS];
  Moby *hit = NULL;
  int x = pPos->x, y = pPos->y, nslots = 0, col, row, ncol, nrow, i;
  if ((x | y | pPos->z) < 0)
    return 0;
  col = (u_int)x >> 13;
  row = ((u_int)y >> 13) << 5;
  if (x & 0x1000) {
    ncol = col + 1;
    if (ncol == 0x20)
      ncol = -1;
  } else {
    ncol = col - 1;
  }
  if (y & 0x1000) {
    nrow = row + 0x20;
    if (nrow == 0x400)
      nrow = -1;
  } else {
    nrow = row - 0x20;
  }
  slots[nslots++] = &grid[col + row];
  if (ncol >= 0)
    slots[nslots++] = &grid[ncol + row];
  if (nrow >= 0)
    slots[nslots++] = &grid[col + nrow];
  if (ncol >= 0 && nrow >= 0)
    slots[nslots++] = &grid[ncol + nrow];

  for (i = 0; i < nslots; i++) {
    Moby *m;
    for (m = *slots[i]; m; m = m->m_CollisionChainNext) {
      const CollisionModel *model;
      const u_int *e;
      u_int word = 0;
      Vector3D d;
      int dx2, dy2, dist2, reach;
      if (m == pIgnoreMoby || !m->m_CollisionGroup)
        continue;
      model = MobyCollisionModel(m);
      d = Sub(&m->m_Position, pPos);
      dx2 = S16(d.x) * S16(d.x);
      dy2 = S16(d.y) * S16(d.y);
      dist2 = LengthSq(&d);
      reach = model->m_Radius + pRadius;
      if (dist2 - reach * reach >= 0)
        continue;
      e = (const u_int *)model->m_Triangles;
      while (!(word & VOLUME_LAST)) {
        int r = (word = *e++) >> 16, limit, len2;
        Vector3D v = d;
        limit = (r + pRadius) * (r + pRadius);
        switch ((word & 0xFF00) >> 8) {
        case VOLUME_SPHERE:
          e += 1;
          len2 = dist2;
          if (len2 - limit >= 0)
            continue;
          break;
        case VOLUME_CYLINDER:
          v.z = ((int)e[0] >> 16) + d.z;
          e += 2;
          len2 = v.z * v.z + dx2 + dy2;
          if (len2 - limit >= 0)
            continue;
          break;
        case VOLUME_CAPSULE: {
          int top = ((int)e[0] >> 16) + d.z, bottom = (short)e[0] + d.z;
          e += 4;
          if (limit - dx2 - dy2 <= 0)
            continue;
          if ((top | -bottom) >= 0) {
            v.z = 0;
            len2 = dx2 + dy2;
          } else {
            int end = top < 0 ? (short)(e[-3] >> 16) : (short)e[-3];
            v.z = end + d.z;
            len2 = v.z * v.z + dx2 + dy2;
            limit = ((int)e[-2] + pRadius) * ((int)e[-2] + pRadius);
            if (len2 - limit >= 0)
              continue;
          }
          break;
        }
        default: { /* VOLUME_VERTEX */
          int scale;
          const u_int *words = MobyFrameWords(m, &scale);
          Vector3D w = MobyVertex(m, words[(u_short)e[0]], scale);
          v = Sub(&w, pPos);
          e += 2;
          len2 = LengthSq(&v);
          if (len2 - limit >= 0)
            continue;
          break;
        }
        }
        /* US records the moby before the pass test, so a pass volume is
         * still the hit ColMoby returns; PAL records it after (its asm moves
         * the `addi s2, fp, 0` past the branch, PAL 0x80053CA4 and the three
         * kinds after it): a pass volume damages but doesn't block (D149). */
        if (!REGION_IS(PAL))
          hit = m;
        if (!(word & VOLUME_NO_DAMAGE))
          m->m_DamageFlags |= pApplyingDamage;
        if (pReceivingDamage)
          *pReceivingDamage |= e[-1];
        if (word & VOLUME_PASS)
          continue;
        if (REGION_IS(PAL))
          hit = m;
        if (arg5 & 1) {
          MobyPushOut(pPos, v, len2, limit);
          return (int)(uintptr_t)m;
        }
        if (!(arg5 & 2))
          return (int)(uintptr_t)hit;
      }
    }
  }
  return arg5 & 1 ? 0 : (int)(uintptr_t)hit;
}

/* Whether a sphere at pPos of radius pRadius touches a moby's collision
 * volumes (pIgnoreMoby's excepted), in the cell of pPos and the three
 * beside it towards which it lies. Each hit volume gets pApplyingDamage
 * on its moby (unless marked otherwise) and adds its damage word to
 * *pReceivingDamage. The first hit ends the test (returning its moby),
 * except for volumes marked VOLUME_PASS and with arg5 bit 2; with arg5
 * bit 1 the first hit instead pushes pPos out of the volume and returns
 * that moby (and only such a push returns a moby). */
int ColMoby(Vector3D *pPos, int pRadius, int *pReceivingDamage, int pApplyingDamage, Moby *pIgnoreMoby,
            int arg5) {
#if COL_SHADOW
  for (int i = FuzzCount(pPos->x ^ pPos->y << 8 ^ pRadius ^ arg5 << 20); i > 0; i--) {
    /* pPos and *pReceivingDamage are outputs: host copies, compared here. */
    Vector3D a = FuzzRand() & 1 ? FuzzNear(pPos) : FuzzAtMoby(pPos), ag = a, ah = a;
    int r = FuzzRadius(pRadius), flags = (int)(FuzzRand() & 3), dmg = (int)FuzzRand(), rg = 0, rh = 0, gen_;
    int recv = FuzzRand() & 1;
    Moby *ignore = FuzzRand() & 1 ? pIgnoreMoby : NULL;
    ShadowBegin();
    gen_ = (int)ColMoby_gen(&ag, r, recv ? &rg : NULL, dmg, ignore, flags);
    ShadowSwap();
    ShadowEnd("fuzz ColMoby", gen_, MobyVolumes(&ah, r, recv ? &rh : NULL, dmg, ignore, flags));
    if (rg != rh || ag.x != ah.x || ag.y != ah.y || ag.z != ah.z) {
      g_col_shadow_fail++;
      printf("  colshadow fuzz ColMoby outputs unit %x %d,%d,%d hand %x %d,%d,%d\n", rg, ag.x, ag.y, ag.z, rh, ah.x,
             ah.y, ah.z);
    }
    memcpy((void *)g_psx_arena_base, s_before, PSX_RAM_SIZE);
    gteRegs = s_gte_before;
  }
#endif
  SHADOW(ColMoby, ColMoby_gen(pPos, pRadius, pReceivingDamage, pApplyingDamage, pIgnoreMoby, arg5), MobyVolumes(pPos, pRadius, pReceivingDamage, pApplyingDamage, pIgnoreMoby, arg5));
  return MobyVolumes(pPos, pRadius, pReceivingDamage, pApplyingDamage, pIgnoreMoby, arg5);
}
#else
int ColMoby(Vector3D *pPos, int pRadius, int *pReceivingDamage, int pApplyingDamage, Moby *pIgnoreMoby,
            int arg5) {
  return ColMoby_gen(pPos, pRadius, pReceivingDamage, pApplyingDamage, pIgnoreMoby, arg5);
}
#endif
