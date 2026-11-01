/* The environment renderer, func_800258F0 (asm/renderers/r_environment.s,
 * hand-written asm in the original), written by hand from the asm as C
 * (decisions D69, the D33 method; sky_hand.c is the worked example). It
 * replaces the mips2c translation when fast_render=1 (render_hand.h
 * HAND_RENDERER); fast_render=0 runs the translation, linked as
 * func_800258F0_gen. Checked against PCSX-Redux by the oracle replay and
 * against the translation by host runs (docs/translation-log.md).
 *
 * What it does, in the asm's order:
 *  1. Sector culling (800258F0): each sector's bounding sphere against the
 *     camera's view frustum (MVMVA on the view matrix). A visible sector
 *     goes on the low-poly list (D_8006FCF4+0x2000) when its far edge is past
 *     the LOD distance, on the high-poly list (+0x1C00) when its near edge is
 *     inside it, flagged "close" when that edge is within 0x100. Its geometry
 *     animations (vertex and colour frames, blended with INTPL/DPCS) are
 *     applied for the level of detail it is drawn at.
 *  2. The low-poly pass (80026228): untextured Gouraud quads and triangles.
 *  3. The high-poly pass (800267E4): textured quads and triangles, faded to
 *     the far colours with depth (DPCS). Polygons close to the camera go to
 *     two lists instead: 2x2 subdivision (+0x2000) and 4x4 (+0x1E00).
 *  4. The 2x2 pass (800276B4), the 4x4 quad pass (80028590) and the 4x4
 *     triangle pass (80029500): each polygon is cut into a grid in the
 *     scratchpad, transformed again, and drawn as smaller polygons, with
 *     triangles along the edges shared with less-subdivided neighbours.
 *     4x4 pieces still too large on screen go to a last list.
 *  5. The last list (8002A0B0): each primitive there is split in place into
 *     four (quads) or four (triangles) GT3s, which can queue again.
 *
 * Everything the asm leaves in RAM, the scratchpad and the GTE is the same:
 * the same GTE moves and commands in the same order, the vertex past the end
 * that the pipelined loads leave in V0, and the stores the asm makes before
 * it rejects a polygon (they land in the next primitive's space). */
#include "render_hand.h"

extern char g_Camera[];
extern char g_Environment[];
extern char g_EnvironmentAnimations[];
extern char g_SkipLowPolyWorld[];
extern char g_WorldOT[];
extern char D_8006FCF4[]; /* the sector and polygon lists, polygon flags */
extern char D_800757B0[]; /* the primitive buffer's next free byte */
extern char D_800771C8[]; /* a byte per sector, 0xFF when it was visible */
/* Subdivision tables (asm/data/math.data.s). The words holding scratchpad
 * addresses are relocated to the arena's scratchpad at start (port.c). */
extern char D_8006CF48[]; /* 2x2: the four quads' first grid points */
extern char D_8006CF58[]; /* 4x4: the sixteen quads' first grid points */
extern char D_8006CF98[]; /* 2x2 quad: edge triangles, 4 x 12 bytes */
extern char D_8006CFC8[]; /* 4x4 quad: edge triangles, 4 x 36 bytes */
extern char D_8006D058[]; /* texture corner adjustments */
extern char D_8006D0D8[]; /* 4x4 quad: the four inner points to correct */
extern char D_8006D0E8[]; /* 2x2 triangle: the four triangles */
extern char D_8006D0F8[]; /* 4x4 triangle: the sixteen triangles */
extern char D_8006D138[]; /* 2x2 triangle: edge triangles per rotation */
extern char D_8006D1C8[]; /* 4x4 triangle: edge triangles per rotation */
extern char D_8006D378[]; /* 2x2 triangle: UV pair offsets per rotation */
extern char D_8006D388[]; /* 4x4 triangle: UV pair offsets per rotation */
extern char D_8006D3C8[]; /* triangle UV offsets */
extern char D_8006D5C8[]; /* last list, triangle: seven GT3s */
extern char D_8006D5E4[]; /* last list, quad: twelve GT3s */

#define ADDR(sym) ((u32)(uintptr_t)(sym))
#define LISTS ADDR(D_8006FCF4)
#define LIST_HIGH (LISTS + 0x1C00) /* high-poly sectors */
#define LIST_4X4 (LISTS + 0x1E00)  /* polygons for 4x4 subdivision */
#define LIST_LOW (LISTS + 0x2000)  /* low-poly sectors; then 2x2; then last */
#define SC(off) (SCRATCH_BASE + (u32)(off))
#define SC_LAST_END 0x3FC /* scratchpad word: the last list's end */

/* GTE registers (data, then control) */
enum {
  VXY0 = 0, VZ0 = 1, RGBC = 6, IR0 = 8, IR1 = 9, IR2 = 10, IR3 = 11,
  SXY0 = 12, SXY1 = 13, SXY2 = 14, SZ3 = 19, RGB2 = 22,
  MAC0 = 24, MAC1 = 25, MAC2 = 26, MAC3 = 27,
};
enum { C_RBK = 13, C_GBK = 14, C_BBK = 15, C_RFC = 21, C_GFC = 22, C_BFC = 23, C_FLAG = 31 };
#define gte_mvmva_rt_ir() m2c_gte_0x49E012() /* MVMVA sf=1 RT*IR, no translation */
#define gte_intpl() m2c_gte_0x980011()
#define gte_dpcs() m2c_gte_0x780010()
#define gte_gpf0() m2c_gte_0x190003D()

/* A sector (byte offsets). */
enum {
  SEC_SPHERE = 0x0,     /* bounding sphere centre x << 16 | y */
  SEC_SPHERE_Z = 0x4,   /* centre z << 16 | kind (0xE000) | radius (0x1FFF) */
  SEC_ORIGIN = 0x8,     /* geometry origin x << 16 | y */
  SEC_ORIGIN_Z = 0xC,   /* origin z << 16 | first polygon's flag index */
  SEC_LOW = 0x10,       /* low-poly counts: polygons, colours, vertices */
  SEC_HIGH = 0x14,      /* high-poly counts; top byte: the low-poly size */
  SEC_ANIMS = 0x18,     /* four animation indices, 0xFF none */
  SEC_DATA = 0x1C,      /* low-poly data, then high-poly data */
};
/* SEC_SPHERE_Z kinds */
#define SEC_NO_LOW 0x2000u
#define SEC_NO_HIGH 0x4000u
#define SEC_ALWAYS_LOW 0x8000u

/* The OT slots hold the last primitive (+0) and the first (+4) of a chain;
 * a new one goes on the end. */
static inline void OtAppend(u32 slot, u32 prim) {
  u32 tail = AT32(slot);
  AT32(slot) = prim;
  if (tail) {
    u32 t = MIPS_KUSEG(tail);
    AT16(t) = (u16)prim;
    AT8(t + 2) = (u8)(prim >> 16);
  } else {
    AT32(slot + 4) = prim;
  }
}

/* The asm's screen clip flags, below a vertex's SXY shifted left 5. */
static inline u32 ClipLowFlags(u32 sxy, u32 right) {
  u32 f = sxy << 5;
  if ((s32)(sxy - 0x10000u) <= 0)
    f += 1; /* above */
  if ((s32)(sxy - 0x1000000u) >= 0)
    f += 2; /* below */
  if (sxy & 0xFE00)
    f += right; /* x outside 0..511 */
  return f;
}

/* ===================================================================== */
/* 1. Sector culling and geometry animations                              */
/* ===================================================================== */

/* An animation record: the current frame (u8 at +2), the frame size (u16
 * at +6), the frames' offset (+8) and a table of 8-byte frame pairs (+0xC:
 * blend at +4, frame a at +5, frame b at +6). */
typedef struct {
  u32 a, b, size, blend;
} AnimFrames;

static inline AnimFrames AnimCurrent(u32 list_field, u32 index) {
  AnimFrames f;
  u32 list = MIPS_KUSEG(AT32(ADDR(g_EnvironmentAnimations) + list_field));
  u32 rec = MIPS_KUSEG(AT32(list + index * 4));
  u32 pair = rec + 0xC + AT8(rec + 2) * 8;
  u32 data = rec + AT32(rec + 8);
  f.blend = AT8(pair + 4);
  f.size = AT16(rec + 6);
  f.a = data + AT8(pair + 5) * f.size;
  f.b = data + AT8(pair + 6) * f.size;
  return f;
}

static void AnimCopy(u32 src, u32 size, u32 dst) {
  u32 end = src + size;
  do {
    AT32(dst) = AT32(src);
    src += 4;
    dst += 4;
  } while (src != end);
}

/* Packed vertices (11:11:10), blended between two frames with INTPL. The
 * asm loads a word past each frame's end, and the IR registers keep it. */
static void AnimBlendVertices(const AnimFrames *f, u32 dst) {
  u32 a = f->a, b = f->b, end = f->a + f->size + 4, wa, wb;
  MTC2(f->blend << 4, IR0);
  wa = AT32(a);
  a += 4;
  wb = AT32(b);
  b += 4;
  MTC2(wa >> 21, IR1);
  MTC2((wa >> 10) & 0x7FF, IR2);
  MTC2(wa & 0x3FF, IR3);
  do {
    u32 w;
    CTC2(wb >> 21, C_RFC);
    CTC2((wb >> 10) & 0x7FF, C_GFC);
    CTC2(wb & 0x3FF, C_BFC);
    wa = AT32(a);
    wb = AT32(b);
    gte_intpl();
    dst += 4;
    a += 4;
    b += 4;
    MTC2(wa >> 21, IR1);
    MTC2((wa >> 10) & 0x7FF, IR2);
    MTC2(wa & 0x3FF, IR3);
    w = MFC2(MAC1) << 21;
    w += MFC2(MAC2) << 10;
    w += MFC2(MAC3);
    AT32(dst - 4) = w;
  } while (end != a);
}

/* Colours with a step (the top byte, a word count) to the next one written. */
static void AnimCopyColours(u32 src, u32 size, u32 dst) {
  u32 end = src + size;
  do {
    u32 w = AT32(src);
    src += 4;
    dst += (w >> 22) & 0x3FC;
    AT32(dst) = w & 0xFFFFFF;
  } while (src != end);
}

static inline void LoadFarColour(u32 w) {
  CTC2((w << 4) & 0xFF0, C_RFC);
  CTC2((w >> 4) & 0xFF0, C_GFC);
  CTC2((w >> 12) & 0xFF0, C_BFC);
}

static void AnimBlendColours(const AnimFrames *f, u32 dst) {
  u32 a = f->a, b = f->b, end = f->a + f->size, wa, wb;
  MTC2(f->blend << 4, IR0);
  wa = AT32(a);
  wb = AT32(b);
  do {
    u32 step;
    MTC2(wa & 0xFFFFFF, RGBC);
    LoadFarColour(wb);
    step = (wa >> 22) & 0x3FC;
    gte_dpcs();
    wa = AT32(a + 4);
    wb = AT32(b + 4);
    a += 4;
    b += 4;
    dst += step;
    AT32(dst) = MFC2(RGB2);
  } while (end != a);
}

/* High-poly colours: a near and a far colour per step, into the two tables. */
static void AnimCopyColourPairs(u32 src, u32 size, u32 sec) {
  u32 end = src + size, counts = AT32(sec + SEC_HIGH), near, far;
  near = sec + SEC_DATA + ((counts >> 22) & 0x3FC) + ((counts << 2) & 0x3FC);
  far = near + ((counts >> 6) & 0x3FC);
  do {
    u32 w0 = AT32(src), w1 = AT32(src + 4), step;
    src += 8;
    step = (w0 >> 22) & 0x3FC;
    near += step;
    far += step;
    AT32(near) = w0 & 0xFFFFFF;
    AT32(far) = w1;
  } while (src != end);
}

static void AnimBlendColourPairs(const AnimFrames *f, u32 sec) {
  u32 a = f->a, b = f->b, end, counts, near, far, w0, w1;
  MTC2(f->blend << 4, IR0);
  end = f->a + f->size;
  counts = AT32(sec + SEC_HIGH);
  near = sec + SEC_DATA + ((counts >> 22) & 0x3FC);
  w0 = AT32(a);
  near += (counts << 2) & 0x3FC;
  w1 = AT32(b);
  far = near + ((counts >> 6) & 0x3FC);
  do {
    u32 step;
    MTC2(w0 & 0xFFFFFF, RGBC);
    LoadFarColour(w1);
    step = (w0 >> 22) & 0x3FC;
    gte_dpcs();
    w0 = AT32(a + 4);
    w1 = AT32(b + 4);
    near += step;
    AT32(near) = MFC2(RGB2);
    MTC2(w0, RGBC);
    LoadFarColour(w1);
    a += 8;
    b += 8;
    gte_dpcs();
    w0 = AT32(a);
    w1 = AT32(b);
    far += step;
    AT32(far) = MFC2(RGB2);
  } while (a != end);
}

/* g_EnvironmentAnimations' lists for the four kinds a sector can name. */
enum { ANIM_LOW_POLY = 0x14, ANIM_LOW_COLOUR = 0x1C, ANIM_HIGH_POLY = 0x24, ANIM_HIGH_COLOUR = 0x2C };

static void SectorAnimate(u32 sec, u32 anims) {
  AnimFrames f;
  if ((s32)(anims << 24) >= 0) {
    AT8(sec + SEC_ANIMS) = 0xFF;
    f = AnimCurrent(ANIM_LOW_POLY, anims & 0xFF);
    if ((s32)f.blend > 0)
      AnimBlendVertices(&f, sec + SEC_DATA);
    else
      AnimCopy(f.a, f.size, sec + SEC_DATA);
  }
  if ((s32)(anims << 16) >= 0) {
    u32 dst;
    AT8(sec + SEC_ANIMS + 1) = 0xFF;
    f = AnimCurrent(ANIM_LOW_COLOUR, (anims >> 8) & 0xFF);
    dst = sec + SEC_DATA + AT8(sec + SEC_LOW) * 4;
    if ((s32)f.blend > 0)
      AnimBlendColours(&f, dst);
    else
      AnimCopyColours(f.a, f.size, dst);
  }
  if ((s32)(anims << 8) >= 0) {
    u32 dst;
    AT8(sec + SEC_ANIMS + 2) = 0xFF;
    f = AnimCurrent(ANIM_HIGH_POLY, (anims >> 16) & 0xFF);
    dst = sec + SEC_DATA + AT8(sec + SEC_HIGH + 3) * 4;
    if ((s32)f.blend > 0)
      AnimBlendVertices(&f, dst);
    else
      AnimCopy(f.a, f.size, dst);
  }
  if ((s32)anims >= 0) {
    AT8(sec + SEC_ANIMS + 3) = 0xFF;
    f = AnimCurrent(ANIM_HIGH_COLOUR, anims >> 24);
    if ((s32)f.blend > 0)
      AnimBlendColourPairs(&f, sec);
    else
      AnimCopyColourPairs(f.a, f.size, sec);
  }
}

/* The camera, in the units the passes use */
typedef struct {
  s32 x, y, z; /* g_Camera's position >> 4 */
} CamPos;

static void SectorLists(s32 group, const CamPos *cam) {
  const u32 env = ADDR(g_Environment);
  const u32 lod = PORT_LOD_ENV(AT32(env + 0x24)) >> 4; /* m_LodDistance */
  const u32 sectors = AT32(env + 0x0);                    /* m_SectorPointer */
  u32 low = LIST_LOW, high = LIST_HIGH;
  u32 i = 0, n4 = AT32(env + 0x4) << 2; /* m_SectorCount */
  const u8 *group_list = 0;
  int s;

  for (s = 0; s < 0x100; s += 4)
    AT32(ADDR(D_800771C8) + s) = 0;
  if (group >= 0)
    group_list = HPTR(const u8, AT32(MIPS_KUSEG(AT32(env + 0x8)) + (u32)group * 4));

  for (;;) {
    u32 raw, sec, w0, w1, kind, r, ra, rb, rc, rd, depth, sx, sy, v, anims, entry, seen, mask;
    if (group >= 0) {
      u32 idx = *group_list++;
      if (idx == 0xFF)
        break;
      raw = AT32(MIPS_KUSEG(sectors + idx * 4));
      seen = ADDR(D_800771C8) + idx;
    } else {
      if (i == n4)
        break;
      raw = AT32(MIPS_KUSEG(sectors + i));
      seen = ADDR(D_800771C8) + (i >> 2);
      i += 4;
    }
    sec = MIPS_KUSEG(raw);
    w0 = AT32(sec + SEC_SPHERE);
    w1 = AT32(sec + SEC_SPHERE_Z);
    entry = raw | 1; /* "clip against the screen" */
    MTC2((w0 >> 16) - (u32)cam->x, IR3);
    MTC2((u32)cam->y - (w0 & 0xFFFF), IR1);
    MTC2((u32)cam->z - (w1 >> 16), IR2);
    kind = w1 & 0xE000;
    r = w1 & 0x1FFF;
    gte_mvmva_rt_ir();
    /* the frustum's slopes, as fractions of the radius */
    ra = (r >> 1) + (r >> 2) + (r >> 5);
    rb = (r >> 1) + (r >> 4) + (r >> 5);
    depth = MFC2(IR3);
    sx = MFC2(IR1);
    if ((s32)(depth + r) <= 0)
      continue; /* behind the camera */
    if ((s32)sx < 0)
      sx = -sx;
    v = depth + rb;
    if ((s32)(((sx - ra) << 2) - (v + (v << 1))) >= 0)
      continue; /* off the side */
    rc = r - (r >> 3);
    rd = (r >> 1) - (r >> 4);
    sy = MFC2(IR2);
    v = depth + rd;
    if ((s32)sy < 0)
      sy = -sy;
    if ((s32)(((sy - rc) << 5) - (v + (v << 4))) >= 0)
      continue; /* off the top or bottom */
    anims = AT32(sec + SEC_ANIMS);
    if ((s32)(depth - r) > 0) {
      v = depth - rb;
      if ((s32)(((sx + ra) << 2) - (v + (v << 1))) < 0) {
        v = depth - rd;
        if ((s32)(((sy + rc) << 5) - (v + (v << 4))) < 0)
          entry ^= 1; /* wholly inside the view */
      }
    }

    mask = 0xFFFFFFFFu; /* which animations to skip: low halves, high halves */
    AT8(seen) = 0xFF;
    if (!(kind & SEC_NO_LOW)) {
      /* the asm stores the entry before it tests the distance */
      AT32(low) = entry;
      if ((kind & SEC_ALWAYS_LOW) || (s32)(lod - (depth + r + 0x100)) < 0) {
        low += 4;
        mask <<= 16;
      }
    }
    if (!(kind & SEC_NO_HIGH)) {
      u32 near_edge = depth - r;
      if ((s32)(lod - near_edge) > 0) {
        mask &= 0xFFFF;
        if ((s32)(near_edge - 0x100) < 0)
          entry += 2; /* close: subdivide */
        if (!PORT_LOD_NEAR_FULL(high)) {
          AT32(high) = entry;
          high += 4;
        }
      }
    }
    anims |= mask;
    if (anims != 0xFFFFFFFFu)
      SectorAnimate(sec, anims);
  }
  PORT_LOD_ENV_LISTS(high);
  AT32(low) = 0;
  AT32(high) = 0;
}

/* ===================================================================== */
/* 2. The low-poly pass: untextured quads and triangles                   */
/* ===================================================================== */

/* A low-poly vertex (z 11 bits, x 11, y 10, relative to the sector's
 * origin) into V0. */
static inline void LoadLowVertex(u32 w, u32 oz, u32 ox, u32 oy) {
  u32 z = (w >> 21) + oz;
  u32 x = ox - ((w >> 10) & 0x7FF);
  u32 y = oy - (w & 0x3FF);
  MTC2(z, VZ0);
  MTC2(x + (y << 16), VXY0);
}

/* The 3-word primitive that sets a semi-transparent polygon's blend mode
 * (the tpage bits from the polygon's colour word), before the polygon. */
static inline u32 EmitBlendMode(u32 fp, u32 slot, u32 w1) {
  AT32(fp) = 0x2000000;
  AT32(fp + 8) = 0;
  AT16(fp + 4) = (u16)(((w1 << 5) & 0x7F) | 0x600);
  AT16(fp + 6) = 0xE100;
  OtAppend(slot, fp);
  return fp + 0xC;
}

/* Low-poly polygon (8 bytes): word 0 the four vertices (6 bits each, x8 in
 * the scratchpad), double-sided (0x80) and a depth bias (0x1F); word 1 the
 * four colours (6 bits each, x4), an OT bias (0xF8), semi-transparent (4)
 * and its blend mode (3). Quads are POLY_G4, triangles POLY_G3. */
static u32 DrawLowSectors(u32 fp, u32 ot, const CamPos *cam) {
  const u32 env = ADDR(g_Environment);
  const u32 cull = AT32(env + 0x28) >> 7; /* m_CullingDistance */
  const u32 lod = (PORT_LOD_ENV(AT32(env + 0x24)) >> 7) - 0x20;
  u32 list = LIST_LOW;

  for (;;) {
    u32 entry, clip, sec, w, oz, ox, oy, counts, vp, vend, next, scr, all, cols, poly, pend;
    entry = AT32(list);
    list += 4;
    if (PORT_LOD_ENV_FULL(fp, 0))
      entry = 0;
    if (!entry)
      break;
    clip = entry & 1;
    sec = MIPS_KUSEG(entry ^ clip);
    w = AT32(sec + SEC_ORIGIN);
    oz = (w >> 16) - (u32)cam->x;
    ox = (u32)cam->y - (w & 0xFFFF);
    oy = (u32)cam->z - AT16(sec + SEC_ORIGIN_Z + 2);
    counts = AT32(sec + SEC_LOW);

    /* Transform the vertices into the scratchpad, 8 bytes each: SXY (or,
     * clipping, SXY << 5 | flags) and SZ. The asm loads two vertices ahead,
     * so V0 ends up holding the word after the last (the first colour). */
    vp = sec + SEC_DATA;
    scr = SCRATCH_BASE;
    all = ~0u;
    w = AT32(vp);
    vp += 4;
    vend = vp + (counts & 0xFF) * 4 + 4;
    LoadLowVertex(w, oz, ox, oy);
    w = AT32(vp);
    vp += 4;
    if (!clip) {
      do {
        u32 sxy, sz;
        gte_rtps();
        next = AT32(vp);
        LoadLowVertex(w, oz, ox, oy);
        w = next;
        sxy = MFC2(SXY2);
        sz = MFC2(SZ3);
        vp += 4;
        AT32(scr) = sxy;
        AT32(scr + 4) = sz;
        scr += 8;
      } while (vp != vend);
    } else {
      do {
        u32 sxy, sz, f;
        gte_rtps();
        next = AT32(vp);
        LoadLowVertex(w, oz, ox, oy);
        w = next;
        sxy = MFC2(SXY2);
        sz = MFC2(SZ3);
        vp += 4;
        f = ClipLowFlags(sxy, 4);
        AT32(scr + 4) = sz;
        all &= f;
        AT32(scr) = f;
        scr += 8;
      } while (vp != vend);
      if (all & 0xF)
        continue; /* every vertex off the same side */
    }

    cols = vend - 8;
    poly = cols + ((counts >> 6) & 0x3FC);
    pend = poly + ((counts >> 13) & 0x7F8);
    for (; poly != pend; poly += 8) {
      const u32 w0 = AT32(poly), w1 = AT32(poly + 4);
      const u32 p0 = SC((w0 >> 23) & 0x1F8), p1 = SC((w0 >> 17) & 0x1F8);
      const u32 i2 = (w0 >> 11) & 0x1F8, i3 = (w0 >> 5) & 0x1F8;
      const u32 two_sided = w0 & 0x80, bias8 = (w0 & 0x1F) << 3;
      u32 xy0, xy1, xy2, z0, z1, z2, depth, fade, c0, slot, code;
      xy0 = AT32(p0);
      xy1 = AT32(p1);
      xy2 = AT32(SC(i2));
      if (i2 != i3) {
        /* a quad */
        u32 xy3 = AT32(SC(i3)), z3, zsum, area, back = 0;
        if (clip) {
          if ((s32)(xy0 & xy1 & xy2 & xy3 & 0xF) > 0)
            continue;
          xy0 = (u32)((s32)xy0 >> 5);
          xy1 = (u32)((s32)xy1 >> 5);
          xy2 = (u32)((s32)xy2 >> 5);
          xy3 = (u32)((s32)xy3 >> 5);
        }
        MTC2(xy0, SXY0);
        MTC2(xy1, SXY1);
        MTC2(xy2, SXY2);
        z0 = AT32(p0 + 4);
        z1 = AT32(p1 + 4);
        gte_nclip();
        z2 = AT32(SC(i2) + 4);
        z3 = AT32(SC(i3) + 4);
        zsum = z0 + z1 + z2 + z3;
        area = MFC2(MAC0);
        MTC2(xy3, SXY0);
        if ((s32)area <= 0) {
          /* the other half: (3, 1, 2) */
          gte_nclip();
          back = !two_sided;
        }
        depth = zsum >> 5;
        if ((s32)(depth - cull) >= 0)
          continue;
        if (back && (s32)MFC2(MAC0) >= 0)
          continue;
        fade = bias8 + depth - lod;
        if ((s32)fade < 0) {
          u32 d;
          if ((s32)(fade + 0x20) <= 0)
            continue;
          d = (lod - bias8) << 3;
          if ((s32)((z0 - d) & (z1 - d) & (z2 - d) & (z3 - d)) < 0)
            continue;
        }
        c0 = AT32(cols + ((w1 >> 24) & 0xFC));
        {
          u32 c1 = AT32(cols + ((w1 >> 18) & 0xFC));
          u32 c2 = AT32(cols + ((w1 >> 12) & 0xFC));
          u32 c3 = AT32(cols + ((w1 >> 6) & 0xFC));
          slot = ot + (((depth + (w1 & 0xF8)) + 0x40) << 3);
          code = 0x38000000;
          if (w1 & 4) {
            code += 0x2000000;
            fp = EmitBlendMode(fp, slot, w1);
          }
          AT32(fp) = 0x8000000;
          AT32(fp + 0x4) = c0 + code;
          AT32(fp + 0x8) = xy0;
          AT32(fp + 0xC) = c1;
          AT32(fp + 0x10) = xy1;
          AT32(fp + 0x14) = c2;
          AT32(fp + 0x18) = xy2;
          AT32(fp + 0x1C) = c3;
          AT32(fp + 0x20) = xy3;
          OtAppend(slot, fp);
          fp += 0x24;
        }
      } else {
        /* a triangle */
        u32 area;
        if (clip) {
          if ((s32)(xy0 & xy1 & xy2 & 0x1F) > 0)
            continue;
          xy0 = (u32)((s32)xy0 >> 5);
          xy1 = (u32)((s32)xy1 >> 5);
          xy2 = (u32)((s32)xy2 >> 5);
        }
        MTC2(xy0, SXY0);
        MTC2(xy1, SXY1);
        MTC2(xy2, SXY2);
        gte_nclip();
        area = MFC2(MAC0) + (two_sided << 23);
        z0 = AT32(p0 + 4);
        z1 = AT32(p1 + 4);
        if ((s32)area <= 0)
          continue;
        z2 = AT32(SC(i2) + 4);
        depth = (z0 + z1 + z2 + z2) >> 5;
        if ((s32)(depth - cull) >= 0)
          continue;
        c0 = AT32(cols + ((w1 >> 24) & 0xFC));
        fade = bias8 + depth - lod;
        if ((s32)fade < 0) {
          u32 d;
          if ((s32)(fade + 0x20) <= 0)
            continue;
          d = (lod - bias8) << 3;
          if ((s32)((z0 - d) & (z1 - d) & (z2 - d)) < 0)
            continue;
        }
        {
          u32 c1 = AT32(cols + ((w1 >> 18) & 0xFC));
          u32 c2 = AT32(cols + ((w1 >> 12) & 0xFC));
          slot = ot + (((depth + (w1 & 0xF8)) + 0x40) << 3);
          code = 0x30000000;
          if (w1 & 4) {
            code += 0x2000000;
            fp = EmitBlendMode(fp, slot, w1);
          }
          AT32(fp) = 0x6000000;
          AT32(fp + 0x4) = c0 + code;
          AT32(fp + 0x8) = xy0;
          AT32(fp + 0xC) = c1;
          AT32(fp + 0x10) = xy1;
          AT32(fp + 0x14) = c2;
          AT32(fp + 0x18) = xy2;
          OtAppend(slot, fp);
          fp += 0x1C;
        }
      }
    }
  }
  return fp;
}

/* ===================================================================== */
/* 3. The high-poly pass: textured quads and triangles                    */
/* ===================================================================== */

/* A high-poly vertex (z, x 11 bits x4, y 10 bits x4, relative to the
 * sector's origin x4, which the GTE's background colour holds). */
static inline u32 HighVertexZ(u32 w, u32 ox) { return ((w >> 19) & 0x1FFC) + ox; }
static inline u32 HighVertexXY(u32 w, u32 oy, u32 oz) {
  u32 x = oy - ((w >> 8) & 0x1FFC);
  u32 y = oz - ((w << 2) & 0xFFC);
  return x + (y << 16);
}

/* A vertex's colour faded from its near colour to its far one: F is 0x1000
 * at full near colour (DPCS: far + (near - far) * F). */
static inline u32 FadeColour(u32 near_addr, u32 far_addr, u32 f) {
  u32 near = AT32(near_addr), far;
  if ((s32)(f - 0x1000) >= 0)
    return near;
  far = AT32(far_addr);
  if ((s32)f <= 0)
    return far;
  CTC2((near << 4) & 0xFF0, C_RFC);
  CTC2((near >> 4) & 0xFF0, C_GFC);
  CTC2(near >> 12, C_BFC);
  MTC2(far, RGBC);
  MTC2(f, IR0);
  gte_dpcs();
  return MFC2(RGB2);
}

/* The two subdivision lists the high-poly pass fills: a sector's address
 * >> 2 (positive), then its polygons' addresses | 1 (a vertex's projection
 * overflowed) | 2 (a triangle). */
typedef struct {
  u32 l2x2, l4x4;
} SubdivLists;

/* High-poly polygon (16 bytes): word 0 the four vertices (8 bits each, x4
 * in the scratchpad), word 1 the four colours (x4), word 2 the texture
 * (signed byte; -1 none; bit 7 semi-transparent), the UV rotation (0x300)
 * and two neighbours (12-bit signed polygon-flag offsets), word 3 no fade
 * (1), reversed winding (2), double-sided (4), an OT bias (0x38), "2x2 at
 * most" (0x40), never subdivided (0x80) and two more neighbours. */
static u32 DrawHighSectors(u32 fp, u32 ot, SubdivLists *lists) {
  const u32 env = ADDR(g_Environment);
  const u32 lod = PORT_LOD_ENV(AT32(env + 0x24));
  const u32 lqtex = AT32(env + 0x18); /* m_LQTexturePointer */
  u32 list = LIST_HIGH, l2x2 = LIST_LOW, l4x4 = LIST_4X4;
  {
    const u32 cam = ADDR(g_Camera);
    CTC2(AT32(cam + 0x28) >> 2, C_RBK);
    CTC2(AT32(cam + 0x2C) >> 2, C_GBK);
    CTC2(AT32(cam + 0x30) >> 2, C_BBK);
  }

  for (;;) {
    u32 entry, mode, sraw, sec, kdelta, w, w3, ox, oy, oz, pf, counts, vp, vend, next, all, vz, vxy;
    u32 scr, sz16, near_t, far_t, poly, pend;
    int clip_tests = 1;
    entry = AT32(list);
    list += 4;
    if (PORT_LOD_ENV_FULL(fp, 1))
      entry = 0;
    if (!entry)
      break;
    mode = entry & 3; /* 1: clip, 2: close */
    sraw = entry ^ mode;
    sec = MIPS_KUSEG(sraw);
    kdelta = sec - sraw;
    w = AT32(sec + SEC_ORIGIN);
    w3 = AT32(sec + SEC_ORIGIN_Z);
    ox = (w >> 14) - CFC2(C_RBK);
    oy = CFC2(C_GBK) - ((w & 0xFFFF) << 2);
    oz = CFC2(C_BBK) - (w3 >> 14);
    pf = LISTS + (w3 & 0xFFFF); /* this sector's polygon flags */
    counts = AT32(sec + SEC_HIGH);

    /* Transform: SXY (or flags) to the scratchpad's words, SZ to its
     * halves from 0x2A8. */
    scr = SCRATCH_BASE;
    sz16 = SC(0x2A8);
    all = ~0u;
    vp = sec + SEC_DATA + ((counts >> 22) & 0x3FC);
    w = AT32(vp);
    vp += 4;
    vend = vp + (counts & 0xFF) * 4 + 4;
    vz = HighVertexZ(w, ox);
    vxy = HighVertexXY(w, oy, oz);
    w = AT32(vp);
    MTC2(vz, VZ0);
    MTC2(vxy, VXY0);
    vp += 4;
    if (mode & 2) {
      /* close: a vertex near the view axis is projected again at 16x for
       * precision; an overflowed projection is flagged (0x10) */
      do {
        u32 sxy, sz, flag, f;
        gte_rtps();
        next = AT32(vp);
        vz = HighVertexZ(w, ox);
        vxy = HighVertexXY(w, oy, oz);
        w = next;
        vp += 4;
        sz = MFC2(SZ3);
        sxy = MFC2(SXY2);
        scr += 4;
        sz16 += 2;
        if ((s32)(sz - 0x600) >= 0) {
          MTC2(vz, VZ0);
          MTC2(vxy, VXY0);
          f = ClipLowFlags(sxy, 0xC);
          AT16(sz16 - 2) = (u16)sz;
        } else {
          if ((s32)(sz - 0x100) < 0) {
            u32 m1 = MFC2(MAC1), m2 = MFC2(MAC2);
            if ((s32)(m1 + 0x100) > 0 && (s32)(m1 - 0x100) < 0 && (s32)(m2 + 0x100) > 0 &&
                (s32)(m2 - 0x100) < 0) {
              u32 z = MFC2(VZ0), xy = MFC2(VXY0);
              MTC2(z << 4, VZ0);
              MTC2((xy << 4) & 0xFFF0FFF0u, VXY0);
              gte_rtps();
              sxy = MFC2(SXY2);
            }
          }
          flag = CFC2(C_FLAG);
          MTC2(vz, VZ0);
          MTC2(vxy, VXY0);
          f = sxy << 5;
          if ((s32)flag < 0)
            f += 0x10;
          if ((s32)(sxy - 0x10000u) <= 0)
            f += 1;
          if ((s32)(sxy - 0x1000000u) >= 0)
            f += 2;
          if ((s32)(sxy << 16) <= 0)
            f += 4;
          AT16(sz16 - 2) = (u16)sz;
          if ((s32)((sxy << 16) - 0x2000000u) >= 0)
            f += 8;
        }
        all &= f;
        AT32(scr - 4) = f;
      } while (vp != vend);
      if (all & 0xF)
        continue;
    } else if (mode & 1) {
      do {
        u32 sxy, sz, f;
        gte_rtps();
        next = AT32(vp);
        vz = HighVertexZ(w, ox);
        vxy = HighVertexXY(w, oy, oz);
        w = next;
        vp += 4;
        MTC2(vz, VZ0);
        MTC2(vxy, VXY0);
        sxy = MFC2(SXY2);
        sz = MFC2(SZ3);
        f = ClipLowFlags(sxy, 0xC);
        AT16(sz16) = (u16)sz;
        all &= f;
        AT32(scr) = f;
        scr += 4;
        sz16 += 2;
      } while (vp != vend);
      if (all & 0xF)
        continue;
    } else {
      do {
        u32 sxy, sz;
        gte_rtps();
        next = AT32(vp);
        vz = HighVertexZ(w, ox);
        vxy = HighVertexXY(w, oy, oz);
        w = next;
        vp += 4;
        MTC2(vz, VZ0);
        MTC2(vxy, VXY0);
        sxy = MFC2(SXY2);
        sz = MFC2(SZ3);
        AT32(scr) = sxy;
        AT16(sz16) = (u16)sz;
        scr += 4;
        sz16 += 2;
      } while (vp != vend);
      /* the asm clears the flag pointer's sign bit, which its later sign
       * tests read as "no clipping" (the pointer has bit 31 set otherwise) */
      clip_tests = 0;
    }

    near_t = vend - 8;
    far_t = near_t + ((counts >> 6) & 0x3FC);
    poly = far_t + ((counts >> 6) & 0x3FC);
    pend = poly + ((counts >> 12) & 0xFF0);
    sz16 = SC(0x2A8);
    for (; poly != pend; poly += 16, pf++) {
      const u32 w0 = AT32(poly);
      const u32 i0 = (w0 >> 22) & 0x3FC, i1 = (w0 >> 14) & 0x3FC;
      const u32 i2 = (w0 >> 6) & 0x3FC, i3 = (w0 << 2) & 0x3FC;
      const u32 w3p = AT32(poly + 12);
      u32 xy0 = AT32(SC(i0)), xy1 = AT32(SC(i1)), xy2 = AT32(SC(i2)), err = 0;
      u32 z0, z1, z2, zsum, zavg, slot, w1, fbase, t0, texbase, tri = 0;
      if (i2 != i3) {
        /* a quad, POLY_GT4 in the order 0, 1, 3, 2 */
        u32 xy3 = AT32(SC(i3)), z3;
        if (clip_tests) {
          if ((s32)(xy0 & xy1 & xy2 & xy3 & 0xF) > 0)
            continue;
          err = (xy0 | xy1 | xy2 | xy3) & 0x10;
          xy0 = (u32)((s32)xy0 >> 5);
          xy1 = (u32)((s32)xy1 >> 5);
          xy2 = (u32)((s32)xy2 >> 5);
          xy3 = (u32)((s32)xy3 >> 5);
        }
        MTC2(xy0, SXY0);
        MTC2(xy1, SXY1);
        MTC2(xy3, SXY2);
        gte_nclip();
        if (!err) {
          s32 a = (s32)MFC2(MAC0);
          if (w3p & 2)
            a = -a;
          if (a < 0) {
            MTC2(xy2, SXY0);
            gte_nclip();
            if (!(w3p & 4)) {
              s32 b = (s32)MFC2(MAC0);
              if ((w3p & 2) ? b < 0 : b > 0)
                continue; /* back-facing */
            }
          }
        }
        z0 = AT16(sz16 + (i0 >> 1));
        z1 = AT16(sz16 + (i1 >> 1));
        z2 = AT16(sz16 + (i2 >> 1));
        z3 = AT16(sz16 + (i3 >> 1));
        zsum = z0 + z1 + z2 + z3;
        if ((s32)(lod - zsum) <= 0)
          continue;
        if ((s32)(zsum - 0x2000) < 0 && !(w3p & 0x80)) {
          int very = (s32)(z0 - 0x140) < 0 || (s32)(z1 - 0x140) < 0 || (s32)(z2 - 0x140) < 0 ||
                     (s32)(z3 - 0x140) < 0;
          if (very) {
            if (zsum == 0)
              continue;
            if (w3p & 0x40)
              goto queue_2x2;
            goto queue_4x4;
          }
          if (!(w3p & 0x40))
            goto queue_2x2;
        }
        /* draw it */
        w1 = AT32(poly + 4);
        zavg = zsum >> 7;
        slot = ((zavg + ((w3p & 0x38) >> 1)) << 3);
        AT32(fp + 0x8) = xy0;
        AT32(fp + 0x14) = xy1;
        AT32(fp + 0x20) = xy3;
        AT32(fp + 0x2C) = xy2;
        fbase = lod >> 2;
        if (w3p & 1)
          fbase += 0x2000;
        {
          u32 ci, c3, b;
          ci = (w1 >> 22) & 0x3FC;
          AT32(fp + 0x4) = FadeColour(near_t + ci, far_t + ci, fbase - z0) | 0x3C000000;
          ci = (w1 >> 14) & 0x3FC;
          AT32(fp + 0x10) = FadeColour(near_t + ci, far_t + ci, fbase - z1);
          ci = (w1 >> 6) & 0x3FC;
          AT32(fp + 0x28) = FadeColour(near_t + ci, far_t + ci, fbase - z2);
          ci = (w1 << 2) & 0x3FC;
          c3 = FadeColour(near_t + ci, far_t + ci, fbase - z3);
          b = (u32)(s32)(s8)AT8(poly + 8);
          t0 = (zavg << 5) - fbase + 0x1000;
          texbase = lqtex;
          if ((s32)t0 > 0) {
            t0 >>= 8;
          } else {
            t0 = 0;
            texbase += 8; /* the texture's far half */
          }
          AT8(pf) = 1;
          AT32(fp + 0x1C) = c3;
          if ((s32)b < 0) {
            AT8(fp + 7) = 0x3E;
            if (b == 0xFFFFFFFFu) {
              /* no texture: POLY_G4 */
              u32 a = AT32(fp + 0x14), bb = AT32(fp + 0x20), c = AT32(fp + 0x2C);
              u32 d = AT32(fp + 0x10), e = AT32(fp + 0x1C), f = AT32(fp + 0x28);
              AT32(fp + 0x10) = a;
              AT32(fp + 0x18) = bb;
              AT32(fp + 0x20) = c;
              AT32(fp + 0xC) = d;
              AT32(fp + 0x14) = e;
              AT32(fp + 0x1C) = f;
              AT8(fp + 7) = 0x38;
              AT32(fp) = 0x8000000;
              OtAppend(ot + slot, fp);
              fp += 0x24;
              continue;
            }
          }
          {
            u32 tex = MIPS_KUSEG(texbase + ((b & 0x7F) << 4));
            u32 uv0 = AT32(tex) + (t0 << 22), uv1 = AT32(tex + 4);
            AT32(fp + 0xC) = uv0;
            AT32(fp + 0x18) = uv1;
            AT32(fp + 0x24) = uv0 + 0x1F00;
            AT32(fp + 0x30) = uv0 + 0x1F00 + 0x1F;
            AT32(fp) = 0xC000000;
            OtAppend(ot + slot, fp);
            fp += 0x34;
          }
        }
        continue;
      }

      /* a triangle, POLY_GT3 */
      tri = 2;
      if (clip_tests) {
        if ((s32)(xy0 & xy1 & xy2 & 0xF) > 0)
          continue;
        err = (xy0 | xy1 | xy2) & 0x10;
        xy0 = (u32)((s32)xy0 >> 5);
        xy1 = (u32)((s32)xy1 >> 5);
        xy2 = (u32)((s32)xy2 >> 5);
      }
      MTC2(xy0, SXY0);
      MTC2(xy1, SXY1);
      MTC2(xy2, SXY2);
      gte_nclip();
      if (!(w3p & 4) && !err) {
        s32 a = (s32)MFC2(MAC0);
        if (w3p & 2)
          a = -a;
        if (a < 0)
          continue;
      }
      z0 = AT16(sz16 + (i0 >> 1));
      z1 = AT16(sz16 + (i1 >> 1));
      z2 = AT16(sz16 + (i2 >> 1));
      zsum = z0 + z1 + z2 + z2;
      if ((s32)(lod - zsum) <= 0)
        continue;
      if ((s32)(zsum - 0x2000) < 0 && !(w3p & 0x80)) {
        int very = (s32)(z0 - 0x140) < 0 || (s32)(z1 - 0x140) < 0 || (s32)(z2 - 0x140) < 0;
        if (very) {
          if (zsum == 0)
            continue;
          if (w3p & 0x40)
            goto queue_2x2;
          goto queue_4x4;
        }
        if (!(w3p & 0x40))
          goto queue_2x2;
      }
      w1 = AT32(poly + 4);
      zavg = zsum >> 7;
      slot = ((zavg + ((w3p & 0x38) >> 1)) << 3);
      AT32(fp + 0x8) = xy0;
      AT32(fp + 0x14) = xy1;
      AT32(fp + 0x20) = xy2;
      fbase = lod >> 2;
      if (w3p & 1)
        fbase += 0x2000;
      {
        u32 ci, c2, w2, uv0, uv1, tex, rot;
        ci = (w1 >> 22) & 0x3FC;
        AT32(fp + 0x4) = FadeColour(near_t + ci, far_t + ci, fbase - z0) | 0x34000000;
        ci = (w1 >> 14) & 0x3FC;
        AT32(fp + 0x10) = FadeColour(near_t + ci, far_t + ci, fbase - z1);
        ci = (w1 >> 6) & 0x3FC;
        c2 = FadeColour(near_t + ci, far_t + ci, fbase - z2);
        w2 = AT32(poly + 8);
        t0 = (zavg << 5) - fbase + 0x1000;
        texbase = lqtex;
        if ((s32)t0 > 0) {
          t0 >>= 8;
        } else {
          t0 = 0;
          texbase += 8;
        }
        AT8(pf) = 1;
        AT32(fp + 0x1C) = c2;
        if ((s32)(w2 << 24) < 0) {
          AT8(fp + 7) = 0x36;
          if ((w2 & 0xFF) == 0xFF) {
            /* no texture: POLY_G3 */
            u32 a = AT32(fp + 0x14), b = AT32(fp + 0x20), c = AT32(fp + 0x10), d = AT32(fp + 0x1C);
            AT32(fp + 0x10) = a;
            AT32(fp + 0x18) = b;
            AT32(fp + 0xC) = c;
            AT32(fp + 0x14) = d;
            AT8(fp + 7) = 0x30;
            AT32(fp) = 0x6000000;
            OtAppend(ot + slot, fp);
            fp += 0x1C;
            continue;
          }
        }
        tex = MIPS_KUSEG(texbase + ((w2 & 0x7F) << 4));
        uv0 = AT32(tex) + (t0 << 22);
        uv1 = AT32(tex + 4);
        rot = w2 & 0x300;
        if (rot == 0) {
          AT32(fp + 0xC) = uv0;
          AT32(fp + 0x18) = uv1;
          AT32(fp + 0x24) = uv0 + 0x1F00;
        } else if (rot == 0x100) {
          AT32(fp + 0xC) = uv0 + 0x1F;
          AT32(fp + 0x18) = uv1 + 0x1F00;
          AT32(fp + 0x24) = uv0;
        } else if (rot == 0x200) {
          AT32(fp + 0xC) = uv0 + 0x1F1F;
          AT32(fp + 0x18) = uv1 + 0x1EE1;
          AT32(fp + 0x24) = uv1;
        } else {
          AT32(fp + 0xC) = uv0 + 0x1F00;
          AT32(fp + 0x18) = uv1 - 0x1F;
          AT32(fp + 0x24) = uv0 + 0x1F1F;
        }
        AT32(fp) = 0x9000000;
        OtAppend(ot + slot, fp);
        fp += 0x28;
      }
      continue;

    queue_2x2:
      if (!(sraw & 1))
        AT32(l2x2) = sraw >> 2, l2x2 += 4;
      sraw |= 1;
      AT32(l2x2) = ((poly - kdelta) + (err >> 4)) | tri;
      l2x2 += 4;
      AT8(pf) = 2;
      continue;
    queue_4x4:
      if (!(sraw & 2))
        AT32(l4x4) = sraw >> 2, l4x4 += 4;
      sraw |= 2;
      AT32(l4x4) = ((poly - kdelta) + (err >> 4)) | tri;
      l4x4 += 4;
      AT8(pf) = 4;
      continue;
    }
  }
  lists->l2x2 = l2x2;
  lists->l4x4 = l4x4;
  return fp;
}

/* ===================================================================== */
/* 4. Subdivision: grids in the scratchpad                                */
/* ===================================================================== */

/* A grid point, 16 bytes at SC(k * 0x10): the vertex's z, x, y (halves at
 * +0, +2, +4) and its colour (+8); after the transform, SXY (+0) and SZ with
 * clip flags above it (+4: 0x1000 above, 0x2000 below, 0x4000 left, 0x8000
 * right of the screen; 0xF000 behind the camera, in the 4x4 grids). */
#define GRID(k) SC((k) * 0x10)
#define GRID_C(k, c) AT16(GRID(k) + (c) * 2)
#define GRID_COLOUR(k) AT32(GRID(k) + 8)
#define COLOUR_HALF_MASK 0xFFFEFEFFu /* so two colours add without carries */

static inline u32 Mid(u32 a, u32 b) { return (u32)((s32)(a + b) >> 1); }
static inline u32 CMid(u32 a, u32 b) { return (a + b) >> 1; }

/* A sector's header in a subdivision list: the origin (x4, against the
 * GTE's background colour, which holds the camera), its high-poly vertices
 * and colours, and the polygon its flag index 0 stands for. */
typedef struct {
  u32 oz, ox, oy;         /* the asm's s7, t8, t9 */
  u32 verts, cols, pbase; /* s4, s5, s6 */
} SubdivSector;

static inline void SubdivHeader(u32 entry, SubdivSector *h) {
  u32 sec = MIPS_KUSEG(entry << 2);
  u32 w = AT32(sec + SEC_ORIGIN), w3 = AT32(sec + SEC_ORIGIN_Z), counts;
  h->oz = (w >> 14) - CFC2(C_RBK);
  h->ox = CFC2(C_GBK) - ((w & 0xFFFF) << 2);
  counts = AT32(sec + SEC_HIGH);
  h->oy = CFC2(C_BBK) - (w3 >> 14);
  h->verts = sec + SEC_DATA + ((counts >> 22) & 0x3FC);
  h->cols = h->verts + ((counts << 2) & 0x3FC);
  h->pbase = h->cols + ((counts >> 5) & 0x7F8) - ((w3 & 0xFFFF) << 4);
}

/* The next polygon entry of a subdivision list (after any sector header,
 * which a polygon always follows), or 0 at its end. */
static inline u32 SubdivNext(u32 *list, u32 fp, SubdivSector *h) {
  u32 e = AT32(*list);
  *list += 4;
  if (PORT_LOD_ENV_FULL(fp, 1))
    e = 0;
  if (!e)
    return 0;
  if ((s32)e >= 0) {
    SubdivHeader(e, h);
    e = AT32(*list);
    *list += 4;
  }
  return e;
}

/* A high-poly vertex's grid coordinates */
static inline u32 SubZ(u32 w, const SubdivSector *h) { return ((w >> 19) & 0x1FFC) + h->oz; }
static inline u32 SubX(u32 w, const SubdivSector *h) { return h->ox - ((w >> 8) & 0x1FFC); }
static inline u32 SubY(u32 w, const SubdivSector *h) { return h->oy - ((w << 2) & 0xFFC); }

/* 3x3 from four corners (0, 2, 6, 8), one coordinate */
static void Grid3x3(u32 c, u32 p0, u32 p1, u32 p2, u32 p3) {
  u32 m02 = Mid(p0, p2), m13 = Mid(p1, p3);
  GRID_C(0, c) = (u16)p0;
  GRID_C(2, c) = (u16)p1;
  GRID_C(6, c) = (u16)p2;
  GRID_C(8, c) = (u16)p3;
  GRID_C(1, c) = (u16)Mid(p0, p1);
  GRID_C(7, c) = (u16)Mid(p2, p3);
  GRID_C(3, c) = (u16)m02;
  GRID_C(5, c) = (u16)m13;
  GRID_C(4, c) = (u16)Mid(m02, m13);
}

/* A triangle's 6 points from three corners (0, 2, 5) */
static void GridTri2(u32 c, u32 p0, u32 p1, u32 p2) {
  GRID_C(0, c) = (u16)p0;
  GRID_C(2, c) = (u16)p1;
  GRID_C(5, c) = (u16)p2;
  GRID_C(1, c) = (u16)Mid(p0, p1);
  GRID_C(3, c) = (u16)Mid(p0, p2);
  GRID_C(4, c) = (u16)Mid(p1, p2);
}

/* 5x5 from four corners (0, 4, 20, 24), in the asm's order of halvings */
static void Grid5x5(u32 c, u32 a, u32 b, u32 d, u32 e) {
  u32 t1, t2, t3;
  GRID_C(0, c) = (u16)a;
  GRID_C(4, c) = (u16)b;
  GRID_C(20, c) = (u16)d;
  GRID_C(24, c) = (u16)e;
  t1 = Mid(a, b);
  GRID_C(2, c) = (u16)t1;
  GRID_C(1, c) = (u16)Mid(a, t1);
  GRID_C(3, c) = (u16)Mid(b, t1);
  t1 = Mid(d, e);
  GRID_C(22, c) = (u16)t1;
  GRID_C(21, c) = (u16)Mid(d, t1);
  GRID_C(23, c) = (u16)Mid(e, t1);
  t1 = Mid(a, d);
  GRID_C(10, c) = (u16)t1;
  t2 = Mid(b, e);
  GRID_C(14, c) = (u16)t2;
  t3 = Mid(t1, t2);
  GRID_C(12, c) = (u16)t3;
  GRID_C(11, c) = (u16)Mid(t1, t3);
  GRID_C(13, c) = (u16)Mid(t2, t3);
  a = Mid(a, t1);
  GRID_C(5, c) = (u16)a;
  b = Mid(b, t2);
  GRID_C(9, c) = (u16)b;
  d = Mid(d, t1);
  GRID_C(15, c) = (u16)d;
  e = Mid(e, t2);
  GRID_C(19, c) = (u16)e;
  t1 = Mid(a, b);
  GRID_C(7, c) = (u16)t1;
  GRID_C(6, c) = (u16)Mid(a, t1);
  GRID_C(8, c) = (u16)Mid(b, t1);
  t1 = Mid(d, e);
  GRID_C(17, c) = (u16)t1;
  GRID_C(16, c) = (u16)Mid(d, t1);
  GRID_C(18, c) = (u16)Mid(e, t1);
}

/* A triangle's 15 points (rows of 5, 4, 3, 2, 1) from corners 0, 4, 14 */
static void GridTri4(u32 c, u32 a, u32 b, u32 d) {
  u32 m01, m02, m12;
  GRID_C(0, c) = (u16)a;
  GRID_C(4, c) = (u16)b;
  GRID_C(14, c) = (u16)d;
  m01 = Mid(a, b);
  GRID_C(2, c) = (u16)m01;
  GRID_C(1, c) = (u16)Mid(a, m01);
  GRID_C(3, c) = (u16)Mid(b, m01);
  m02 = Mid(a, d);
  GRID_C(9, c) = (u16)m02;
  GRID_C(5, c) = (u16)Mid(a, m02);
  GRID_C(12, c) = (u16)Mid(d, m02);
  m12 = Mid(b, d);
  GRID_C(11, c) = (u16)m12;
  GRID_C(8, c) = (u16)Mid(b, m12);
  GRID_C(13, c) = (u16)Mid(d, m12);
  GRID_C(6, c) = (u16)Mid(a, m12);
  GRID_C(7, c) = (u16)Mid(b, m02);
  GRID_C(10, c) = (u16)Mid(d, m01);
}

/* A grid point into V0's form. Points within +-0x100 are scaled by 16 for
 * precision (their SZ is scaled back). */
static inline int GridVertex(u32 p, u32 *vz, u32 *vxy) {
  s32 z = (s16)AT16(p), x = (s16)AT16(p + 2), y = (s16)AT16(p + 4);
  if ((((z >> 8) + 1) >> 1) == 0 && (((x >> 8) + 1) >> 1) == 0 && (((y >> 8) + 1) >> 1) == 0) {
    *vz = (u32)z << 4;
    *vxy = ((u32)x << 4) + ((u32)y << 20);
    return 1;
  }
  *vz = (u32)z;
  *vxy = (u32)x + ((u32)y << 16);
  return 0;
}

/* Transform N grid points in place. The asm clears point N's first word
 * and loads it into V0 after the last (its y half is whatever was there). */
static void GridTransform(u32 n, int behind_flag) {
  u32 p = GRID(1), end = GRID(n + 1), vz, vxy;
  int small, was;
  AT32(end - 0x10) = 0;
  small = GridVertex(GRID(0), &vz, &vxy);
  MTC2(vz, VZ0);
  MTC2(vxy, VXY0);
  do {
    u32 sxy, f;
    gte_rtps();
    was = small;
    small = GridVertex(p, &vz, &vxy);
    sxy = MFC2(SXY2);
    f = MFC2(SZ3);
    MTC2(vz, VZ0);
    MTC2(vxy, VXY0);
    p += 0x10;
    if (was)
      f >>= 4;
    if (behind_flag && (s32)f <= 0)
      f |= 0xF000;
    if ((s32)(sxy - 0x10000u) <= 0)
      f |= 0x1000;
    if ((s32)(sxy - 0x1000000u) >= 0)
      f |= 0x2000;
    if ((s32)(sxy << 16) <= 0)
      f |= 0x4000;
    AT32(p - 0x20) = sxy;
    if ((s32)((sxy << 16) - 0x2000000u) >= 0)
      f |= 0x8000;
    AT32(p - 0x1C) = f;
  } while (p != end);
}

/* A point's screen position moved toward the average of two others (GPF
 * twice, weights W_OUT and W_IN out of 0x100): the asm's correction for the
 * perspective error of interpolated points close to the camera. */
static void GridCorrect(u32 g, u32 w_out, u32 w_in, u32 pa_addr, u32 pb_addr) {
  u32 xy = AT32(g), pa, pb, m1, m2, n1, n2, x, y;
  MTC2(w_out, IR0);
  MTC2((u32)(s32)(s16)xy, IR1);
  MTC2((u32)((s32)xy >> 16), IR2);
  pa = AT32(pa_addr);
  pb = AT32(pb_addr);
  gte_gpf0();
  MTC2(w_in, IR0);
  MTC2((u32)((s32)(s16)pa + (s32)(s16)pb), IR1);
  MTC2((u32)(((s32)pa >> 16) + ((s32)pb >> 16)), IR2);
  m1 = MFC2(MAC1);
  m2 = MFC2(MAC2);
  gte_gpf0();
  n1 = MFC2(MAC1);
  n2 = MFC2(MAC2);
  x = (u32)((s32)(m1 + n1) >> 8);
  y = (u32)((s32)(m2 + n2) >> 8);
  AT32(g) = (x & 0xFFFF) + (y << 16);
}

/* The 3x3 grid's centre, when the centre is within 0x700..0x800 deep */
static void Grid3x3Centre(void) {
  u32 f = AT32(GRID(4) + 4), z;
  if (f & 0xF000)
    return;
  z = (f & 0xFFF) - 0x700;
  if ((s32)z <= 0)
    return;
  GridCorrect(GRID(4), 0x100 - z, z >> 1, GRID(2), GRID(6));
}

/* The 5x5 grid's four inner points, by the nearest corner's depth */
static void Grid5x5Inner(void) {
  u32 a = (AT32(GRID(0) + 4) & 0xFFF) - 0x40, b = (AT32(GRID(4) + 4) & 0xFFF) - 0x40;
  u32 c = (AT32(GRID(20) + 4) & 0xFFF) - 0x40, d = (AT32(GRID(24) + 4) & 0xFFF) - 0x40, m, k;
  if ((s32)a <= 0 || (s32)b <= 0 || (s32)c <= 0 || (s32)d <= 0)
    return;
  m = a;
  if ((s32)(b - m) < 0)
    m = b;
  if ((s32)(c - m) < 0)
    m = c;
  if ((s32)(d - m) < 0)
    m = d;
  for (k = 0; k < 16; k += 4) {
    u32 g = AT32(ADDR(D_8006D0D8) + k);
    if (AT32(g + 4) & 0xF000)
      continue;
    GridCorrect(g, 0x100 - m, m >> 1, g - 0x40, g + 0x40);
  }
}

/* The neighbours a polygon shares its edges with (12-bit signed offsets in
 * the flag bytes), their flags & BITS packed a byte each. */
static inline u32 NeighbourByte(u32 pf, u32 field, u32 bits) {
  return AT8(pf + (u32)((s32)field >> 20)) & bits;
}

/* A GT3 along an edge shared with a less subdivided neighbour: an entry of
 * three words (two grid offsets, one grid offset and a UV offset, two UV
 * offsets). CODE_SUB turns the quads' GT4 code into GT3's. */
static u32 EdgeTriangle(u32 fp, u32 ot, u32 e, u32 tex, u32 w3, u32 code_sub) {
  u32 e0 = AT32(e), e1 = AT32(e + 4), e2, pa, pb, pc, fa, fb, fc, uvb, zs, slot, x;
  pa = SC(e0 >> 16);
  pb = SC(e0 & 0xFFFF);
  pc = SC(e1 >> 16);
  fa = AT32(pa + 4);
  fb = AT32(pb + 4);
  fc = AT32(pc + 4);
  e2 = AT32(e + 8);
  if (fa & fb & fc & 0xF000)
    return fp;
  uvb = AT32(tex);
  zs = (fa & 0xFFF) + (fb & 0xFFF) + (fc & 0xFFF) + (fc & 0xFFF);
  AT32(fp + 0x8) = AT32(pa);
  AT32(fp + 0x14) = AT32(pb);
  AT32(fp + 0x20) = AT32(pc);
  AT32(fp + 0x4) = AT32(pa + 8) - code_sub;
  AT32(fp + 0x10) = AT32(pb + 8);
  AT32(fp + 0x1C) = AT32(pc + 8);
  slot = ((zs >> 7) + ((w3 & 0x38) >> 1)) << 3;
  x = AT32(tex + 4);
  AT32(fp + 0xC) = (e1 & 0xFFFF) + uvb;
  AT32(fp + 0x18) = (u32)((s32)e2 >> 16) + x;
  AT32(fp + 0x24) = (e2 & 0xFFFF) + uvb;
  AT32(fp) = 0x9000000;
  OtAppend(ot + slot, fp);
  return fp + 0x28;
}

/* Back-face test of a piece, done only when a vertex of the polygon
 * overflowed (the polygon passed it as a whole otherwise). 1 = drop it. */
static inline int PieceBack(u32 w3, u32 xy0, u32 xy1, u32 xy2) {
  s32 a;
  MTC2(xy0, SXY0);
  MTC2(xy1, SXY1);
  MTC2(xy2, SXY2);
  gte_nclip();
  if (w3 & 4)
    return 0;
  a = (s32)MFC2(MAC0);
  if (w3 & 2)
    a = -a;
  return a < 0;
}

/* A quad piece's back-face test: both halves (0, 1, 2) and (3, 2, 1). */
static inline int PieceQuadBack(u32 w3, u32 xy0, u32 xy1, u32 xy2, u32 xy3) {
  s32 a;
  if (!PieceBack(w3, xy0, xy1, xy2))
    return 0;
  MTC2(xy3, SXY0);
  MTC2(xy2, SXY1);
  MTC2(xy1, SXY2);
  gte_nclip();
  a = (s32)MFC2(MAC0);
  if (w3 & 2)
    a = -a;
  return a < 0;
}

/* A quad piece's UVs: the texture's (u, v) pair, its far corners at +31 (or
 * +15), optionally adjusted (D_8006D058, by the second word's top bits). */
static inline void PieceQuadUVs(u32 fp, u32 uvw0, u32 uvw1, u32 far2, u32 far3) {
  u32 uv0 = uvw0, uv1 = uvw1, uv2 = uvw0 + far2, uv3 = uvw0 + far3, r = uvw1 >> 25;
  if (r) {
    u32 adj = ADDR(D_8006D058) + r, a1 = AT32(adj + 4), a0 = AT32(adj);
    uv2 = uvw0 + (a1 & 0xFFFF);
    uv3 = uvw0 + (a1 >> 16);
    uv0 = uvw0 + (a0 & 0xFFFF);
    uv1 = uvw1 + (u32)((s32)a0 >> 16);
  }
  AT32(fp + 0xC) = uv0;
  AT32(fp + 0x18) = uv1;
  AT32(fp + 0x24) = uv2;
  AT32(fp + 0x30) = uv3;
}

static inline u32 PieceSlot(u32 zs, u32 w3) { return ((zs >> 7) + ((w3 & 0x38) >> 1)) << 3; }

/* The polygon's four corners from its vertex word (0, 1, then the low
 * byte's, then the third byte's) and colours, the same way. */
static inline u32 CornerOffset(u32 w, int k) {
  switch (k) {
  case 0:
    return (w >> 22) & 0x3FC;
  case 1:
    return (w >> 14) & 0x3FC;
  case 2:
    return (w << 2) & 0x3FC;
  default:
    return (w >> 6) & 0x3FC;
  }
}
static inline u32 PolyVertex(const SubdivSector *h, u32 w0, int k) { return AT32(h->verts + CornerOffset(w0, k)); }
static inline u32 PolyColour(const SubdivSector *h, u32 w1, int k) { return AT32(h->cols + CornerOffset(w1, k)); }

/* 2x2 subdivision of a quad: four GT4s, plus GT3s along the edges whose
 * neighbour was drawn whole (flag 1). */
static u32 Subdiv2x2Quad(u32 fp, u32 ot, u32 poly, u32 err, const SubdivSector *h, u32 hqtex) {
  const u32 w0 = AT32(poly), w1 = AT32(poly + 4);
  u32 v0, v1, v2, v3, w2, w3, code, c0, c1, c2, c3, pf, mask, tex, e, k;
  v0 = PolyVertex(h, w0, 0);
  v1 = PolyVertex(h, w0, 1);
  v2 = PolyVertex(h, w0, 2);
  v3 = PolyVertex(h, w0, 3);
  w2 = AT32(poly + 8);
  w3 = AT32(poly + 12);
  Grid3x3(0, SubZ(v0, h), SubZ(v1, h), SubZ(v2, h), SubZ(v3, h));
  Grid3x3(1, SubX(v0, h), SubX(v1, h), SubX(v2, h), SubX(v3, h));
  Grid3x3(2, SubY(v0, h), SubY(v1, h), SubY(v2, h), SubY(v3, h));
  code = 0x3C000000;
  if (w2 & 0x80)
    code += 0x2000000;
  c3 = PolyColour(h, w1, 3) + code;
  c2 = PolyColour(h, w1, 2) + code;
  c1 = PolyColour(h, w1, 1) + code;
  c0 = PolyColour(h, w1, 0) + code;
  GRID_COLOUR(8) = c3;
  GRID_COLOUR(6) = c2;
  GRID_COLOUR(2) = c1;
  GRID_COLOUR(0) = c0;
  c0 &= COLOUR_HALF_MASK;
  c1 &= COLOUR_HALF_MASK;
  c2 &= COLOUR_HALF_MASK;
  c3 &= COLOUR_HALF_MASK;
  GRID_COLOUR(1) = CMid(c0, c1);
  GRID_COLOUR(7) = CMid(c2, c3);
  GRID_COLOUR(3) = CMid(c0, c2);
  GRID_COLOUR(5) = CMid(c1, c3);
  GRID_COLOUR(4) = CMid(c1, c2);
  GridTransform(9, 0);
  Grid3x3Centre();

  pf = LISTS + ((poly - h->pbase) >> 4);
  mask = NeighbourByte(pf, w2, 1) | NeighbourByte(pf, w2 << 12, 1) << 8 |
         NeighbourByte(pf, w3, 1) << 16 | NeighbourByte(pf, w3 << 12, 1) << 24;
  tex = MIPS_KUSEG(hqtex + (w2 & 0x7F) * 0xA8);
  for (e = ADDR(D_8006CF98); mask; mask >>= 8) {
    u32 b = mask & 0xFF;
    e += 0xC;
    if (b)
      fp = EdgeTriangle(fp, ot, e - 0xC, tex, w3, 0x8000000);
  }

  for (k = 0; k < 4; k++) {
    const u32 g = AT32(ADDR(D_8006CF48) + k * 4), uvp = tex + 8 + k * 8;
    u32 f0 = AT32(g + 0x4), f1 = AT32(g + 0x14), f2 = AT32(g + 0x34), f3 = AT32(g + 0x44);
    u32 uvw0 = AT32(uvp), uvw1, zs, xy0, xy1, xy2, xy3;
    if (f0 & f1 & f2 & f3 & 0xF000)
      continue;
    uvw1 = AT32(uvp + 4);
    zs = (f0 & 0xFFF) + (f1 & 0xFFF) + (f2 & 0xFFF) + (f3 & 0xFFF);
    xy0 = AT32(g);
    xy1 = AT32(g + 0x10);
    xy2 = AT32(g + 0x30);
    xy3 = AT32(g + 0x40);
    if (err && PieceQuadBack(w3, xy0, xy1, xy2, xy3)) {
      AT32(fp + 0x8) = xy0; /* the asm's store in the branch's delay slot */
      continue;
    }
    AT32(fp + 0x8) = xy0;
    AT32(fp + 0x14) = xy1;
    AT32(fp + 0x20) = xy2;
    AT32(fp + 0x2C) = xy3;
    AT32(fp + 0x4) = AT32(g + 0x8);
    AT32(fp + 0x10) = AT32(g + 0x18);
    AT32(fp + 0x1C) = AT32(g + 0x38);
    AT32(fp + 0x28) = AT32(g + 0x48);
    PieceQuadUVs(fp, uvw0, uvw1, 0x1F00, 0x1F1F);
    AT32(fp) = 0xC000000;
    OtAppend(ot + PieceSlot(zs, w3), fp);
    fp += 0x34;
  }
  return fp;
}

/* A triangle piece's UVs: a UV pair of the texture record picked by the
 * piece and the polygon's rotation, plus per-corner offsets. The asm writes
 * them before it tests the piece. */
static inline void PieceTriUVs(u32 fp, u32 w, u32 rot, u32 rtab, u32 wmask, u32 uvp) {
  u32 q = uvp + (u32)(s32)(s8)AT8(rtab + rot + (w & wmask));
  u32 q1 = AT32(q + 4), pt, q0, pw, ph;
  pt = ((((rot + w) & 3) << 7) + ((q1 >> 25) & 0x78)) + ADDR(D_8006D3C8);
  q0 = AT32(q);
  pw = AT32(pt);
  ph = (u32)(s32)(s16)AT16(pt + 4);
  AT32(fp + 0xC) = (pw & 0xFFFF) + q0;
  AT32(fp + 0x18) = (u32)((s32)pw >> 16) + q1;
  AT32(fp + 0x24) = ph + q0;
}

/* 2x2 subdivision of a triangle: four GT3s, plus edge GT3s. */
static u32 Subdiv2x2Tri(u32 fp, u32 ot, u32 poly, u32 err, const SubdivSector *h, u32 hqtex) {
  const u32 w0 = AT32(poly), w1 = AT32(poly + 4);
  u32 v0, v1, v2, w2, w3, code, c0, c1, c2, pf, mask, tex, e, k, rot, uvp;
  v0 = PolyVertex(h, w0, 0);
  v1 = PolyVertex(h, w0, 1);
  v2 = PolyVertex(h, w0, 2);
  w2 = AT32(poly + 8);
  w3 = AT32(poly + 12);
  GridTri2(0, SubZ(v0, h), SubZ(v1, h), SubZ(v2, h));
  GridTri2(1, SubX(v0, h), SubX(v1, h), SubX(v2, h));
  GridTri2(2, SubY(v0, h), SubY(v1, h), SubY(v2, h));
  code = 0x34000000;
  if (w2 & 0x80)
    code += 0x2000000;
  c2 = PolyColour(h, w1, 2) + code;
  c1 = PolyColour(h, w1, 1) + code;
  c0 = PolyColour(h, w1, 0) + code;
  GRID_COLOUR(5) = c2;
  GRID_COLOUR(2) = c1;
  GRID_COLOUR(0) = c0;
  c0 &= COLOUR_HALF_MASK;
  c1 &= COLOUR_HALF_MASK;
  c2 &= COLOUR_HALF_MASK;
  GRID_COLOUR(1) = CMid(c0, c1);
  GRID_COLOUR(3) = CMid(c0, c2);
  GRID_COLOUR(4) = CMid(c1, c2);
  GridTransform(6, 0);

  pf = LISTS + ((poly - h->pbase) >> 4);
  mask = NeighbourByte(pf, w2, 1) | NeighbourByte(pf, w3 << 12, 1) << 8 |
         NeighbourByte(pf, w3, 1) << 16;
  tex = MIPS_KUSEG(hqtex + (w2 & 0x7F) * 0xA8);
  rot = (w2 >> 8) & 3;
  for (e = ADDR(D_8006D138) + ((w2 & 0x300) >> 3) + ((w2 & 0x300) >> 6); mask; mask >>= 8) {
    u32 b = mask & 0xFF;
    e += 0xC;
    if (b)
      fp = EdgeTriangle(fp, ot, e - 0xC, tex, w3, 0);
  }

  uvp = tex + 8;
  for (k = 0; k < 16; k += 4) {
    const u32 w = AT32(ADDR(D_8006D0E8) + k);
    const u32 pa = SC((w >> 20) & 0xFF0), pb = SC((w >> 12) & 0xFF0), pc = SC((w >> 4) & 0xFF0);
    u32 fa, fb, fc, zs, xya, xyb, xyc;
    PieceTriUVs(fp, w, rot, ADDR(D_8006D378), 0xC, uvp);
    fa = AT32(pa + 4);
    fb = AT32(pb + 4);
    fc = AT32(pc + 4);
    if (fa & fb & fc & 0xF000)
      continue;
    zs = (fa & 0xFFF) + (fb & 0xFFF) + (fc & 0xFFF) + (fc & 0xFFF);
    xya = AT32(pa);
    xyb = AT32(pb);
    xyc = AT32(pc);
    if (err && PieceBack(w3, xya, xyb, xyc)) {
      AT32(fp + 0x8) = xya;
      continue;
    }
    AT32(fp + 0x8) = xya;
    AT32(fp + 0x14) = xyb;
    AT32(fp + 0x20) = xyc;
    AT32(fp + 0x4) = AT32(pa + 8);
    AT32(fp + 0x10) = AT32(pb + 8);
    AT32(fp + 0x1C) = AT32(pc + 8);
    AT32(fp) = 0x9000000;
    OtAppend(ot + PieceSlot(zs, w3), fp);
    fp += 0x28;
  }
  return fp;
}

static u32 Subdiv2x2Pass(u32 fp, u32 ot) {
  const u32 hqtex = AT32(ADDR(g_Environment) + 0x1C); /* m_HQTexturePointer */
  u32 list = LIST_LOW, e;
  SubdivSector h = {0, 0, 0, 0, 0, 0};
  while ((e = SubdivNext(&list, fp, &h)) != 0) {
    u32 poly = MIPS_KUSEG((e >> 2) << 2);
    if (e & 2)
      fp = Subdiv2x2Tri(fp, ot, poly, e & 1, &h, hqtex);
    else
      fp = Subdiv2x2Quad(fp, ot, poly, e & 1, &h, hqtex);
  }
  return fp;
}

/* ===================================================================== */
/* 4x4 subdivision                                                        */
/* ===================================================================== */

/* -LIM < A - B < LIM */
static inline int Within(u32 a, u32 b, u32 lim) {
  return (s32)(a - b - lim) < 0 && (s32)(a - b - lim + 2 * lim) > 0;
}
static inline u32 GridY(u32 g) { return (u32)(s32)(s16)AT16(g + 2); }
static inline u32 GridX(u32 g) { return (u32)(s32)(s16)AT16(g); }

/* A piece still too large on screen goes on the last list (the asm writes
 * the list's next word even when it then drops a piece of depth 0). */
static inline int QueueLast(u32 fp, u32 zs) {
  u32 q = AT32(SC(SC_LAST_END));
  AT32(MIPS_KUSEG(q)) = fp;
  if (!zs)
    return 0;
  AT32(SC(SC_LAST_END)) = q + 4;
  return 1;
}

static u32 Subdiv4x4Quad(u32 fp, u32 ot, u32 poly, u32 err, const SubdivSector *h, u32 hqtex) {
  const u32 w0 = AT32(poly), w1 = AT32(poly + 4);
  u32 v0, v1, v2, v3, w2, w3, code, c0, c1, c2, c3, cc, m, pf, mask, tex, e, k;
  v0 = PolyVertex(h, w0, 0);
  v1 = PolyVertex(h, w0, 1);
  v2 = PolyVertex(h, w0, 2);
  v3 = PolyVertex(h, w0, 3);
  w2 = AT32(poly + 8);
  w3 = AT32(poly + 12);
  Grid5x5(0, SubZ(v0, h), SubZ(v1, h), SubZ(v2, h), SubZ(v3, h));
  Grid5x5(1, SubX(v0, h), SubX(v1, h), SubX(v2, h), SubX(v3, h));
  Grid5x5(2, SubY(v0, h), SubY(v1, h), SubY(v2, h), SubY(v3, h));
  code = 0x3C000000;
  if (w2 & 0x80)
    code += 0x2000000;
  c3 = PolyColour(h, w1, 3) + code;
  c2 = PolyColour(h, w1, 2) + code;
  c1 = PolyColour(h, w1, 1) + code;
  c0 = PolyColour(h, w1, 0) + code;
  GRID_COLOUR(24) = c3;
  GRID_COLOUR(20) = c2;
  GRID_COLOUR(4) = c1;
  GRID_COLOUR(0) = c0;
  c3 &= COLOUR_HALF_MASK;
  c2 &= COLOUR_HALF_MASK;
  c1 &= COLOUR_HALF_MASK;
  c0 &= COLOUR_HALF_MASK;
  cc = CMid(c1, c2);
  GRID_COLOUR(12) = cc;
  cc &= COLOUR_HALF_MASK;
  m = CMid(c0, c1);
  GRID_COLOUR(2) = m;
  m &= COLOUR_HALF_MASK;
  GRID_COLOUR(1) = CMid(c0, m);
  GRID_COLOUR(3) = CMid(c1, m);
  GRID_COLOUR(7) = CMid(cc, m);
  m = CMid(c0, c2);
  GRID_COLOUR(10) = m;
  m &= COLOUR_HALF_MASK;
  GRID_COLOUR(5) = CMid(c0, m);
  GRID_COLOUR(15) = CMid(c2, m);
  GRID_COLOUR(11) = CMid(cc, m);
  m = CMid(c1, c3);
  GRID_COLOUR(14) = m;
  m &= COLOUR_HALF_MASK;
  GRID_COLOUR(9) = CMid(c1, m);
  GRID_COLOUR(19) = CMid(c3, m);
  GRID_COLOUR(13) = CMid(cc, m);
  m = CMid(c2, c3);
  GRID_COLOUR(22) = m;
  m &= COLOUR_HALF_MASK;
  GRID_COLOUR(21) = CMid(c2, m);
  GRID_COLOUR(23) = CMid(c3, m);
  GRID_COLOUR(17) = CMid(cc, m);
  GRID_COLOUR(6) = CMid(c0, cc);
  GRID_COLOUR(8) = CMid(c1, cc);
  GRID_COLOUR(16) = CMid(c2, cc);
  GRID_COLOUR(18) = CMid(c3, cc);
  GridTransform(25, 1);
  Grid5x5Inner();

  pf = LISTS + ((poly - h->pbase) >> 4);
  mask = NeighbourByte(pf, w2, 3) | NeighbourByte(pf, w2 << 12, 3) << 8 |
         NeighbourByte(pf, w3, 3) << 16 | NeighbourByte(pf, w3 << 12, 3) << 24;
  tex = MIPS_KUSEG(hqtex + (w2 & 0x7F) * 0xA8);
  for (e = ADDR(D_8006CFC8); mask; mask >>= 8) {
    u32 b = mask & 0xFF;
    e += 0x24;
    if (!b)
      continue;
    fp = EdgeTriangle(fp, ot, e - 0x24, tex, w3, 0x8000000);
    fp = EdgeTriangle(fp, ot, e - 0x18, tex, w3, 0x8000000);
    if (b & 1)
      fp = EdgeTriangle(fp, ot, e - 0xC, tex, w3, 0x8000000);
  }

  for (k = 0; k < 16; k++) {
    const u32 g = AT32(ADDR(D_8006CF58) + k * 4), uvp = tex + 0x28 + k * 8;
    u32 f0 = AT32(g + 0x4), f1 = AT32(g + 0x14), f2 = AT32(g + 0x54), f3 = AT32(g + 0x64);
    u32 uvw0 = AT32(uvp), uvw1, zs, xy0, xy1, xy2, xy3;
    if (f0 & f1 & f2 & f3 & 0xF000)
      continue;
    uvw1 = AT32(uvp + 4);
    zs = (f0 & 0xFFF) + (f1 & 0xFFF) + (f2 & 0xFFF) + (f3 & 0xFFF);
    AT32(fp + 0x4) = AT32(g + 0x8);
    AT32(fp + 0x10) = AT32(g + 0x18);
    AT32(fp + 0x1C) = AT32(g + 0x58);
    AT32(fp + 0x28) = AT32(g + 0x68);
    PieceQuadUVs(fp, uvw0, uvw1, 0xF00, 0xF0F);
    xy0 = AT32(g);
    xy1 = AT32(g + 0x10);
    xy2 = AT32(g + 0x50);
    xy3 = AT32(g + 0x60);
    if (err && PieceQuadBack(w3, xy0, xy1, xy2, xy3)) {
      AT32(fp + 0x8) = xy0;
      continue;
    }
    AT32(fp + 0x8) = xy0;
    AT32(fp + 0x14) = xy1;
    AT32(fp + 0x20) = xy2;
    AT32(fp + 0x2C) = xy3;
    {
      const u32 ya = GridY(g), yb = GridY(g + 0x10), yc = GridY(g + 0x50), yd = GridY(g + 0x60);
      const u32 xa = GridX(g), xb = GridX(g + 0x10), xc = GridX(g + 0x50), xd = GridX(g + 0x60);
      int small = Within(ya, yb, 0x200) && Within(ya, yc, 0x200) && Within(yb, yc, 0x200) &&
                  Within(yb, yd, 0x200) && Within(yc, yd, 0x200) && Within(xa, xb, 0x400) &&
                  Within(xa, xc, 0x400) && Within(xb, xc, 0x400) && Within(xb, xd, 0x400) &&
                  Within(xc, xd, 0x400);
      if (small ? !zs : !QueueLast(fp, zs))
        continue;
    }
    AT32(fp) = 0xC000000;
    OtAppend(ot + PieceSlot(zs, w3), fp);
    fp += 0x34;
  }
  return fp;
}

static u32 Subdiv4x4Tri(u32 fp, u32 ot, u32 poly, u32 err, const SubdivSector *h, u32 hqtex) {
  const u32 w0 = AT32(poly), w1 = AT32(poly + 4);
  u32 v0, v1, v2, w2, w3, code, c0, c1, c2, m01, m02, m12, pf, mask, tex, e, k, rot, uvp;
  v0 = PolyVertex(h, w0, 0);
  v1 = PolyVertex(h, w0, 1);
  v2 = PolyVertex(h, w0, 2);
  w2 = AT32(poly + 8);
  w3 = AT32(poly + 12);
  GridTri4(0, SubZ(v0, h), SubZ(v1, h), SubZ(v2, h));
  GridTri4(1, SubX(v0, h), SubX(v1, h), SubX(v2, h));
  GridTri4(2, SubY(v0, h), SubY(v1, h), SubY(v2, h));
  code = 0x34000000;
  if (w2 & 0x80)
    code += 0x2000000;
  c2 = PolyColour(h, w1, 2) + code;
  c1 = PolyColour(h, w1, 1) + code;
  c0 = PolyColour(h, w1, 0) + code;
  GRID_COLOUR(14) = c2;
  GRID_COLOUR(4) = c1;
  GRID_COLOUR(0) = c0;
  c2 &= COLOUR_HALF_MASK;
  c1 &= COLOUR_HALF_MASK;
  c0 &= COLOUR_HALF_MASK;
  m01 = CMid(c0, c1);
  GRID_COLOUR(2) = m01;
  m01 &= COLOUR_HALF_MASK;
  GRID_COLOUR(1) = CMid(c0, m01);
  GRID_COLOUR(3) = CMid(c1, m01);
  m02 = CMid(c0, c2);
  GRID_COLOUR(9) = m02;
  m02 &= COLOUR_HALF_MASK;
  GRID_COLOUR(5) = CMid(c0, m02);
  GRID_COLOUR(12) = CMid(c2, m02);
  m12 = CMid(c1, c2);
  GRID_COLOUR(11) = m12;
  m12 &= COLOUR_HALF_MASK;
  GRID_COLOUR(8) = CMid(c1, m12);
  GRID_COLOUR(13) = CMid(c2, m12);
  GRID_COLOUR(6) = CMid(c0, m12);
  GRID_COLOUR(7) = CMid(c1, m02);
  GRID_COLOUR(10) = CMid(c2, m01);
  GridTransform(15, 1);

  pf = LISTS + ((poly - h->pbase) >> 4);
  mask = NeighbourByte(pf, w2, 3) | NeighbourByte(pf, w3 << 12, 3) << 8 |
         NeighbourByte(pf, w3, 3) << 16;
  tex = MIPS_KUSEG(hqtex + (w2 & 0x7F) * 0xA8);
  for (e = ADDR(D_8006D1C8) + ((w2 & 0x300) >> 8) * 0x6C; mask; mask >>= 8) {
    u32 b = mask & 0xFF;
    e += 0x24;
    if (!b)
      continue;
    fp = EdgeTriangle(fp, ot, e - 0x24, tex, w3, 0);
    fp = EdgeTriangle(fp, ot, e - 0x18, tex, w3, 0);
    if (b & 1)
      fp = EdgeTriangle(fp, ot, e - 0xC, tex, w3, 0);
  }

  rot = (w2 >> 8) & 3;
  uvp = tex + 0x28;
  for (k = 0; k < 64; k += 4) {
    const u32 w = AT32(ADDR(D_8006D0F8) + k);
    const u32 pa = SC((w >> 20) & 0xFF0), pb = SC((w >> 12) & 0xFF0), pc = SC((w >> 4) & 0xFF0);
    u32 fa, fb, fc, zs, xya, xyb, xyc;
    PieceTriUVs(fp, w, rot, ADDR(D_8006D388), 0x3C, uvp);
    fa = AT32(pa + 4);
    fb = AT32(pb + 4);
    fc = AT32(pc + 4);
    if (fa & fb & fc & 0xF000)
      continue;
    AT32(fp + 0x4) = AT32(pa + 8);
    AT32(fp + 0x10) = AT32(pb + 8);
    AT32(fp + 0x1C) = AT32(pc + 8);
    zs = (fa & 0xFFF) + (fb & 0xFFF) + (fc & 0xFFF) + (fc & 0xFFF);
    xya = AT32(pa);
    xyb = AT32(pb);
    xyc = AT32(pc);
    if (err && PieceBack(w3, xya, xyb, xyc)) {
      AT32(fp + 0x8) = xya;
      continue;
    }
    AT32(fp + 0x8) = xya;
    AT32(fp + 0x14) = xyb;
    AT32(fp + 0x20) = xyc;
    {
      const u32 ya = GridY(pa), yb = GridY(pb), yc = GridY(pc);
      const u32 xa = GridX(pa), xb = GridX(pb), xc = GridX(pc);
      int small = Within(ya, yb, 0x200) && Within(ya, yc, 0x200) && Within(yb, yc, 0x200) &&
                  Within(xa, xb, 0x400) && Within(xa, xc, 0x400) && Within(xb, xc, 0x400);
      if (small ? !zs : !QueueLast(fp, zs))
        continue;
    }
    AT32(fp) = 0x9000000;
    OtAppend(ot + PieceSlot(zs, w3), fp);
    fp += 0x28;
  }
  return fp;
}

/* The 4x4 list holds both: quads in one pass, then triangles in another. */
static u32 Subdiv4x4Pass(u32 fp, u32 ot, int triangles) {
  const u32 hqtex = AT32(ADDR(g_Environment) + 0x1C);
  u32 list = LIST_4X4, e;
  SubdivSector h = {0, 0, 0, 0, 0, 0};
  while ((e = SubdivNext(&list, fp, &h)) != 0) {
    u32 poly;
    if (!(e & 2) != !triangles)
      continue;
    poly = MIPS_KUSEG((e >> 2) << 2);
    if (triangles)
      fp = Subdiv4x4Tri(fp, ot, poly, e & 1, &h, hqtex);
    else
      fp = Subdiv4x4Quad(fp, ot, poly, e & 1, &h, hqtex);
  }
  return fp;
}

/* ===================================================================== */
/* 5. The last list: split each primitive in place                        */
/* ===================================================================== */

static inline u32 ScreenFlags(u32 sxy) {
  u32 f = 0;
  if ((s32)(sxy - 0x10000u) <= 0)
    f |= 1;
  if ((s32)(sxy - 0x1000000u) >= 0)
    f |= 2;
  if ((s32)(sxy << 16) <= 0)
    f |= 4;
  if ((s32)((sxy << 16) - 0x2000000u) >= 0)
    f |= 8;
  return f;
}

/* Grid points here: SXY (+0), colour (+4), UV (+8, half), flags (+0xF). */
static inline void SplitMid(u32 k, u32 xya, u32 xyb, u32 ca, u32 cb) {
  GRID_C(k, 0) = (u16)Mid((u32)(s32)(s16)xya, (u32)(s32)(s16)xyb);
  GRID_C(k, 1) = (u16)Mid((u32)((s32)xya >> 16), (u32)((s32)xyb >> 16));
  AT32(GRID(k) + 4) = CMid(ca & COLOUR_HALF_MASK, cb & COLOUR_HALF_MASK);
}
static inline void SplitMidUV(u32 k, u32 ua, u32 ub) {
  AT16(GRID(k) + 8) = (u16)(((ua & 0xFF00) + (ub & 0xFF00)) >> 1);
  AT8(GRID(k) + 8) = (u8)(((ua & 0xFF) + (ub & 0xFF)) >> 1);
}

static u32 SplitLast(u32 fp, u32 ot) {
  u32 list = LIST_LOW, end = AT32(SC(SC_LAST_END));
  for (;;) {
    u32 graw, p, t3raw, t4, tbl, tend, clut, tpage, k, n;
    s32 len;
    if (PORT_LOD_ENV_FULL(fp, 1))
      end = list;
    if (list == end)
      break;
    graw = AT32(list);
    list += 4;
    p = MIPS_KUSEG(graw);
    len = (s8)AT8(p + 3);
    AT8(p + 3) = 0;
    if (len == 0xC) {
      /* a GT4 into a 3x3 grid (0, 2, 6, 8 its corners) */
      const u32 xy0 = AT32(p + 0x8), xy1 = AT32(p + 0x14), xy2 = AT32(p + 0x20), xy3 = AT32(p + 0x2C);
      const u32 c0 = AT32(p + 0x4) - 0x8000000, c1 = AT32(p + 0x10) - 0x8000000;
      const u32 c2 = AT32(p + 0x1C) - 0x8000000, c3 = AT32(p + 0x28) - 0x8000000;
      const u32 u0 = AT32(p + 0xC), u1 = AT32(p + 0x18), u2 = AT32(p + 0x24), u3 = AT32(p + 0x30);
      SplitMid(1, xy0, xy1, c0, c1);
      SplitMid(3, xy0, xy2, c0, c2);
      SplitMid(4, xy1, xy2, c1, c2);
      SplitMid(5, xy1, xy3, c1, c3);
      SplitMid(7, xy2, xy3, c2, c3);
      AT32(GRID(0)) = xy0;
      AT32(GRID(2)) = xy1;
      AT32(GRID(6)) = xy2;
      AT32(GRID(8)) = xy3;
      AT32(GRID(0) + 4) = c0;
      AT32(GRID(2) + 4) = c1;
      AT32(GRID(6) + 4) = c2;
      AT32(GRID(8) + 4) = c3;
      clut = (u0 >> 16) << 16;
      tpage = (u1 >> 16) << 16;
      AT16(GRID(0) + 8) = (u16)u0;
      AT16(GRID(2) + 8) = (u16)u1;
      AT16(GRID(6) + 8) = (u16)u2;
      AT16(GRID(8) + 8) = (u16)u3;
      SplitMidUV(1, u0, u1);
      SplitMidUV(3, u0, u2);
      SplitMidUV(4, u1, u2);
      SplitMidUV(5, u1, u3);
      SplitMidUV(7, u2, u3);
      n = 9;
      tbl = ADDR(D_8006D5E4);
      tend = tbl + 0x30;
    } else {
      /* a GT3 into 6 points (0, 2, 5 its corners) */
      const u32 xy0 = AT32(p + 0x8), xy1 = AT32(p + 0x14), xy2 = AT32(p + 0x20);
      const u32 c0 = AT32(p + 0x4), c1 = AT32(p + 0x10), c2 = AT32(p + 0x1C);
      const u32 u0 = AT32(p + 0xC), u1 = AT32(p + 0x18), u2 = AT32(p + 0x24);
      SplitMid(1, xy0, xy1, c0, c1);
      SplitMid(3, xy0, xy2, c0, c2);
      SplitMid(4, xy1, xy2, c1, c2);
      AT32(GRID(0)) = xy0;
      AT32(GRID(2)) = xy1;
      AT32(GRID(5)) = xy2;
      AT32(GRID(0) + 4) = c0;
      AT32(GRID(2) + 4) = c1;
      AT32(GRID(5) + 4) = c2;
      clut = (u0 >> 16) << 16;
      tpage = (u1 >> 16) << 16;
      AT16(GRID(0) + 8) = (u16)u0;
      AT16(GRID(2) + 8) = (u16)u1;
      AT16(GRID(5) + 8) = (u16)u2;
      SplitMidUV(1, u0, u1);
      SplitMidUV(3, u0, u2);
      SplitMidUV(4, u1, u2);
      n = 6;
      tbl = ADDR(D_8006D5C8);
      tend = tbl + 0x1C;
    }
    for (k = 0; k < n; k++)
      AT8(GRID(k) + 0xF) = (u8)ScreenFlags(AT32(GRID(k)));

    /* GT3s chained after the original (now a 0-length primitive), then
     * whatever followed it */
    t4 = AT32(p);
    t3raw = graw;
    for (; tbl != tend; tbl += 4) {
      const u32 t2 = AT32(tbl);
      const u32 a = SC(t2 >> 24), b = SC((t2 >> 16) & 0xFF), c = SC((t2 >> 8) & 0xFF);
      u32 t3;
      if (((s32)(s8)AT8(a + 0xF) & (s32)(s8)AT8(b + 0xF) & (s32)(s8)AT8(c + 0xF)) > 0)
        continue;
      AT32(fp + 0x4) = AT32(a + 4);
      AT32(fp + 0x10) = AT32(b + 4);
      AT32(fp + 0x1C) = AT32(c + 4);
      AT32(fp + 0xC) = AT16(a + 8) + clut;
      AT32(fp + 0x18) = AT16(b + 8) + tpage;
      AT32(fp + 0x24) = AT16(c + 8);
      if (!(t2 & 1)) {
        int small = Within(GridY(a), GridY(b), 0x200) && Within(GridY(a), GridY(c), 0x200) &&
                    Within(GridY(b), GridY(c), 0x200) && Within(GridX(a), GridX(b), 0x400) &&
                    Within(GridX(a), GridX(c), 0x400) && Within(GridX(b), GridX(c), 0x400);
        if (!small) {
          AT32(MIPS_KUSEG(end)) = fp;
          end += 4;
        }
      }
      AT32(fp + 0x8) = AT32(a);
      AT32(fp + 0x14) = AT32(b);
      AT32(fp + 0x20) = AT32(c);
      AT32(fp) = 0x9000000;
      t3 = MIPS_KUSEG(t3raw);
      AT16(t3) = (u16)fp;
      AT8(t3 + 2) = (u8)(fp >> 16);
      t3raw = fp;
      fp += 0x28;
    }
    if (t4) {
      u32 t3 = MIPS_KUSEG(t3raw);
      AT16(t3) = (u16)t4;
      AT8(t3 + 2) = (u8)(t4 >> 16);
    } else {
      /* it ended its OT slot's chain: the last GT3 does now */
      u32 slot = ot;
      while (AT32(slot) != graw)
        slot += 8;
      AT32(slot) = t3raw;
    }
  }
  return fp;
}

/* ===================================================================== */

static u32 EnvDraw(u32 a0, u32 a1, u32 a2, u32 a3) {
  const u32 cam = ADDR(g_Camera);
  u32 r1112 = AT32(cam + 0x14), r1321 = AT32(cam + 0x18);
  const u32 r2223 = AT32(cam + 0x1C), r3132 = AT32(cam + 0x20), r33 = AT32(cam + 0x24);
  u32 fp, ot;
  CamPos pos;
  SubdivLists lists;
  (void)a1;
  (void)a2;
  (void)a3;

  hand_spill_regs();
  /* the view matrix, no translation: for the sector culling */
  CTC2(r1112, 0);
  PORT_WS_VIEW_ROW0(r1112, r1321);
  CTC2(r1321, 1);
  CTC2(r2223, 2);
  CTC2(r3132, 3);
  CTC2(r33, 4);
  CTC2(0u, 5);
  CTC2(0u, 6);
  CTC2(0u, 7);
  pos.x = (s32)AT32(cam + 0x28) >> 4;
  pos.y = (s32)AT32(cam + 0x2C) >> 4;
  pos.z = (s32)AT32(cam + 0x30) >> 4;
  SectorLists((s32)a0, &pos);

  /* the projection matrix for drawing */
  CTC2(AT32(cam + 0x0), 0);
  CTC2(AT32(cam + 0x4), 1);
  CTC2(AT32(cam + 0x8), 2);
  CTC2(AT32(cam + 0xC), 3);
  CTC2(AT32(cam + 0x10), 4);
  fp = AT32(ADDR(D_800757B0));
  ot = MIPS_KUSEG(AT32(ADDR(g_WorldOT)));
  if (!AT32(ADDR(g_SkipLowPolyWorld)))
    fp = DrawLowSectors(fp, ot, &pos);
  fp = DrawHighSectors(fp, ot, &lists);
  AT32(lists.l2x2) = 0;
  AT32(lists.l4x4) = 0;
  fp = Subdiv2x2Pass(fp, ot);
  AT32(SC(SC_LAST_END)) = LIST_LOW;
  fp = Subdiv4x4Pass(fp, ot, 0);
  fp = Subdiv4x4Pass(fp, ot, 1);
  fp = SplitLast(fp, ot);
  PORT_LOD_ENV_END(fp);
  AT32(ADDR(D_800757B0)) = fp;
  return 0; /* v0 is dead at every caller (tools/oracle/ignore_ranges.txt) */
}

HAND_RENDERER(func_800258F0, EnvDraw)
