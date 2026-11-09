/* The moby (object) culls of asm/renderers/r_moby.s: func_8001F158 (the
 * regular mobys) and func_800208FC (the specular, shiny or metal, ones),
 * hand-written asm in the original, written by hand from the asm so it
 * reads as C (docs/native-plan.md stage 3, decisions D96).
 *
 * Each walks a NULL-ended list of mobys (D_8006FCF4+0, or +0x2200 for the
 * specular ones), drops those outside the view (a box around the camera,
 * then the frustum after the camera's matrix), and leaves a MobyRecord for
 * each moby it keeps at D_8006FCF4+0x1600 (0-ended): its flags, animation
 * frames, position and rotation in camera space, and how far inside its
 * draw distance it is. The draw loops, func_8001F798 and func_80020F34
 * (moby_hand.c), draw from those. A kept moby close enough, whose shadow is
 * on, also goes into the shadow list (moby, its frame's shadow byte) that
 * g_MobyShadows' third word points into.
 *
 * The asm keeps its state in registers and touches no stack or scratchpad
 * (but its register save to D_80077DD8, which nothing reads). This sets the
 * rotation matrix as the asm does and does the rest as native maths on
 * values (D124).
 * The draw-distance hooks of tools/mips2c/hints.yaml (lod.c, decisions D49)
 * are made where the unit makes them, with the same values.
 *
 * The draw loops are hand C already, so the unit stays linked for
 * fast_render=0 (render_hand.h HAND_UNIT_FN_RC); --fast-render-check and
 * the oracle replay's shadow compare each call with the unit, and the
 * oracle replay with PCSX-Redux (docs/translation-log.md).
 *
 * The asm's `add`/`sub` would trap on a signed overflow; none happens with
 * the game's positions and sizes (in particular the record flags' adds:
 * a depth scale of 0x80 or more on a clipped moby would), and this wraps. */
#define RENDER_READABLE 1 /* render_hand.h: no GTE data registers, scratchpad or spill area (D124) */
#include "render_hand.h"

extern char D_8006FCF4[];    /* the moby renderers' tables (TBL_*) */
extern char D_8006CBF8[];    /* sines, 256 half words */
extern char D_8006CC78[];    /* cosines (the sines from +0x80) */
extern char g_Camera[];      /* the view matrix (5 words), position at 0x28 */
extern char g_Models[];      /* models by class */
extern char g_MobyShadows[]; /* word 2: the shadow list's next entry */

#define gte_mvmva_rt_v0() m2c_gte_0x486012() /* MVMVA sf 1, R x V0 */
#define gte_mvmva_rt_ir() m2c_gte_0x49E012() /* MVMVA sf 1, R x IR */

/* D_8006FCF4's tables (byte offsets). */
enum {
  TBL_MOBYS = 0x0000,   /* the regular mobys to cull, NULL-ended */
  TBL_RECORDS = 0x1600, /* MobyRecords (0x38 bytes), 0-ended */
  TBL_RECORDS_END = 0x2200,
  TBL_SPECULAR = 0x2200, /* the specular mobys to cull, NULL-ended */
  TBL_SHADOWS = 0x2800,  /* the shadow list: moby, shadow byte */
};

/* A MobyRecord's fields (moby_hand.c has the struct). */
enum {
  REC_FLAGS = 0x00,  /* REC_*, depth scale << 24, depth offset << 16,
                        frame progress << 8, scale override */
  REC_ANIM = 0x04,   /* the animation's header */
  REC_FRAME = 0x08,  /* its current frame */
  REC_NEXT = 0x0C,   /* the frame it blends into, 0 when progress is 0 */
  REC_POS = 0x10,    /* x, y, z: camera space */
  REC_MATRIX = 0x1C, /* the rotation, R11R12 .. R33 (half) */
  REC_DETAIL = 0x2E, /* how far inside its detail distance (half) */
  REC_SPECULAR = 0x30, /* specular records only: the moby's 0x4C word */
  REC_MOBY = 0x34,
  REC_SIZE = 0x38,
};
#define REC_CLIP 0x80000000u   /* partly off screen: clip-test the vertices */
#define REC_INSIDE 0x40000000u /* well inside the view */

/* A Moby's fields this uses (include/moby.h). */
enum {
  MOBY_POS = 0xC, /* x, y, z words */
  MOBY_SHADOW_DIST = 0x1C,
  MOBY_CLASS = 0x36,
  MOBY_ANIMS = 0x3C,    /* animation, next animation, frame, next frame */
  MOBY_FRAME = 0x3E,
  MOBY_PROGRESS = 0x40, /* between the frame and the next */
  MOBY_ANGLES = 0x44,   /* m_Rotation x, y, z bytes, m_DepthOffset */
  MOBY_DETAIL = 0x4B,   /* low 6 bits: the detail distance >> 8 */
  MOBY_SPECULAR = 0x4C, /* m_SpecularMetalColor[3], m_SpecularMetalType */
  MOBY_RADIUS = 0x50,   /* m_RenderRadius; the next byte m_WasDrawn */
  MOBY_WAS_DRAWN = 0x51,
  MOBY_SCALE = 0x57, /* m_ScaleOverride */
};

/* A model (g_Models[class]): its animations' headers from 0x38. An
 * animation's radius byte is at 7, its depth scale at 0xB, its frames
 * (8 bytes, the shadow byte at 6) from 0x24. */
#define MODEL_ANIMS 0x38u
#define ANIM_RADIUS 0x7u
#define ANIM_DEPTH_SCALE 0xBu
#define ANIM_FRAMES 0x24u
#define FRAME_SHADOW 0x6u

/* The GTE's rotation matrix, as the asm keeps it in five registers. */
typedef struct {
  u32 r11r12, r13r21, r22r23, r31r32, r33;
} Mat;

static inline void MatToGte(const Mat *m) {
  CTC2(m->r11r12, 0);
  CTC2(m->r13r21, 1);
  CTC2(m->r22r23, 2);
  CTC2(m->r31r32, 3);
  CTC2(m->r33, 4);
}

/* The camera, as the asm loads it once: position and view matrix. */
typedef struct {
  u32 x, y, z;
  Mat view;
} Cam;

/* fast_render=3 (D124): the matrix as MVMVA reads it once MatToGte has
 * set it (CTC2 4 sign-extends R33), and the rotation of a vector by it
 * (MVMVA sf 1): IR1-3 and MAC1-3, as values. */
static inline NgteMat MatNgte(const Mat *m) {
  NgteMat n;
  n.m11 = (s16)m->r11r12, n.m12 = (s16)(m->r11r12 >> 16), n.m13 = (s16)m->r13r21;
  n.m21 = (s16)(m->r13r21 >> 16), n.m22 = (s16)m->r22r23, n.m23 = (s16)(m->r22r23 >> 16);
  n.m31 = (s16)m->r31r32, n.m32 = (s16)(m->r31r32 >> 16), n.m33 = (s16)m->r33;
  return n;
}
static inline NgteVec Rotate(const Mat *m, int vx, int vy, int vz) {
  const NgteMat n = MatNgte(m);
  return ngte_mvmva(&n, 0, 0, 0, vx, vy, vz);
}

/* The matrix turned by an angle (a byte, doubled: the sine table's index):
 * two of its columns from the matrix times a unit vector each, as the
 * register-file code below. The Z and Y turns set the GTE's matrix (the
 * asm's state), the X turn leaves it. */
static inline void TurnZ(Mat *m, u32 a) {
  NgteVec u, v;
  s32 c, s;
  if (!a)
    return;
  c = (s16)AT16(D_8006CC78 + a);
  s = (s16)AT16(D_8006CBF8 + a);
  u = Rotate(m, c, 0, s);
  v = Rotate(m, (s16)-s, 0, c);
  m->r11r12 = (m->r11r12 & 0xFFFF0000) + ((u32)u.ir1 & 0xFFFF);
  m->r31r32 = (m->r31r32 & 0xFFFF0000) + ((u32)u.ir3 & 0xFFFF);
  m->r13r21 = ((u32)v.ir1 & 0xFFFF) + ((u32)u.ir2 << 16);
  m->r22r23 = (m->r22r23 & 0xFFFF) + ((u32)v.ir2 << 16);
  m->r33 = (u32)v.ir3 & 0xFFFF;
  MatToGte(m);
}

static inline void TurnY(Mat *m, u32 a) {
  NgteVec u, v;
  s32 c, s;
  if (!a)
    return;
  c = (s16)AT16(D_8006CC78 + a);
  s = (s16)AT16(D_8006CBF8 + a);
  u = Rotate(m, 0, c, s);
  v = Rotate(m, 0, (s16)-s, c);
  m->r11r12 = (m->r11r12 & 0xFFFF) + ((u32)u.ir1 << 16);
  m->r31r32 = (m->r31r32 & 0xFFFF) + ((u32)u.ir3 << 16);
  m->r13r21 = (m->r13r21 & 0xFFFF0000) + ((u32)v.ir1 & 0xFFFF);
  m->r22r23 = ((u32)v.ir2 << 16) + ((u32)u.ir2 & 0xFFFF);
  m->r33 = (u32)v.ir3 & 0xFFFF;
  MatToGte(m);
}

static inline void TurnX(Mat *m, u32 a) {
  NgteVec u, v;
  s32 c, s;
  if (!a)
    return;
  c = (s16)AT16(D_8006CC78 + a);
  s = (s16)AT16(D_8006CBF8 + a);
  u = Rotate(m, c, s, 0);
  v = Rotate(m, (s16)-s, c, 0);
  m->r13r21 = (m->r13r21 & 0xFFFF) + ((u32)u.ir2 << 16);
  m->r11r12 = ((u32)v.ir1 << 16) + ((u32)u.ir1 & 0xFFFF);
  m->r31r32 = ((u32)v.ir3 << 16) + ((u32)u.ir3 & 0xFFFF);
  m->r22r23 = (m->r22r23 & 0xFFFF0000) + ((u32)v.ir2 & 0xFFFF);
}

/* abs, as the asm's `neg` when negative. */
static inline u32 Abs32(u32 v) {
  return (s32)v < 0 ? 0u - v : v;
}

static inline void LoadCam(Cam *cam) {
  const u32 *c = (const u32 *)g_Camera;
  cam->x = c[10];
  cam->y = c[11];
  cam->z = c[12];
  cam->view.r11r12 = c[0];
  cam->view.r13r21 = c[1];
  cam->view.r22r23 = c[2];
  cam->view.r31r32 = c[3];
  cam->view.r33 = c[4];
}

#define CULL_NEXT 0 /* culled (or skipped): the next moby */
#define CULL_KEPT 1 /* a record at rec */
#define CULL_END -1 /* pass 2's records are full: end the list */

/* Cull one moby; when kept, its record at rec and maybe a shadow entry.
 * spec: func_800208FC's (the specular word in the record; pass 2 bounds
 * its records here, the regular loop after each record). */
static inline __attribute__((always_inline)) int CullMoby(u32 moby, u32 rec, u32 *shadow,
                                                          const Cam *cam, int spec) {
  u8 *const m = HPTR(u8, moby);
  const u32 half = AT16(m + MOBY_RADIUS); /* radius | was drawn << 8 */
  u32 x = AT32(m + MOBY_POS), y, z, box, model, anims, anim, depth, r, ex, ez, ey, ey2, t2, t3;
  u32 shadow_dist, flags, detail, progress, angles, frame, next, ds, scale_override, spec_word = 0;
  u32 drawn;
  Mat mat;

  m[MOBY_WAS_DRAWN] = 0;
  if (spec) {
    if (g_lod_pass) {
      if (port_lod_moby_skip(half, moby))
        return CULL_NEXT;
      if (rec + REC_SIZE > PSX_ADDR(D_8006FCF4) + TBL_RECORDS_END)
        return CULL_END;
    }
  } else if (PORT_LOD_MOBY_SKIP(half, moby)) {
    return CULL_NEXT;
  }

  /* A box around the camera, half the radius << 8 wide in the asm's /4
   * units (wider by 0x200 when it was drawn last frame). */
  box = ((half & 0xFF) << 8) + ((half & 0x100) << 1);
  box = PORT_LOD_MOBY_BOX(box, moby);
  x = (u32)((s32)(x - cam->x) >> 2);
  if ((s32)(x + box) <= 0 || (s32)(x - box) >= 0)
    return CULL_NEXT;
  y = (u32)((s32)(cam->y - AT32(m + MOBY_POS + 4)) >> 2);
  if ((s32)(y + box) <= 0 || (s32)(y - box) >= 0)
    return CULL_NEXT;
  z = (u32)((s32)(cam->z - AT32(m + MOBY_POS + 8)) >> 2);
  if ((s32)(z + box) <= 0 || (s32)(z - box) >= 0)
    return CULL_NEXT;

  /* Into camera space (x, y, z: the camera's axes, z the depth). */
  model = MEM_MODEL_AT(AT32(g_Models + AT16(m + MOBY_CLASS) * 4));
  anims = AT32(m + MOBY_ANIMS);
  anim = *HPTR(u32, model + ((anims & 0xFF) << 2) + MODEL_ANIMS);
  MatToGte(&cam->view);
  r = (u32)*HPTR(u8, anim + ANIM_RADIUS) << 4;
  {
    /* the view matrix times (y, z, x), as the asm loads them into IR1-3 */
    const NgteVec v = Rotate(&cam->view, (s16)y, (s16)z, (s16)x);
    z = (u32)v.mac3;
    y = (u32)v.mac2;
    x = (u32)v.mac1;
  }
  ex = (r >> 1) + (r >> 2) + (r >> 5); /* the radius scaled to the view's */
  ez = (r >> 1) + (r >> 4) + (r >> 5); /* sides and its depth */
  /* Beyond the box's depth, behind the camera, or off a side. */
  if ((s32)(z - box) >= 0 || (s32)(z + r) <= 0)
    return CULL_NEXT;
  depth = z;
  t3 = z + ez;
  t2 = (Abs32(x) - ex) << 2;
  if ((s32)(t2 - t3 - (t3 << 1)) >= 0)
    return CULL_NEXT;
  shadow_dist = AT32(m + MOBY_SHADOW_DIST);
  ey = r - (r >> 5) - (r >> 6);
  ey2 = (r >> 2) + (r >> 4);

  /* Its shadow, when on and the moby is near: before the top and bottom
   * test, so a moby just above or below the view keeps its shadow. Pass 2
   * adds none. */
  if (g_lod_pass)
    shadow_dist = 0;
  if ((s32)shadow_dist < 0 && (s32)(z - 0x1200) < 0) {
    const u32 fr = m[MOBY_FRAME];
    u32 b;
    *HPTR(u32, *shadow) = moby;
    b = *HPTR(u8, (fr << 3) + anim + FRAME_SHADOW + ANIM_FRAMES);
    *shadow += 8;
    *HPTR(u32, *shadow - 4) = b;
  }

  t2 = Abs32(y) - ey;
  if ((s32)(z + ey2 - t2 - (t2 << 1)) <= 0)
    return CULL_NEXT;

  /* Clip unless it's well inside the view. */
  flags = REC_CLIP;
  t3 = z - ez;
  t2 = (Abs32(x) + ex) << 2;
  if ((s32)(t2 - t3 - (t3 << 1)) < 0) {
    t2 = Abs32(y) + ey;
    if ((s32)(z - ey2 - t2 - (t2 << 1)) > 0)
      flags = REC_INSIDE;
  }

  /* The record's flags and frames: the depth scale (the larger of the two
   * animations' when blending), the depth offset, progress, scale. */
  detail = (u32)(m[MOBY_DETAIL] & 0x3F) << 8;
  progress = m[MOBY_PROGRESS];
  angles = AT32(m + MOBY_ANGLES);
  detail = PORT_LOD_MOBY_DETAIL(detail);
  detail -= depth;
  frame = ((anims >> 16) & 0xFF) * 8 + ANIM_FRAMES + anim;
  ds = *HPTR(u8, anim + ANIM_DEPTH_SCALE);
  if (progress) {
    const u32 next_anim = *HPTR(u32, model + ((anims & 0xFF00) >> 6) + MODEL_ANIMS);
    const u32 nds = *HPTR(u8, next_anim + ANIM_DEPTH_SCALE);
    flags += progress << 8;
    next = (anims >> 24) * 8 + ANIM_FRAMES + next_anim;
    if ((s32)(ds - nds) < 0)
      ds = nds;
  } else {
    next = 0;
  }
  scale_override = m[MOBY_SCALE];
  flags += ((angles >> 24) << 16) + (ds << 24) + scale_override;

  if (spec)
    spec_word = AT32(m + MOBY_SPECULAR);
  drawn = 1;
  PORT_LOD_MOBY_DRAWN(drawn, moby);
  m[MOBY_WAS_DRAWN] = (u8)drawn;
  AT32(rec + REC_FLAGS) = flags;
  AT32(rec + REC_ANIM) = anim;
  AT32(rec + REC_FRAME) = frame;
  AT32(rec + REC_NEXT) = next;
  AT32(rec + REC_POS) = x;
  AT32(rec + REC_POS + 4) = y;
  AT32(rec + REC_POS + 8) = z;
  AT16(rec + REC_DETAIL) = (u16)detail;
  if (spec)
    AT32(rec + REC_SPECULAR) = spec_word;
  AT32(rec + REC_MOBY) = moby;

  /* Its rotation: the view matrix turned by its z, y and x angles. */
  mat = cam->view;
  TurnZ(&mat, (angles >> 15) & 0x1FE);
  TurnY(&mat, (angles & 0xFF00) >> 7);
  TurnX(&mat, (angles & 0xFF) << 1);
  AT32(rec + REC_MATRIX) = mat.r11r12;
  AT32(rec + REC_MATRIX + 4) = mat.r13r21;
  AT32(rec + REC_MATRIX + 8) = mat.r22r23;
  AT32(rec + REC_MATRIX + 12) = mat.r31r32;
  AT16(rec + REC_MATRIX + 16) = (u16)mat.r33;
  return CULL_KEPT;
}

/* ---- func_8001F158: the regular mobys ----
 * The list at D_8006FCF4+0; the shadow list starts at +0x2800. The records
 * stop at +0x2200 (one more always fits for the end's 0). */
static u32 CullMobys(u32 a0, u32 a1, u32 a2, u32 a3) {
  const u32 tbl = PSX_ADDR(D_8006FCF4), end = tbl + TBL_RECORDS_END;
  u32 list = tbl + TBL_MOBYS, rec = tbl + TBL_RECORDS, shadow = tbl + TBL_SHADOWS;
  Cam cam;

  LoadCam(&cam);
  for (;;) {
    const u32 moby = AT32(list);
    list += 4;
    if (moby) {
      if (CullMoby(moby, rec, &shadow, &cam, 0) == CULL_NEXT)
        continue;
      rec += REC_SIZE;
      if ((s32)(end - (rec + REC_SIZE)) >= 0)
        continue;
    }
    /* the draw distance's pass 2 (lod.c): the list again, longer box */
    if (PORT_LOD_MOBY_PASS2(rec + REC_SIZE <= end, rec)) {
      list = tbl + TBL_MOBYS;
      continue;
    }
    break;
  }
  AT32(rec) = 0;
  ((u32 *)g_MobyShadows)[2] = shadow;
  return PSX_ADDR(g_MobyShadows); /* v0, as the asm leaves it */
}

/* ---- func_800208FC: the specular mobys ----
 * The list at D_8006FCF4+0x2200; the shadow list goes on from where the
 * regular cull left it. No bound on the records but pass 2's. */
static u32 CullSpecular(u32 a0, u32 a1, u32 a2, u32 a3) {
  const u32 tbl = PSX_ADDR(D_8006FCF4), end = tbl + TBL_RECORDS_END;
  u32 list = tbl + TBL_SPECULAR, rec = tbl + TBL_RECORDS;
  u32 shadow = ((const u32 *)g_MobyShadows)[2];
  Cam cam;

  LoadCam(&cam);
  for (;;) {
    const u32 moby = AT32(list);
    list += 4;
    if (moby) {
      const int k = CullMoby(moby, rec, &shadow, &cam, 1);
      if (k == CULL_KEPT)
        rec += REC_SIZE;
      if (k != CULL_END)
        continue;
    }
    if (PORT_LOD_MOBY_PASS2(rec + REC_SIZE <= end, rec)) {
      list = tbl + TBL_SPECULAR;
      continue;
    }
    break;
  }
  AT32(rec) = 0;
  ((u32 *)g_MobyShadows)[2] = shadow;
  return PSX_ADDR(g_MobyShadows); /* v0, as the asm leaves it */
}

HAND_UNIT_FN_RC(func_8001F158, CullMobys)
HAND_UNIT_FN_RC(func_800208FC, CullSpecular)
