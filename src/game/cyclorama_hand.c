/* The cyclorama set-up (src/cyclorama.c, asm/nonmatchings/cyclorama/
 * func_80050BD0.s): the level's portals and the sky behind them, then the
 * sky, written by hand from the asm so it reads as C (docs/native-plan.md
 * stage 3, decisions D94). Compiled C in the original, so this is
 * decompiling: the asm's struct offsets are checked by static_asserts.
 * Replaces the mips2c unit (cmake/mips2c.cmake M2C_HAND); checked against
 * the unit with the shadow of hand_shadow.h (in a child: it draws) and
 * against PCSX-Redux with the oracle replay (docs/translation-log.md).
 * M2C_func_80050BD0=1 (cmake SPYRO_M2C_FUNCS) compiles it out for the unit.
 *
 * It calls the same functions with the same values in the same order as
 * the asm (the matrix library and the projections share the GTE's
 * rotation and translation), and leaves the same values in the globals the
 * sky renderers read: the portal's screen box, depth and outline. */
#include <assert.h> /* PsyCross headers use C11 static_assert */
#include <string.h>

#include "common.h"

#include "camera.h"
#include "cyclorama.h"
#include "environment.h"
#include "hand_shadow.h"
#include "images.h"
#include "math.h"
#include "renderers.h"
#include "vector.h"

#include <libgte.h>
#include <stddef.h>

static_assert(offsetof(Portal, m_PointCount) == 4 && offsetof(Portal, unk_0x8) == 8, "Portal");
static_assert(offsetof(Portal, m_WorldSector) == 0x14 && offsetof(Portal, m_Center) == 0x20, "Portal");
static_assert(offsetof(Portal, m_Points) == 0x2C && offsetof(PortalDataSky, copyBgColor) == 0x10, "Portal");
static_assert(offsetof(Camera, m_ViewMatrix) == 0x14 && offsetof(Camera, m_Position) == 0x28, "Camera");
static_assert(offsetof(Camera, m_Rotation) == 0x4C && offsetof(Camera, m_OcclusionGroup) == 0x54, "Camera");
static_assert(offsetof(Cyclorama, m_BackgroundColor) == 0x10, "Cyclorama");
static_assert(offsetof(Environment, m_OcclusionGroupCount) == 0xC, "Environment");

/* A side of a portal's outline on screen, for the sky renderers: from and
 * to (y << 16 | x), and its line dy*x + dx*y + c = 0. A 0 in m_Live ends
 * the list. */
typedef struct {
  int m_Live;
  u_int m_From, m_To;
  int m_Dy, m_Dx, m_C;
} PortalEdge;
static_assert(sizeof(PortalEdge) == 0x18, "PortalEdge");

extern char D_8006FCF4[];          /* the renderers' work area */
extern u_char D_800771C8[];        /* a byte per sector, not 0 when it was visible */
extern char *D_80075798;           /* the sky renderers' list, D_8006FCF4+0x800 */
extern int D_80075934;             /* the portal's depth, its OT slot */
extern int D_8007AA00[4];          /* its screen box: top, bottom, right, left, << 16 */
extern PortalEdge D_80077EA0[];    /* its outline */
extern Vector3D D_80078DD8[];      /* its points on screen: x, y and depth */

/* The sky renderers (asm/renderers/r_cyclorama.s, mips2c units). */
void func_8004FEA0(Color *pColor);
void func_80050240(PortalDataSky *pSky, MATRIX *pView, MATRIX *pSkyView);
void func_8004F4BC(PortalDataSky *pSky, MATRIX *pView, MATRIX *pSkyView);
int func_80016D08(int pValue); /* math_hand.c: the leading sign bits */

/* The mips2c unit, linked as func_80050BD0_gen with SPYRO_M2C_REF or
 * SPYRO_M2C_FUNCS. */
u_int func_80050BD0_gen();

#if !M2C_func_80050BD0

#define SCREEN_W 0x200
#define SCREEN_H 0xF0

/* A product as the R3000A's mult leaves it in LO. */
static inline int MulLo(int a, int b) { return (int)((u_int)a * (u_int)b); }
static inline int Abs(int v) { return v < 0 ? -v : v; }

/* The portal's points: its centre, then the outline. */
static inline Vector3D *PortalPoint(Portal *p, int j) { return &(&p->m_Center)[j]; }

/* The view for the portals' sky (sky), from the camera's angles and the
 * sky's slow turn (D_80075858 yaw, D_800758FC pitch), and the same with
 * its middle row at 5/8 (squash). */
static void SkyMatrices(MATRIX *sky, MATRIX *squash) {
  int a, i;
  memset(sky, 0, sizeof *sky);
  SetTransMatrix(sky);
  a = g_Camera.m_Rotation.y - D_800758FC;
  sky->m[0][0] = 0x1000;
  sky->m[1][1] = Cos(a);
  sky->m[2][1] = Sin(a);
  sky->m[1][2] = -Sin(a);
  sky->m[2][2] = Cos(a);
  memset(squash, 0, sizeof *squash);
  a = g_Camera.m_Rotation.z + D_80075858;
  squash->m[0][0] = Cos(a);
  squash->m[2][0] = -Sin(a);
  squash->m[1][1] = 0x1000;
  squash->m[0][2] = Sin(a);
  squash->m[2][2] = Cos(a);
  MulMatrix(sky, squash);
  memset(squash, 0, sizeof *squash);
  a = g_Camera.m_Rotation.x;
  squash->m[0][0] = Cos(a);
  squash->m[1][0] = -Sin(a);
  squash->m[0][1] = Sin(a);
  squash->m[1][1] = Cos(a);
  squash->m[2][2] = 0x1000;
  MulMatrix(sky, squash);
  memcpy(squash, sky, 0x14);
  for (i = 0; i < 3; i++)
    squash->m[1][i] = sky->m[1][i] * 320 / 512;
}

/* A point strictly inside the screen and in front of the camera. */
static inline int OnScreen(const Vector3D *q) {
  return (u_int)(q->x - 1) < SCREEN_W - 1 && (u_int)(q->y - 1) < SCREEN_H - 1 && q->z > 0;
}

/* Whether the side from c to q can cross the screen: not both past the
 * same edge of it. */
static int SideOnScreen(const Vector3D *c, const Vector3D *q) {
  return !(c->x >= SCREEN_W && q->x >= SCREEN_W) && !(c->x <= 0 && q->x <= 0) &&
         !(c->y >= SCREEN_H && q->y >= SCREEN_H) && !(c->y <= 0 && q->y <= 0);
}

static void SetEdge(PortalEdge *e, const Vector3D *c, const Vector3D *q) {
  e->m_Live = 1;
  e->m_From = (u_int)c->y << 16 | (u_short)c->x;
  e->m_To = (u_int)q->y << 16 | (u_short)q->x;
  e->m_Dy = c->y - q->y;
  e->m_Dx = q->x - c->x;
  e->m_C = MulLo(-e->m_Dy, c->x) - MulLo(e->m_Dx, c->y);
}

/* The outline's screen box and what it found. */
typedef struct {
  int minX, minY, maxX, maxY;
  int found; /* bit 0: a point on screen; bit 1: the portal is seen */
  int edges;
} Outline;

/* Seen: the box overlaps the screen with a side crossing it, or covers the
 * whole screen; and the portal is in front. */
static int Seen(const Outline *o) {
  if (o->minX < SCREEN_W && o->minY < SCREEN_H && o->maxX > 0 && o->maxY > 0 && D_80075934 > 0 && o->edges)
    return 1;
  return o->minX <= 0 && o->minY <= 0 && o->maxX >= SCREEN_W && o->maxY >= SCREEN_H && D_80075934 > 0;
}

/* Seen, but with no point on screen, and near: the camera may be inside
 * the box looking away. Its forward vector against the way to the
 * portal's middle decides. */
static int LooksAway(const Vector3D *middle) {
  Vector3D to, fwd = {0x1000, 0, 0};
  Vector3D8 angles;
  MATRIX m;
  VecSub(&to, (Vector3D *)middle, &g_Camera.m_Position);
  angles.x = (u_short)g_Camera.m_Rotation.x >> 4;
  angles.y = (u_short)g_Camera.m_Rotation.y >> 4;
  angles.z = (u_short)g_Camera.m_Rotation.z >> 4;
  RotVec8ToMatrix(&angles, &m, NULL);
  VecRotateByMatrix(&m, &fwd, &fwd);
  return (int)((u_int)MulLo(fwd.x, to.x) + (u_int)MulLo(fwd.y, to.y) + (u_int)MulLo(fwd.z, to.z)) < 0;
}

/* The box clamped to the screen, for the renderers; the outline's list
 * ended. */
static void PublishBox(Outline *o) {
  if (o->minX < 0)
    o->minX = 0;
  if (o->minY < 0)
    o->minY = 0;
  if (o->maxX > SCREEN_W)
    o->maxX = SCREEN_W;
  if (o->maxY > SCREEN_H)
    o->maxY = SCREEN_H;
  D_8007AA00[0] = o->minY << 16;
  D_8007AA00[1] = o->maxY << 16;
  D_8007AA00[2] = o->maxX << 16;
  D_8007AA00[3] = o->minX << 16;
  D_80077EA0[o->edges].m_Live = 0;
}

static inline void Extend(Outline *o, const Vector3D *q) {
  if (o->maxX < q->x)
    o->maxX = q->x;
  if (q->x < o->minX)
    o->minX = q->x;
  if (o->maxY < q->y)
    o->maxY = q->y;
  if (q->y < o->minY)
    o->minY = q->y;
}

/* The outline's next point after j, the way round the camera sees it. */
static inline int NextPoint(int j, int n, int facing) {
  if (facing > 0)
    return j + 1 == n ? 0 : j + 1;
  return j - 1 < 0 ? n - 1 : j - 1;
}

static void Portals(void) {
  MATRIX sky, squash;
  int i;
  SkyMatrices(&sky, &squash);
  for (i = 0; i < g_PortalCount; i++) {
    Portal *portal = g_Portals[i];
    Vector3D *screen = D_80078DD8, toCamera, middle;
    Outline o;
    int color, sector = portal->m_WorldSector, shift, dist, n, j, facing, thinX = 1, thinY = 1, sumX, sumY, fade;
    u_int sumZ;
    /* A portal in a sector out of view isn't drawn. */
    if (sector >= 0 && !D_800771C8[sector])
      continue;
    /* The sky's colour pulses towards white... */
    color = ColorLerp(portal->m_Skybox->copyBgColor, 0xFFFFFF, Abs(Cos(g_LevelTicks * 16 + i * 512)) >> 1);
    /* ...and far away, with the low-poly world skipped, fades to this
     * level's sky. */
    VecSub(&toCamera, &portal->m_Center, &g_Camera.m_Position);
    shift = func_80016D08(Abs(toCamera.x) + Abs(toCamera.y) + Abs(toCamera.z));
    if (shift >= 15) {
      VecShiftRight(&toCamera, shift >> 1);
      dist = VecMagnitude(&toCamera, 1) << (shift >> 1);
    } else
      dist = VecMagnitude(&toCamera, 1);
    shift = func_80016D08(dist) - 13;
    if (shift < 0)
      shift = 0;
    if (g_SkipLowPolyWorld && dist > 0x4000) {
      fade = (0x8000 - dist) >> 2;
      if (fade < 0)
        fade = 0;
      if (fade > 0x1000)
        fade = 0x1000;
      color = ColorLerp(*(int *)&g_Cyclorama.m_BackgroundColor, color, fade);
    }
    /* The points on screen (shifted down as far as the distance allows),
     * and their middle. */
    VecNull(&middle);
    for (j = 0; j < portal->m_PointCount; j++) {
      func_80017B48(&screen[j], PortalPoint(portal, j), shift);
      VecAdd(&middle, &middle, PortalPoint(portal, j));
    }
    middle.x /= portal->m_PointCount;
    middle.y /= portal->m_PointCount;
    middle.z /= portal->m_PointCount;
    /* Which side of the portal the camera is on (unk_0x8 its normal). */
    VecSub(&toCamera, &g_Camera.m_Position, PortalPoint(portal, 1));
    VecShiftRight(&toCamera, shift);
    facing = MulLo(-toCamera.x, portal->unk_0x8.x) - MulLo(toCamera.y, portal->unk_0x8.y) -
             MulLo(toCamera.z, portal->unk_0x8.z);

    /* First pass: the outline as projected. */
    o.minX = 0x13F8, o.minY = SCREEN_H, o.maxX = 0, o.maxY = 0, o.found = 0, o.edges = 0;
    sumX = sumY = 0;
    sumZ = 0;
    D_80075798 = D_8006FCF4 + 0x800;
    n = portal->m_PointCount;
    for (j = 0; j < n; j++) {
      const Vector3D *c = &screen[j], *q = &screen[NextPoint(j, n, facing)];
      sumX += q->x;
      sumY += q->y;
      sumZ += (u_int)q->z;
      if (OnScreen(q))
        o.found = 1;
      Extend(&o, q);
      if (!SideOnScreen(c, q))
        continue;
      if (Abs(c->x - q->x) >= 3)
        thinX = 0;
      if (Abs(c->y - q->y) >= 3)
        thinY = 0;
      SetEdge(&D_80077EA0[o.edges++], c, q);
    }
    D_80075934 = (int)sumZ;
    /* A sliver (every side under 3 pixels across, or every side under 3
     * high) isn't drawn. */
    if ((thinX || thinY) && o.edges)
      continue;
    sumX /= n;
    sumY /= n;
    D_80075934 = (D_80075934 / n) >> 7;
    if (D_80075934 >= 0x100)
      D_80075934 += 0x40;
    if (D_80075934 >= 0x800)
      D_80075934 = 0x7FF;
    o.found = Seen(&o) ? o.found | 2 : 0;
    if (o.found == 2 && dist < 0x1000 && LooksAway(&middle))
      o.found = 0;
    if (o.found) {
      /* The portal's face in its sky's colour, faded into the sky
       * between 0x3000 and 0x4000 away. */
      Color *fill = (Color *)&color;
      PublishBox(&o);
      if (dist < 0x4000) {
        if (dist > 0x3000) {
          D_8007575C = dist - 0x3000;
          D_800757D4 = ColorLerp(portal->m_Skybox->copyBgColor, color, dist - 0x3000);
          fill = (Color *)&D_800757D4;
        } else
          fill = (Color *)&portal->m_Skybox->copyBgColor;
      }
      func_8004FEA0(fill);
    }
    if (dist >= 0x4000)
      continue;

    /* Second pass, near: each point pushed 2 pixels out from the
     * outline's middle, and the sky behind it. */
    o.minX = SCREEN_W, o.minY = SCREEN_H, o.maxX = 0, o.maxY = 0, o.found = 0, o.edges = 0;
    D_80075798 = D_8006FCF4 + 0x800;
    for (j = 0; j < portal->m_PointCount; j++) {
      Vector3D *p = &screen[j];
      if (sumX < p->x)
        p->x += 2;
      else if (p->x < sumX)
        p->x -= 2;
      if (sumY < p->y)
        p->y += 2;
      else if (p->y < sumY)
        p->y -= 2;
      Extend(&o, p);
    }
    n = portal->m_PointCount;
    for (j = 0; j < n; j++) {
      const Vector3D *c = &screen[j], *q = &screen[NextPoint(j, n, facing)];
      if (OnScreen(q))
        o.found = 1;
      if (SideOnScreen(c, q))
        SetEdge(&D_80077EA0[o.edges++], c, q);
    }
    o.found = Seen(&o) ? o.found | 2 : 0;
    if (o.found == 2 && dist < 0x1000 && LooksAway(&middle))
      o.found = 0;
    if (!o.found)
      continue;
    PublishBox(&o);
    if (dist > 0x3000) {
      D_8007575C = dist - 0x3000;
      D_800757D4 = ColorLerp(portal->m_Skybox->copyBgColor, color, dist - 0x3000);
      func_80050240(portal->m_Skybox, &sky, &squash);
    } else
      func_8004F4BC(portal->m_Skybox, &sky, &squash);
  }
  /* Then the level's own sky, in the camera's occlusion group (-1: all). */
  func_8004EBA8(g_Camera.m_OcclusionGroup < g_Environment.m_OcclusionGroupCount ? g_Camera.m_OcclusionGroup : -1,
                &g_Camera.m_ViewMatrix, &g_Camera.m_ProjectionMatrix);
}

static void TurnAndDraw(void) {
  /* The portals' sky turns slowly; its pitch stops at 0x80. */
  D_80075858 = (D_80075858 + 2) & 0xFFF;
  D_800758FC = (D_800758FC + 2) & 0xFFF;
  if (D_800758FC > 0x800)
    D_800758FC -= 0x1000;
  if (D_800758FC > 0x80)
    D_800758FC = 0x80;
  Portals();
}

/* The portals with the sky behind them, then the sky. */
void func_80050BD0(void) {
  HAND_SHADOW_FORK(func_80050BD0, 0, 0, TurnAndDraw());
  TurnAndDraw();
}
#endif
