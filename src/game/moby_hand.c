/* The moby (object) renderers of asm/renderers/r_moby.s (hand-written asm in
 * the original), written by hand from the asm as C (decisions D70, the D33
 * method). Each replaces its mips2c translation when fast_render=1
 * (render_hand.h HAND_RENDERER); fast_render=0 runs the translation, which
 * stays linked as <name>_gen. Checked against PCSX-Redux by the oracle
 * replay and against the translation by host runs (docs/translation-log.md).
 *
 * func_8001F798 draws the regular mobys. The cull pass, func_8001F158 (still
 * translated), leaves one MobyRecord per moby it kept at D_8006FCF4+0x1600;
 * for each, this draw loop puts the moby's frame into the GTE (blending two
 * animation frames with INTPL when the moby is between them), transforms the
 * vertices into the scratchpad, picks the high- or low-detail faces and
 * colours (fading between them, or into the fog colour), then sorts each
 * visible face into a small bucket table by depth and links the buckets into
 * the world OT.
 *
 * func_80022A2C draws the lit mobys and the HUD's models (further down);
 * func_80020F34 the specular ones, on func_8001F798's set-up. */
#include "render_hand.h"

extern char D_8006FCF4[];       /* the moby renderers' tables (TBL_*) */
extern char D_800757B0[];       /* the primitive buffer's next free byte */
extern char g_SkipLowPolyWorld[];
extern char g_Cyclorama[];      /* word 4: the sky's fog colour */
extern char g_WorldOT[];

/* GTE commands, as the asm's opcodes (gte_inline.h). */
#define gte_gpf0() m2c_gte_0x190003D()  /* GPF, sf 0 */
#define gte_intpl() m2c_gte_0x980011()  /* INTPL, sf 1 */
#define gte_dpcs() m2c_gte_0x780010()   /* DPCS, sf 1 */

/* The face loop's helpers, inlined into the loops that use them. */
#define HAND_INLINE static inline __attribute__((always_inline))

/* D_8006FCF4's tables (byte offsets). */
enum {
  TBL_BUCKETS = 0x0000,    /* depth buckets, 8 bytes: last primitive, first */
  TBL_BIG_DEPTHS = 0x0900, /* vertex depths of models over 0x80 vertices */
  TBL_COLOURS = 0x1100,    /* vertex colours faded for this moby */
  TBL_RECORDS = 0x1600,    /* func_8001F158's MobyRecords, 0-ended */
  TBL_FACE_SAVE = 0x2A00,  /* faces a frame replaced: addr << 8 | count, the
                              words; 0-ended */
};
/* The scratchpad: each vertex's screen position (or clip word), then from
 * 0x200 their depths when there are 0x80 or fewer. */
#define SCRATCH_DEPTHS 0x200u

/* What func_8001F158 leaves for each moby it kept (0x38 bytes). */
typedef struct {
  u32 m_Flags;     /* REC_*, depth scale << 24, depth offset << 16 (signed),
                      frame progress << 8, scale override */
  u32 m_Anim;      /* the animation's header (MobyAnim) */
  u32 m_Frame;     /* its current frame (MobyFrame) */
  u32 m_NextFrame; /* the frame it blends into, 0 when progress is 0 */
  u32 m_Pos[3];    /* the moby relative to the camera, camera axes */
  u32 m_Matrix[5]; /* its rotation in camera space, the GTE's layout (R11R12
                      to R33); the last word's high half is the detail: how
                      far inside its draw distance it is */
  u32 m_Specular;  /* specular mobys' records (func_800208FC): the moby's
                      m_SpecularMetalColor and m_SpecularMetalType */
  u32 m_Moby;
} MobyRecord;
_Static_assert(sizeof(MobyRecord) == 0x38, "MobyRecord");
#define REC_CLIP 0x80000000u /* partly off screen: clip-test the vertices */

/* An animation's header (the decomp's AnimationHeader, include/moby.h, with
 * the pointers as the PS1 addresses they are). */
typedef struct {
  s16 m_NumFrames;
  u16 m_NumColors;
  u8 m_IsSpyroAnimation, m_Scale, m_ShortEncodeShift, m_Radius;
  u8 m_VertCount[2]; /* vertices of the high- and the low-detail model */
  u8 m_Padding2, m_DepthScale;
  u8 m_ProgressPerTick, m_Padding3;
  u16 m_Padding4;
  u32 m_AnimationVertices;
  u32 m_Faces, m_Colors, m_LpFaces, m_LpColors;
} MobyAnim;

/* An animation frame (AnimationFrame). */
typedef struct {
  u32 m_Data;              /* low 21 bits: the vertices' RAM address */
  u16 m_VertexColorOffset; /* here: word offset of the frame's face
                              replacements, 0 for none */
  u8 m_Shadow;
  u8 m_ShortOffset;        /* word offset of the delta (half-word) stream */
} MobyFrame;

/* A face's first word: the vertices' byte offsets into the vertex lists at
 * bits 22-30, 13-21 and 4-12, and these. The second holds the colours'
 * byte offsets (bits 19-27, 10-18, 1-9), semi-transparency (bit 0) and a
 * signed depth bias (bits 28-31); a quad's third, its fourth vertex (bits
 * 2-10) and colour (bits 11-19). Textured faces have three texture words
 * more (uv0 | clut, uv1 | tpage, uv2 | uv3 << 16). */
enum {
  FACE_TWO_SIDED = 0x1,
  FACE_TEXTURED = 0x2,
  FACE_SPRITE = 0x4, /* a quad: a screen-facing square at its first vertex */
  FACE_SPECULAR = 0x4, /* a triangle in func_80020F34: lit or metal */
  FACE_HIDDEN = 0x8,
  FACE_QUAD = 0x80000000,
};

/* A frame's vertex: three fields of a word (z 11 bits, x 11, y 12 with the
 * delta flag in its bit 0, signed), or a delta from the one before: a half
 * word of 5-bit fields scaled by the frame's shift. */
typedef struct {
  u32 z, x, y;
} Vtx;

static inline void VtxLoad(Vtx *v, u32 w) {
  v->z = (u32)((s32)w >> 21);
  v->x = (u32)((s32)(w << 10) >> 21);
  v->y = (u32)((s32)(w << 20) >> 19);
}

static inline void VtxStep(Vtx *v, u32 h, u32 shift) {
  v->z += (u32)((s32)h >> 11) << shift;
  v->x -= (u32)((s32)(h << 21) >> 27) << shift;
  v->y -= (u32)((s32)(h << 26) >> 27) << shift;
}

/* A frame's two vertex streams; bit 0 of each vertex says which the next
 * one comes from. */
typedef struct {
  const u32 *word;
  const s16 *half;
} VtxStream;

static inline u32 StreamNext(VtxStream *s, u32 delta) {
  return delta ? (u32)(s32)*s->half++ : *s->word++;
}

static inline void VtxNext(Vtx *v, u32 raw, u32 delta, u32 shift) {
  if (delta)
    VtxStep(v, raw, shift);
  else
    VtxLoad(v, raw);
}

/* Clip flags below a screen x/y word shifted left 5: above, below, left of
 * and right of the screen. */
static inline u32 ClipFlags(u32 sxy) {
  u32 flags = sxy << 5;
  if ((s32)(sxy - 0x10000u) <= 0)
    flags |= 1;
  if ((s32)(sxy - 0x1000000u) >= 0)
    flags |= 2;
  if ((s32)(sxy << 16) <= 0)
    flags |= 4;
  if ((s32)((sxy << 16) - 0x2000000u) >= 0)
    flags |= 8;
  return flags;
}

/* INTPL's result (MAC1-3) into V0: the vertex's z, then x | y << 16. */
static inline void BlendToV0(void) {
  u32 z = MFC2(25), y = MFC2(27), x;
  MTC2(z, 1);
  x = MFC2(26);
  MTC2(x + (y << 16), 0);
}

static inline void SetFarColour(u32 c) {
  CTC2((c << 4) & 0xFF0, 21);
  CTC2((c >> 4) & 0xFF0, 22);
  CTC2((c >> 12) & 0xFF0, 23);
}

/* Transform a frame's n vertices: screen positions (or, with clip, clip
 * words) to xy, depths to depth. The asm loads the next vertex into V0 while
 * the GTE works on the current one, so V0 ends up holding the vertex after
 * the last; so does this. With clip, 0 when every vertex is off the same
 * side of the screen. */
static inline int Transform(VtxStream cur, u32 n, u32 shift, u32 *xy, u32 *depth, int clip) {
  u32 all_out = ~0u, raw = *cur.word++, delta = raw & 1, i = 0;
  Vtx v;
  VtxLoad(&v, raw);
  MTC2(v.z, 1);
  MTC2(v.x + (v.y << 16), 0);
  do {
    u32 sz, sxy;
    raw = StreamNext(&cur, delta);
    gte_rtps();
    VtxNext(&v, raw, delta, shift);
    MTC2(v.x + (v.y << 16), 0);
    MTC2(v.z, 1);
    sz = MFC2(19);
    sxy = MFC2(14);
    depth[i] = sz;
    delta = raw & 1;
    if (clip) {
      u32 flags = ClipFlags(sxy);
      all_out &= flags;
      xy[i] = flags;
    } else {
      xy[i] = sxy;
    }
  } while (++i != n);
  return !clip || (s32)(all_out & 0x1F) <= 0;
}

/* The same between two frames: each vertex is the current frame's blended
 * towards the next frame's by progress (INTPL), the next one blended while
 * the current one is transformed. */
static inline int TransformBlend(VtxStream cur, VtxStream next, u32 n, u32 progress, u32 shift,
                                 u32 *xy, u32 *depth, int clip) {
  u32 all_out = ~0u, i = 0;
  u32 raw = *cur.word++, delta = raw & 1;
  u32 nraw = *next.word++, ndelta = nraw & 1;
  Vtx v, nv;
  VtxLoad(&v, raw);
  VtxLoad(&nv, nraw);
  MTC2(progress, 8);
  MTC2(v.z, 9);
  MTC2(v.x, 10);
  MTC2(v.y, 11);
  CTC2(nv.z, 21);
  CTC2(nv.x, 22);
  CTC2(nv.y, 23);
  gte_intpl();
  raw = StreamNext(&cur, delta);
  BlendToV0();
  do {
    u32 sz, sxy;
    gte_rtps();
    nraw = StreamNext(&next, ndelta);
    VtxNext(&v, raw, delta, shift);
    delta = raw & 1;
    VtxNext(&nv, nraw, ndelta, shift);
    MTC2(progress, 8);
    MTC2(v.z, 9);
    MTC2(v.x, 10);
    MTC2(v.y, 11);
    CTC2(nv.z, 21);
    CTC2(nv.x, 22);
    CTC2(nv.y, 23);
    sxy = MFC2(14);
    sz = MFC2(19);
    gte_intpl();
    raw = StreamNext(&cur, delta);
    ndelta = nraw & 1;
    BlendToV0();
    depth[i] = sz;
    if (clip) {
      u32 flags = ClipFlags(sxy);
      all_out &= flags;
      xy[i] = flags;
    } else {
      xy[i] = sxy;
    }
  } while (++i != n);
  return !clip || (s32)(all_out & 0x1F) <= 0;
}

/* What the face loop works with (the asm's registers). */
typedef struct {
  u32 prim;          /* t8: the next primitive */
  u32 buckets;       /* s3: bucket 0 */
  u32 lowest;        /* s4: the lowest bucket used (from bucket 0x120) */
  u32 highest;       /* s5: the highest (from bucket 0) */
  u32 bias;          /* s6: subtracted from the depths */
  u32 zshift;        /* s7: depth to bucket */
  u32 tex_bias;      /* s2: added to the first texture word */
  const u8 *colours; /* t9 */
  const u8 *depth;   /* gp: the vertex depths */
  const u8 *xy;      /* sp: the screen positions or clip words */
  int clip;          /* s7's sign */
} FaceCtx;

/* Put the next primitive (size bytes) at the end of a bucket's chain. */
HAND_INLINE void Link(FaceCtx *c, u32 bucket, u32 size) {
  u32 *b = (u32 *)(uintptr_t)bucket;
  u32 last = b[0];
  b[0] = c->prim;
  if (last) {
    u32 t = MIPS_KUSEG(last);
    AT16(t) = (u16)c->prim;
    AT8(t + 2) = (u8)(c->prim >> 16);
  } else {
    if ((s32)(c->lowest - bucket) > 0)
      c->lowest = bucket;
    b[1] = c->prim;
    if ((s32)(c->highest - bucket) < 0)
      c->highest = bucket;
  }
  c->prim += size;
}

/* A face's depth bias, bits 28-31 of its colour word, doubled. */
#define FACE_ZOFF(cw) ((u32)((s32)(cw) >> 28) << 1)
/* A colour, by its byte offset into the colour table. */
#define COLOUR(c, off) WORD_AT((c)->colours, (off))
/* A vertex's depth (i: its byte offset into the xy list): a word per vertex
 * in the regular draw loop; in the specular one (wide), the half word that
 * starts an 8-byte entry, its camera-space position after it. */
#define VERT_DEPTH(c, i, wide)                                                 \
  ((wide) ? (u32) * (const u16 *)((c)->depth + (i) * 2) : WORD_AT((c)->depth, (i)))

/* A Gouraud triangle (POLY_G3) or a textured one (POLY_GT3), its vertices'
 * byte offsets i0-i2 and colours col0 (code byte clear)-col2. tex: the
 * face's texture words end there. uv3: the second half of a quad, whose
 * first vertex is the quad's fourth: its uv3 replaces uv0. The asm stores a
 * G3's colours before it knows the face is in front of the camera. */
HAND_INLINE void EmitTriangle(FaceCtx *c, const u32 *tex, u32 i0, u32 i1, u32 i2, u32 col0,
                              u32 col1, u32 col2, u32 cw, u32 textured, int uv3, int wide) {
  const u32 p = c->prim;
  u32 z0 = VERT_DEPTH(c, i0, wide), z1 = VERT_DEPTH(c, i1, wide), z2 = VERT_DEPTH(c, i2, wide);
  u32 sum = z0 + (z0 >> 1) - c->bias + z1 + (z1 >> 1) + z2, key;
  if (!textured) {
    AT32(p + 0x4) = col0 + 0x30000000u;
    AT32(p + 0xC) = col1;
    AT32(p + 0x14) = col2;
    if ((s32)sum < 0)
      return;
    key = (u32)((s32)sum >> c->zshift) + FACE_ZOFF(cw);
    if ((s32)key < 0)
      return;
    AT32(p) = 0x06000000u;
    Link(c, (key << 3) + c->buckets, 0x1C);
  } else {
    u32 tex2;
    if ((s32)sum < 0)
      return;
    key = (u32)((s32)sum >> c->zshift) + FACE_ZOFF(cw);
    if ((s32)key < 0)
      return;
    tex2 = tex[-1];
    AT32(p + 0x1C) = col2;
    AT32(p + 0x10) = col1;
    AT32(p + 0x4) = col0 + 0x34000000u + ((cw & 1) << 25);
    AT32(p + 0xC) = tex[-3] + c->tex_bias;
    if (uv3)
      AT16(p + 0xC) = (u16)(tex2 >> 16);
    AT32(p) = 0x09000000u;
    AT32(p + 0x18) = tex[-2];
    AT32(p + 0x24) = tex2;
    Link(c, (key << 3) + c->buckets, 0x28);
  }
}

/* A whole quad: POLY_G4 or POLY_GT4. The asm stores a G4's colours, and its
 * tag, before it has the last depth check. */
HAND_INLINE void EmitQuad(FaceCtx *c, const u32 *tex, u32 i0, u32 i1, u32 i2, u32 i3, u32 c0,
                          u32 c1, u32 c3, u32 cw, u32 textured, int wide) {
  const u32 p = c->prim;
  u32 col0 = (COLOUR(c, c0) << 8) >> 8;
  u32 col1 = COLOUR(c, c1), col2 = COLOUR(c, (cw << 1) & 0x7FC);
  u32 col3 = COLOUR(c, c3);
  u32 sum = VERT_DEPTH(c, i0, wide) - c->bias + VERT_DEPTH(c, i1, wide) + VERT_DEPTH(c, i2, wide) +
            VERT_DEPTH(c, i3, wide);
  u32 zoff = FACE_ZOFF(cw) << c->zshift;
  if (!textured) {
    AT32(p + 0x4) = col0 + 0x38000000u;
    AT32(p + 0xC) = col1;
    AT32(p + 0x14) = col2;
    AT32(p + 0x1C) = col3;
    if ((s32)sum < 0)
      return;
    AT32(p) = 0x08000000u;
    sum += zoff;
    if ((s32)sum < 0)
      return;
    Link(c, ((u32)((s32)sum >> c->zshift) << 3) + c->buckets, 0x24);
  } else {
    u32 tex2;
    if ((s32)sum < 0)
      return;
    sum += zoff;
    if ((s32)sum < 0)
      return;
    tex2 = tex[-1];
    AT32(p + 0x28) = col3;
    AT32(p + 0x1C) = col2;
    AT32(p + 0x10) = col1;
    AT32(p + 0x4) = col0 + 0x3C000000u + ((cw & 1) << 25);
    AT32(p) = 0x0C000000u;
    AT32(p + 0xC) = tex[-3] + c->tex_bias;
    AT32(p + 0x18) = tex[-2];
    AT32(p + 0x24) = tex2;
    AT32(p + 0x30) = tex2 >> 16;
    Link(c, ((u32)((s32)sum >> c->zshift) << 3) + c->buckets, 0x34);
  }
}

/* With clip, the xy list holds clip words: a face whose vertices are all
 * off the same side isn't drawn, the others get their positions back. */
#define CLIP_OUT(flags) ((s32)((flags) & 0x1F) > 0)
#define CLIP_XY(w) ((u32)((s32)(w) >> 5))

/* A triangle (face word bit 31 clear): 2 words, 5 textured. */
HAND_INLINE const u32 *DrawTriangle(FaceCtx *c, const u32 *f) {
  const u32 fw = f[0], cw = f[1], textured = fw & FACE_TEXTURED;
  const u32 i0 = (fw >> 20) & 0x7FC, i1 = (fw >> 11) & 0x7FC, i2 = (fw >> 2) & 0x7FC;
  const u32 *next = f + (textured ? 5 : 2);
  const u32 p = c->prim;
  u32 xy0 = WORD_AT(c->xy, i0), xy1 = WORD_AT(c->xy, i1), xy2 = WORD_AT(c->xy, i2), m;
  if (c->clip) {
    if (CLIP_OUT(xy0 & xy1 & xy2))
      return next;
    xy0 = CLIP_XY(xy0);
    xy1 = CLIP_XY(xy1);
    xy2 = CLIP_XY(xy2);
  }
  MTC2(xy0, 12);
  MTC2(xy1, 13);
  MTC2(xy2, 14);
  gte_nclip();
  AT32(p + 0x8) = xy0;
  if (textured) {
    AT32(p + 0x14) = xy1;
    AT32(p + 0x20) = xy2;
  } else {
    AT32(p + 0x10) = xy1;
    AT32(p + 0x18) = xy2;
  }
  if (fw & FACE_HIDDEN)
    return next;
  m = MFC2(24);
  if (fw & FACE_TWO_SIDED ? m == 0 : (s32)m <= 0)
    return next; /* edge-on, or facing away */
  EmitTriangle(c, next, i0, i1, i2, (COLOUR(c, (cw >> 17) & 0x7FC) << 8) >> 8,
               COLOUR(c, (cw >> 8) & 0x7FC), COLOUR(c, (cw << 1) & 0x7FC), cw, textured, 0, 0);
  return next;
}

/* A quad (face word bit 31): 3 words, 6 textured. It's tested as two
 * triangles, (0 1 2) and (3 1 2); when only one faces the camera, that one
 * is drawn as a triangle. */
HAND_INLINE const u32 *DrawQuad(FaceCtx *c, const u32 *f) {
  const u32 fw = f[0], cw = f[1], w2 = f[2], textured = fw & FACE_TEXTURED;
  const u32 i0 = (fw >> 20) & 0x7FC, i1 = (fw >> 11) & 0x7FC, i2 = (fw >> 2) & 0x7FC;
  const u32 i3 = w2 & 0x7FC;
  const u32 *next = f + (textured ? 6 : 3);
  const u32 p = c->prim;
  u32 xy0 = WORD_AT(c->xy, i0), xy1 = WORD_AT(c->xy, i1), xy2 = WORD_AT(c->xy, i2);
  u32 xy3 = WORD_AT(c->xy, i3), m, m2, c0, c1, c3;
  if (c->clip) {
    if (CLIP_OUT(xy0 & xy1 & xy2 & xy3))
      return next;
    xy0 = CLIP_XY(xy0);
    xy1 = CLIP_XY(xy1);
    xy2 = CLIP_XY(xy2);
    xy3 = CLIP_XY(xy3);
  }
  MTC2(xy0, 12);
  MTC2(xy1, 13);
  MTC2(xy2, 14);
  if (fw & FACE_HIDDEN)
    return next;
  gte_nclip();
  AT32(p + 0x8) = xy0;
  if (textured) {
    AT32(p + 0x14) = xy1;
    AT32(p + 0x20) = xy2;
    AT32(p + 0x2C) = xy3;
  } else {
    AT32(p + 0x10) = xy1;
    AT32(p + 0x18) = xy2;
    AT32(p + 0x20) = xy3;
  }
  m = MFC2(24);
  MTC2(xy3, 12);
  if ((fw & FACE_TWO_SIDED) && (s32)m > 0)
    m = 0u - m;
  gte_nclip();
  m2 = MFC2(24);
  if ((fw & FACE_TWO_SIDED) && (s32)m2 < 0)
    m2 = 0u - m2;
  c0 = (cw >> 17) & 0x7FC;
  c1 = (cw >> 8) & 0x7FC;
  c3 = (w2 >> 9) & 0x7FC;
  if ((s32)m < 0) {
    if ((s32)m2 > 0)
      EmitQuad(c, next, i0, i1, i2, i3, c0, c1, c3, cw, textured, 0);
    else
      EmitTriangle(c, next, i0, i1, i2, (COLOUR(c, c0) << 8) >> 8, COLOUR(c, c1),
                   COLOUR(c, (cw << 1) & 0x7FC), cw, textured, 0, 0);
  } else if ((s32)m2 > 0) {
    AT32(p + 0x8) = xy3;
    EmitTriangle(c, next, i3, i1, i2, (COLOUR(c, c3) << 8) >> 8, COLOUR(c, c1),
                 COLOUR(c, (cw << 1) & 0x7FC), cw, textured, 1, 0);
  }
  return next;
}

/* A sprite (a quad with bit 2): a screen-facing textured square (POLY_FT4)
 * at its first vertex, cw's bits 10-18 wide and 1-9 high in model units;
 * 5 words. RTPS with the matrix's last row cleared and the vertex's depth as
 * TRZ leaves the projection's scale in MAC0 (DQA H/SZ), and GPF scales the
 * size by it. The asm leaves the GTE that way, and adds the face's hidden
 * bit to the depth where the depth bias would go. */
HAND_INLINE const u32 *DrawSprite(FaceCtx *c, const u32 *f, int wide) {
  const u32 fw = f[0], cw = f[1];
  const u32 i0 = (fw >> 20) & 0x7FC;
  const u32 *next = f + 5;
  const u32 p = c->prim;
  u32 xy = WORD_AT(c->xy, i0), z, col, d, bucket, scale, w, h, x, y, xr, yb;
  if (c->clip) {
    if (CLIP_OUT(xy))
      return next;
    xy = CLIP_XY(xy);
  }
  z = VERT_DEPTH(c, i0, wide);
  CTC2(0x100u, 27); /* DQA */
  CTC2(0u, 28);     /* DQB */
  CTC2(0u, 3);      /* R31R32 */
  CTC2(0u, 4);      /* R33 */
  CTC2(z, 7);       /* TRZ */
  gte_rtps();
  col = COLOUR(c, (cw >> 17) & 0x7FC);
  d = (z << 2) + (fw & FACE_HIDDEN) - c->bias;
  if ((s32)d <= 0)
    return next;
  bucket = ((u32)((s32)d >> c->zshift) << 3) + c->buckets;
  scale = MFC2(24);
  MTC2((cw >> 10) & 0x1FF, 9);
  MTC2((cw >> 1) & 0x1FF, 10);
  MTC2(scale >> 12, 8);
  gte_gpf0();
  col = ((col << 8) >> 8) + 0x2C000000u + ((cw & 1) << 25);
  w = MFC2(25) >> 11;
  h = MFC2(26) >> 11;
  x = xy & 0xFFFF;
  y = xy >> 16;
  xr = x + (w >> 1);
  yb = y + (h >> 1);
  AT32(p + 0x4) = col;
  AT32(p + 0xC) = f[2];
  AT32(p + 0x14) = f[3];
  AT32(p + 0x1C) = f[4];
  AT32(p + 0x24) = f[4] >> 16;
  AT16(p + 0x8) = (u16)(xr - w);
  AT16(p + 0xA) = (u16)(yb - h);
  AT16(p + 0x10) = (u16)xr;
  AT16(p + 0x12) = (u16)(yb - h);
  AT16(p + 0x18) = (u16)(xr - w);
  AT16(p + 0x1A) = (u16)yb;
  AT16(p + 0x20) = (u16)xr;
  AT16(p + 0x22) = (u16)yb;
  AT32(p) = 0x09000000u;
  Link(c, bucket, 0x28);
  return next;
}

/* Link the buckets used, highest first, into the world OT's slots (8 bytes:
 * last primitive, first), span bytes of buckets per slot, going down from
 * the moby's own slot; each slot's chain gets them after what it has. The
 * buckets are left empty. */
static void LinkBuckets(const FaceCtx *c, u32 zone, u32 rbk) {
  const u32 sh = ((rbk >> 8) + (rbk & 0xFF)) & 31; /* scale + depth scale */
  const u32 span = 0x100u >> sh;
  const u32 stop = c->lowest - 8;
  const u32 ot = *(u32 *)g_WorldOT;
  u32 slot = (zone << 3) + (0x20u << sh) - 8;
  u32 below = PSX_ADDR(D_8006FCF4) + 0x800 - c->highest, bucket = c->highest, at;
  below = (s32)below >= 0 ? below << sh : 0;
  slot -= (below >> 8) << 3;
  if ((s32)slot < 0)
    slot = 0;
  at = slot + ot;
  for (;;) {
    const u32 ot_slot = MIPS_KUSEG(at);
    u32 tail = AT32(ot_slot), low = bucket - span;
    if ((s32)(stop - low) > 0)
      low = stop;
    if (!tail) {
      /* an empty slot starts with the first non-empty bucket */
      for (;;) {
        u32 head;
        if (bucket == low)
          goto slot_done;
        head = AT32(bucket + 4);
        bucket -= 8;
        if (head) {
          tail = AT32(bucket + 8);
          AT32(ot_slot + 4) = head;
          AT32(bucket + 0xC) = 0;
          AT32(bucket + 8) = 0;
          break;
        }
      }
    }
    while (bucket != low) {
      u32 head = AT32(bucket + 4), last, t;
      bucket -= 8;
      if (!head)
        continue;
      last = AT32(bucket + 8);
      t = MIPS_KUSEG(tail);
      AT16(t) = (u16)head;
      AT8(t + 2) = (u8)(head >> 16);
      tail = last;
      AT32(bucket + 8) = 0;
      AT32(bucket + 0xC) = 0;
    }
  slot_done:
    AT32(ot_slot) = tail;
    if (low == stop)
      break;
    if (at != ot)
      at -= 8;
  }
}

/* Put back the faces the frame replaced. */
static void RestoreFaces(void) {
  const u32 *save = (const u32 *)(D_8006FCF4 + TBL_FACE_SAVE);
  u32 e;
  while ((e = *save++) != 0) {
    u32 n = e & 0xFF;
    u32 *face = HPTR(u32, (e >> 8) | 0x80000000u);
    while (n--)
      *face++ = *save++;
  }
}

/* A record's set-up, shared by the regular and the specular draw loops. */
typedef struct {
  const MobyAnim *anim;
  const MobyFrame *frame;
  u32 flags;
  u32 progress; /* INTPL's IR0, 0 for one frame */
  u32 shift;    /* the delta stream's scale */
  u32 zone;     /* the OT zone (also in GBK) */
  u32 rbk;      /* depth scale | model scale << 8 (also in RBK) */
  u32 bbk;      /* depth less the depth scale's bias (also in BBK) */
  s32 detail;   /* also in ZSF4 */
  int clip;
} RecState;

/* The OT zone, the moby's position scaled by the model's scale (then, with
 * a scale override, by that), its matrix and the detail into the GTE.
 * 0: too far once scaled, not drawn. The GTE's back colour keeps the depth
 * scales and zone for later. */
static inline int RecordBegin(const MobyRecord *rec, RecState *s) {
  const u32 flags = rec->m_Flags;
  const s32 depth_offset = (s32)(flags << 8) >> 24;
  const u32 depth_scale = (flags >> 24) & 0x1F;
  u32 x, y, z, scale;
  s->flags = flags;
  s->anim = HPTR(const MobyAnim, rec->m_Anim);
  s->frame = HPTR(const MobyFrame, rec->m_Frame);
  s->progress = (flags & 0xFF00) >> 2;
  s->clip = (flags & REC_CLIP) != 0;
  scale = s->anim->m_Scale;
  s->shift = (s->anim->m_ShortEncodeShift + 1u) & 31;
  s->zone = (u32)((s32)rec->m_Pos[2] >> 5) - (u32)depth_offset;
  if ((s32)s->zone < 0)
    s->zone = 0;
  if ((s32)(s->zone - 0x110u) >= 0)
    s->zone += 0x20;
  x = (u32)((s32)(rec->m_Pos[0] << 2) >> (scale & 31));
  y = (u32)((s32)(rec->m_Pos[1] << 2) >> (scale & 31));
  z = (u32)((s32)(rec->m_Pos[2] << 2) >> (scale & 31));
  if (flags & 0xFF) {
    MTC2(flags & 0xFF, 8);
    MTC2(x, 9);
    MTC2(y, 10);
    MTC2(z, 11);
    gte_gpf0();
    z = (u32)((s32)MFC2(27) >> 5);
    y = (u32)((s32)MFC2(26) >> 5);
    x = (u32)((s32)MFC2(25) >> 5);
    if ((s32)(z - 0xF618u) > 0)
      return 0;
  }
  s->rbk = depth_scale + (scale << 8);
  s->bbk = z - (0x200u << depth_scale);
  CTC2(x, 5);
  CTC2(y, 6);
  CTC2(z, 7);
  CTC2(s->rbk, 13);
  CTC2(s->zone, 14);
  CTC2(s->bbk, 15);
  CTC2(rec->m_Matrix[0], 0);
  CTC2(rec->m_Matrix[1], 1);
  CTC2(rec->m_Matrix[2], 2);
  CTC2(rec->m_Matrix[3], 3);
  CTC2(rec->m_Matrix[4], 4);
  s->detail = (s32)rec->m_Matrix[4] >> 16;
  CTC2((u32)s->detail, 30);
  return 1;
}

/* The frame's face replacements (high detail only), saving the faces it
 * replaces to TBL_FACE_SAVE (RestoreFaces puts them back). */
static inline void ReplaceFaces(const RecState *s) {
  u32 *save = (u32 *)(D_8006FCF4 + TBL_FACE_SAVE);
  if (s->detail > 0 && s->frame->m_VertexColorOffset) {
    const u32 *swap = (const u32 *)s->frame + s->frame->m_VertexColorOffset;
    const u32 model_faces = s->anim->m_Faces;
    u32 w;
    do {
      u32 face_addr, k;
      u32 *face;
      w = *swap++;
      face_addr = (w & 0xFFFF) + model_faces;
      k = (w >> 24) & 0x7F;
      *save++ = (face_addr << 8) + k;
      face = HPTR(u32, face_addr);
      while (k--) {
        *save++ = *face;
        *face++ = *swap++;
      }
    } while ((s32)w > 0);
  }
  *save = 0;
}

static inline VtxStream FrameStreams(const MobyFrame *f) {
  VtxStream v;
  v.word = HPTR(const u32, f->m_Data & 0x1FFFFF);
  v.half = (const s16 *)(v.word + f->m_ShortOffset);
  return v;
}

/* The faces and colours: high detail, fading into the low-detail colours
 * over its last 0x400 of distance; past it, low detail, fading into the fog
 * over 0x800 more where the level has it (g_SkipLowPolyWorld, in ZSF3).
 * Faded colours go to TBL_COLOURS. Sets c's colours and texture bias;
 * returns the faces. */
static inline const u32 *PickFaces(const RecState *s, FaceCtx *c) {
  const MobyAnim *anim = s->anim;
  u8 *const tbl = (u8 *)D_8006FCF4;
  const u32 *faces;
  c->tex_bias = 0;
  if (s->detail > 0) {
    faces = HPTR(const u32, anim->m_Faces);
    if (s->detail - 0x400 >= 0) {
      c->colours = HPTR(const u8, anim->m_Colors);
    } else {
      const u32 k = 0u - ((u32)(s->detail - 0x400) << 2);
      const u32 *hp = HPTR(const u32, anim->m_Colors);
      const u32 *lp = HPTR(const u32, anim->m_LpColors);
      u32 *out = (u32 *)(tbl + TBL_COLOURS), i = 0, nc = anim->m_NumColors;
      MTC2(k, 8);
      c->tex_bias = (k >> 9) << 22;
      do {
        MTC2(hp[i], 6);
        SetFarColour(lp[i]);
        gte_dpcs();
        out[i] = MFC2(22);
      } while (++i != nc);
      c->colours = tbl + TBL_COLOURS;
    }
  } else {
    s32 fog = s->detail + 0x800;
    faces = HPTR(const u32, anim->m_LpFaces);
    c->colours = HPTR(const u8, anim->m_LpColors);
    if ((s32)CFC2(29) > 0 && fog < 0) {
      const u32 *lp = HPTR(const u32, anim->m_LpColors);
      u32 *out = (u32 *)(tbl + TBL_COLOURS), i = 0, nc = anim->m_NumColors;
      fog = fog + 0x800 > 0 ? -fog : 0x7FF;
      c->tex_bias = (u32)fog << 1;
      MTC2(c->tex_bias, 8);
      SetFarColour(((const u32 *)g_Cyclorama)[4]);
      MTC2(lp[0], 6);
      do {
        gte_dpcs();
        out[i] = MFC2(22);
        MTC2(lp[i + 1], 6);
      } while (++i != nc);
      c->colours = tbl + TBL_COLOURS;
    }
  }
  return faces;
}

/* The buckets: depth less bias, shifted by the depth scale; bucket 0 moved
 * up for a moby that starts behind the bias. */
static inline void FaceCtxBegin(FaceCtx *c, const RecState *s, u32 prim) {
  c->prim = prim;
  c->clip = s->clip;
  c->bias = s->bbk << 2;
  c->zshift = ((s->rbk & 0xFF) + 4) & 31;
  c->buckets = PSX_ADDR(D_8006FCF4 + TBL_BUCKETS);
  if ((s32)c->bias <= 0) {
    c->buckets += ((0u - c->bias) >> c->zshift) << 3;
    c->bias = 1;
  }
  c->lowest = c->buckets + 0x900;
  c->highest = c->buckets;
}

/* Draw one moby; returns the primitive pointer after it. */
static u32 MobyDrawRecord(const MobyRecord *rec, u32 prim) {
  RecState s;
  FaceCtx c;
  const u32 *faces, *end;
  u32 n, *depth;

  if (!RecordBegin(rec, &s))
    return prim;
  ReplaceFaces(&s);

  /* The vertices: screen positions (or clip words) to the scratchpad,
   * depths after them (or to TBL_BIG_DEPTHS for more than 0x80). */
  n = s.anim->m_VertCount[s.detail < 0];
  if (!n)
    goto restore;
  depth = n <= 0x80 ? (u32 *)(SCRATCH_BASE + SCRATCH_DEPTHS) : (u32 *)(D_8006FCF4 + TBL_BIG_DEPTHS);
  if (s.progress) {
    if (!TransformBlend(FrameStreams(s.frame), FrameStreams(HPTR(const MobyFrame, rec->m_NextFrame)),
                        n, s.progress, s.shift, (u32 *)SCRATCH_BASE, depth, s.clip))
      goto restore;
  } else if (!Transform(FrameStreams(s.frame), n, s.shift, (u32 *)SCRATCH_BASE, depth, s.clip)) {
    goto restore;
  }

  faces = PickFaces(&s, &c);
  FaceCtxBegin(&c, &s, prim);
  c.depth = (const u8 *)depth;
  c.xy = (const u8 *)(uintptr_t)SCRATCH_BASE;
  end = (const u32 *)((const u8 *)(faces + 1) + faces[0]);
  for (faces++; faces != end;) {
    const u32 fw = faces[0];
    if (!(fw & FACE_QUAD))
      faces = DrawTriangle(&c, faces);
    else if (fw & FACE_SPRITE)
      faces = DrawSprite(&c, faces, 0);
    else
      faces = DrawQuad(&c, faces);
  }
  if ((s32)(c.highest - c.lowest) >= 0) {
    LinkBuckets(&c, s.zone, s.rbk);
    prim = c.prim;
  }

restore:
  RestoreFaces();
  return prim;
}

static u32 MobyDraw(u32 a0, u32 a1, u32 a2, u32 a3) {
  u32 prim = *(u32 *)D_800757B0;
  u32 rec = PSX_ADDR(D_8006FCF4 + TBL_RECORDS);
  (void)a0;
  (void)a1;
  (void)a2;
  (void)a3;
  hand_spill_regs();
  CTC2(*(u32 *)g_SkipLowPolyWorld, 29); /* ZSF3 */
  for (;; rec += sizeof(MobyRecord)) {
    const MobyRecord *r = (const MobyRecord *)(uintptr_t)rec;
    if (PORT_LOD_RECORDS_FULL(rec, prim))
      break;
    if (!r->m_Flags)
      break;
    prim = MobyDrawRecord(r, prim);
  }
  *(u32 *)D_800757B0 = prim;
  return prim; /* v0 and v1 are dead at every caller (tools/oracle/ignore_ranges.txt) */
}

HAND_RENDERER(func_8001F798, MobyDraw)

/* ---- func_80022A2C: the lit mobys and the HUD's models ----
 *
 * Each moby in the list at D_8006FCF4+0x2400 (NULL-ended) is culled and
 * drawn in one go: a box test against the view, its rotation from its angles
 * (or, for a HUD model, a fixed matrix and its position as the screen
 * offset), its vertices transformed into the scratchpad, its vertex colours
 * lit from its normals (to D_8006FCF4+0x2A00), and its faces sorted into the
 * buckets and linked into the world OT's (or the HUD OT's) slot for its
 * depth, after a draw-mode primitive when it has semi-transparent faces. */

extern char g_Camera[];
extern char g_Models[];
extern char g_MobyShadows[]; /* word 2: the shadow list's next entry */
extern char g_HudOT[];
extern char D_8006CBF8[];    /* sines, 256 half words (cosines from +0x80) */
extern char D_800770C8[];    /* the light's colour, 3 words */
extern char D_8006E3D8[];    /* base colours by specular type */
extern char D_8006E44C[];    /* shiny faces' two colours by specular type */

#define gte_mvmva_rt_v0() m2c_gte_0x486012()  /* MVMVA sf 1, R x V0 */
#define gte_mvmva_rt_ir() m2c_gte_0x49E012()  /* MVMVA sf 1, R x IR */
#define gte_mvmva_lc_ir0() m2c_gte_0x45E012() /* MVMVA sf 0, LCM x IR */
#define gte_cc() m2c_gte_0x138041C()          /* CC, sf 1 */

enum {
  TBL_LIT = 0x2400,         /* the lit mobys to draw, NULL-ended */
  TBL_LIT_COLOURS = 0x2A00, /* their lit vertex colours */
};
/* The scratchpad from 0x3E0: the camera's position and matrix. */
#define SCRATCH_CAMERA 0x3E0u

/* A Moby's fields this uses (include/moby.h). */
enum {
  MOBY_POS = 0xC,       /* x, y, z words */
  MOBY_SHADOW_DIST = 0x1C,
  MOBY_CLASS = 0x36,
  MOBY_ANGLES = 0x44,   /* m_Rotation x, y, z bytes, m_DepthOffset */
  MOBY_SPECULAR = 0x4C, /* m_SpecularMetalColor[3], m_SpecularMetalType */
  MOBY_RADIUS = 0x50,   /* m_RenderRadius (bit 7: a HUD model) */
  MOBY_WAS_DRAWN = 0x51,
  MOBY_SCALE = 0x57,    /* m_ScaleOverride */
};

/* A lit model (g_Models[class]): vertex count (signed byte), face count,
 * a shadow byte, then its vertices (3 bytes each), colours (when unlit) and
 * faces; from 0x10, a normal (3 signed bytes) per vertex. */
typedef struct {
  s8 m_NumVerts;
  u8 m_NumFaces;
  s8 m_Shadow;
  u8 m_Pad;
  u32 m_Verts;   /* bit 31 clear: (never on the PS1) */
  u32 m_Colors;
  u32 m_Faces;   /* 8 bytes each */
} LitModel;

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

/* The matrix turned by an angle (a byte, doubled: the sine table's index),
 * two of its columns from the matrix times a unit vector each (MVMVA). The
 * three are the asm's, the same in func_8001F158 and func_800208FC. */
static inline void MatTurnZ(Mat *m, u32 a) {
  u32 c, s, x0, y0, z0, x1, y1, z1;
  if (!a)
    return;
  c = AT16(D_8006CBF8 + 0x80 + a);
  s = AT16(D_8006CBF8 + a);
  MTC2(c & 0xFFFF, 0);
  MTC2(s, 1);
  gte_mvmva_rt_v0();
  x0 = MFC2(9);
  y0 = MFC2(10);
  z0 = MFC2(11);
  MTC2((0u - s) & 0xFFFF, 0);
  MTC2(c, 1);
  gte_mvmva_rt_v0();
  m->r11r12 = (m->r11r12 & 0xFFFF0000) + (x0 & 0xFFFF);
  m->r31r32 = (m->r31r32 & 0xFFFF0000) + (z0 & 0xFFFF);
  x1 = MFC2(9);
  y1 = MFC2(10);
  z1 = MFC2(11);
  m->r13r21 = (x1 & 0xFFFF) + (y0 << 16);
  m->r22r23 = (m->r22r23 & 0xFFFF) + (y1 << 16);
  m->r33 = z1 & 0xFFFF;
  MatToGte(m);
}

static inline void MatTurnY(Mat *m, u32 a) {
  u32 c, s, x0, y0, z0, x1, y1, z1;
  if (!a)
    return;
  c = AT16(D_8006CBF8 + 0x80 + a);
  s = AT16(D_8006CBF8 + a);
  MTC2(c << 16, 0);
  MTC2(s, 1);
  gte_mvmva_rt_v0();
  x0 = MFC2(9);
  y0 = MFC2(10);
  z0 = MFC2(11);
  MTC2(0u - (s << 16), 0);
  MTC2(c, 1);
  gte_mvmva_rt_v0();
  m->r11r12 = (m->r11r12 & 0xFFFF) + (x0 << 16);
  m->r31r32 = (m->r31r32 & 0xFFFF) + (z0 << 16);
  x1 = MFC2(9);
  y1 = MFC2(10);
  z1 = MFC2(11);
  m->r13r21 = (m->r13r21 & 0xFFFF0000) + (x1 & 0xFFFF);
  m->r22r23 = (y1 << 16) + (y0 & 0xFFFF);
  m->r33 = z1 & 0xFFFF;
  MatToGte(m);
}

static inline void MatTurnX(Mat *m, u32 a) {
  u32 c, s, x0, y0, z0, x1, y1, z1;
  if (!a)
    return;
  c = AT16(D_8006CBF8 + 0x80 + a);
  s = AT16(D_8006CBF8 + a);
  MTC2(0u, 1);
  MTC2((s << 16) + c, 0);
  gte_mvmva_rt_v0();
  x0 = MFC2(9);
  y0 = MFC2(10);
  z0 = MFC2(11);
  MTC2(((0u - s) & 0xFFFF) + (c << 16), 0);
  MTC2(0u, 1);
  gte_mvmva_rt_v0();
  m->r13r21 = (m->r13r21 & 0xFFFF) + (y0 << 16);
  x1 = MFC2(9);
  y1 = MFC2(10);
  z1 = MFC2(11);
  m->r11r12 = (x1 << 16) + (x0 & 0xFFFF);
  m->r31r32 = (z1 << 16) + (z0 & 0xFFFF);
  m->r22r23 = (m->r22r23 & 0xFFFF0000) + (y1 & 0xFFFF);
  MatToGte(m);
}

/* A lit model's vertex: 3 bytes, z 8 bits, x 9 and y 9 overlapping it by one
 * (signed, z doubled). */
static inline void LitVertexToV0(u32 w) {
  u32 z = (u32)((s32)(w << 24) >> 23);
  u32 x = (u32)((s32)(w << 16) >> 23);
  u32 y = (u32)((s32)(w << 8) >> 23);
  MTC2(x + (y << 16), 0);
  MTC2(z, 1);
}

static inline u32 Load24(const u8 *p) {
  return p[0] | (u32)p[1] << 8 | (u32)p[2] << 16;
}

/* A colour component (x16) as a byte, saturating at 0x1000. */
static inline u32 Sat8(u32 v) {
  return (s32)(v - 0x1000) < 0 ? v >> 4 : 0xFF;
}

/* The highlight: on a colour whose top nibble is set, IR (the light) above
 * that nibble and the back colour's red brightens all three components. */
static inline u32 Highlight(u32 col, u32 base, u32 rbk) {
  u32 k = base >> 28, d;
  if (!k)
    return col;
  d = MFC2(9) - (k << 7) - rbk;
  if ((s32)d <= 0)
    return col;
  d <<= 1;
  {
    u32 r = MFC2(9) + d, g = MFC2(10) + d, b = MFC2(11) + d;
    return Sat8(r) + (Sat8(g) << 8) + (Sat8(b) << 16);
  }
}

/* The base colour into the GTE for CC: back colour, IR0 its scale, RGB
 * white. The normal must be in IR (GPF scales it by IR0). */
static inline void LightNormal(u32 base) {
  const u32 rbk = (base << 4) & 0xFF0, gbk = (base >> 4) & 0xFF0, bbk = (base >> 12) & 0xFF0;
  MTC2((base >> 23) & 0x1E, 8);
  gte_gpf0();
  MTC2(0xFFFFFFu, 6);
  CTC2(rbk, 13);
  CTC2(gbk, 14);
  CTC2(bbk, 15);
  gte_cc();
}

/* What the lit face loop works with. */
typedef struct {
  u32 prim;          /* t9 */
  u32 lowest;        /* s2 */
  u32 highest;       /* s3 */
  u32 zbias;         /* t8 */
  u32 flags;         /* s7: clip (bit 31), semi-transparent (1), shiny (2) */
  u32 spec;          /* lo: the moby's specular word */
  const u8 *colours; /* s4 */
} LitCtx;

static inline void LitLink(LitCtx *c, u32 bucket, u32 size) {
  u32 *b = (u32 *)(uintptr_t)bucket;
  u32 last = b[0];
  b[0] = c->prim;
  if (last) {
    u32 t = MIPS_KUSEG(last);
    AT16(t) = (u16)c->prim;
    AT8(t + 2) = (u8)(c->prim >> 16);
  } else {
    if ((s32)(c->lowest - bucket) > 0)
      c->lowest = bucket;
    b[1] = c->prim;
    if ((s32)(c->highest - bucket) < 0)
      c->highest = bucket;
  }
  c->prim += size;
}

#define LIT_DEPTH(i) WORD_AT(SCRATCH_BASE + SCRATCH_DEPTHS, (i))
#define LIT_BUCKET(sum) (((u32)((s32)(sum) >> 5) << 3) + PSX_ADDR(D_8006FCF4 + TBL_BUCKETS))

/* A Gouraud face (bit 0 clear): a quad, or a triangle when its last two
 * vertices are the same. xy: the screen positions; NCLIP has (0 1 2). */
static inline void LitGouraud(LitCtx *c, u32 cw, const u32 *i, const u32 *xy) {
  const u32 p = c->prim;
  u32 col0, col1, col2, sum;
  if (i[2] == i[3]) {
    if ((s32)MFC2(24) <= 0)
      return;
    col0 = WORD_AT(c->colours, (cw >> 21) & 0x1FC);
    col1 = WORD_AT(c->colours, (cw >> 14) & 0x1FC);
    col2 = WORD_AT(c->colours, (cw >> 7) & 0x1FC);
    AT32(p + 0x8) = xy[0];
    AT32(p + 0x10) = xy[1];
    AT32(p + 0x18) = xy[2];
    sum = LIT_DEPTH(i[0]) + LIT_DEPTH(i[1]) + LIT_DEPTH(i[2]) + LIT_DEPTH(i[2]) - c->zbias;
    AT32(p + 0x4) = col0 + 0x30000000u;
    if ((s32)sum <= 0)
      return;
    AT32(p + 0xC) = col1;
    AT32(p + 0x14) = col2;
    AT32(p) = 0x06000000u;
    LitLink(c, LIT_BUCKET(sum) + ((cw & 3) << 3), 0x1C);
  } else {
    u32 m = MFC2(24), col3;
    MTC2(xy[3], 12);
    if ((s32)m <= 0) {
      gte_nclip();
      if ((s32)MFC2(24) >= 0)
        return;
    }
    col0 = WORD_AT(c->colours, (cw >> 21) & 0x1FC) + 0x38000000u;
    col1 = WORD_AT(c->colours, (cw >> 14) & 0x1FC);
    col2 = WORD_AT(c->colours, (cw >> 7) & 0x1FC);
    col3 = WORD_AT(c->colours, cw & 0x1FC);
    AT32(p + 0x8) = xy[0];
    AT32(p + 0x10) = xy[1];
    AT32(p + 0x18) = xy[2];
    AT32(p + 0x20) = xy[3];
    sum = LIT_DEPTH(i[0]) + LIT_DEPTH(i[1]) + LIT_DEPTH(i[2]) + LIT_DEPTH(i[3]) - c->zbias;
    if ((s32)sum <= 0)
      return;
    AT32(p + 0x4) = col0;
    AT32(p + 0xC) = col1;
    AT32(p + 0x14) = col2;
    AT32(p + 0x1C) = col3;
    AT32(p) = 0x08000000u;
    LitLink(c, LIT_BUCKET(sum) + ((cw & 3) << 3), 0x24);
  }
}

/* The face's normal, cw's three high bytes (signed), into IR. */
static inline void FaceNormalToIr(u32 cw) {
  MTC2((u32)((s32)cw >> 24), 11);
  MTC2((u32)((s32)(cw << 8) >> 24), 9);
  MTC2((u32)((s32)(cw << 16) >> 24), 10);
}

/* Not facing the camera: the first half's NCLIP, then (for a quad) the
 * second's, (3 1 2), which faces it when negative. */
static inline int LitBackFacing(const u32 *i, const u32 *xy) {
  MTC2(xy[3], 12);
  if (i[2] == i[3])
    return 1;
  gte_nclip();
  return (s32)MFC2(24) >= 0;
}

/* A flat face (bit 0, not bit 1): one colour, lit from the face's normal
 * (POLY_F3/F4), semi-transparent for specular type 0. */
static inline void LitFlat(LitCtx *c, u32 cw, const u32 *i, const u32 *xy) {
  const u32 p = c->prim;
  const u32 type = (u32)((s32)c->spec >> 22);
  u32 base, sum, bucket, tag, code, size, col;
  if ((s32)MFC2(24) <= 0 && LitBackFacing(i, xy))
    return;
  base = AT32(PSX_ADDR(D_8006E3D8) + type);
  FaceNormalToIr(cw);
  AT32(p + 0x8) = xy[0];
  AT32(p + 0xC) = xy[1];
  gte_mvmva_rt_ir();
  AT32(p + 0x10) = xy[2];
  AT32(p + 0x14) = xy[3];
  LightNormal(base);
  sum = LIT_DEPTH(i[0]) + LIT_DEPTH(i[1]) + LIT_DEPTH(i[2]) + LIT_DEPTH(i[3]) - c->zbias;
  if ((s32)sum <= 0)
    return;
  bucket = LIT_BUCKET(sum);
  if (i[2] == i[3]) {
    tag = 0x04000000u;
    code = 0x20000000u;
    size = 0x14;
  } else {
    tag = 0x05000000u;
    code = 0x28000000u;
    size = 0x18;
  }
  if (!type) {
    code += 0x02000000u;
    c->flags |= 1;
  }
  col = MFC2(22);
  AT32(p) = tag;
  AT32(p + 0x4) = Highlight(col, base, (base << 4) & 0xFF0) | code;
  LitLink(c, bucket, size);
}

/* A shiny colour component: 0 below 0, 0xFF from 0xFFF. */
static inline u32 Clamp8(u32 v) {
  const u32 over = v - 0xFFF;
  if ((s32)v < 0)
    v = 0;
  v >>= 4;
  return (s32)over >= 0 ? 0xFF : v;
}

/* A shiny face (bits 0 and 1): flat, its colour from the face normal's
 * light (the light colour matrix) between two colours by specular type.
 * Near the camera it's drawn from both sides, a back face opaque and darker;
 * further away, front faces only, semi-transparent. */
static inline void LitShiny(LitCtx *c, u32 cw, const u32 *i, const u32 *xy) {
  const u32 p = c->prim;
  const u32 colours = PSX_ADDR(D_8006E44C) + (u32)((s32)c->spec >> 21);
  u32 m = MFC2(24), opaque, c1, c2, sum, k, r, g, b, tag, code, size;
  if ((s32)(CFC2(7) - 0x800) < 0) {
    opaque = (m >> 31) << 25;
  } else {
    opaque = 0x02000000u;
    if ((s32)m <= 0) {
      if (LitBackFacing(i, xy))
        return;
      m = MFC2(24);
    }
  }
  AT32(p + 0x8) = xy[0];
  AT32(p + 0xC) = xy[1];
  AT32(p + 0x10) = xy[2];
  AT32(p + 0x14) = xy[3];
  FaceNormalToIr(cw);
  sum = LIT_DEPTH(i[0]) + LIT_DEPTH(i[1]);
  gte_mvmva_rt_ir();
  c1 = AT32(colours);
  c2 = AT32(colours + 4);
  sum -= c->zbias;
  gte_mvmva_lc_ir0();
  c->flags |= 2;
  k = MFC2(25);
  MTC2((c2 << 4) & 0xFF0, 9);
  MTC2((c2 >> 4) & 0xFF0, 10);
  MTC2((c2 >> 12) & 0xFF0, 11);
  k = (u32)((s32)k >> 8);
  if ((s32)m < 0) {
    k = (u32)((s32)k >> 2);
    c1 >>= 1;
    sum += 0x200;
  }
  MTC2(k, 8);
  gte_gpf0();
  r = ((c1 << 4) & 0xFF0) + (u32)((s32)MFC2(25) >> 12);
  g = ((c1 >> 4) & 0xFF0) + (u32)((s32)MFC2(26) >> 12);
  b = ((c1 >> 12) & 0xFF0) + (u32)((s32)MFC2(27) >> 12);
  k -= 0x5C0;
  if ((s32)k > 0) {
    k <<= 3;
    r += k;
    g += k;
    b += k;
  }
  r = Clamp8(r);
  g = Clamp8(g);
  b = Clamp8(b);
  if (i[2] == i[3]) {
    tag = 0x04000000u;
    code = 0x22000000u;
    size = 0x14;
  } else {
    tag = 0x05000000u;
    code = 0x2A000000u;
    size = 0x18;
  }
  AT32(p) = tag;
  AT32(p + 0x4) = code - opaque + r + (g << 8) + (b << 16);
  sum += LIT_DEPTH(i[2]) + LIT_DEPTH(i[3]);
  if ((s32)sum <= 0)
    return;
  LitLink(c, LIT_BUCKET(sum), size);
}

/* abs, as the asm's `neg` when negative. */
static inline u32 Abs32(u32 v) {
  return (s32)v < 0 ? 0u - v : v;
}

/* Cull and draw one lit moby. 0: next moby; -1: the draw distance hook
 * ended the list. */
static int LitMoby(u32 moby, u32 *shadow) {
  u8 *const m = HPTR(u8, moby);
  u32 *const scr = (u32 *)SCRATCH_BASE;
  const u32 half = AT16(m + MOBY_RADIUS); /* radius | was drawn << 8 */
  const LitModel *model;
  u32 x, y, z, zone, angles, clip, dqa, drawn, zbias, n, i, all_out = ~0u, verts, model_addr;
  const u8 *vp;
  const u32 *f, *f_end;
  Mat r;
  LitCtx c;

  x = AT32(m + MOBY_POS);
  m[MOBY_WAS_DRAWN] = 0;
  if (g_lod_pass && (port_lod_moby_skip(half, moby) || port_lod_lit_full()))
    return (half & 0x100) ? 0 : -1;
  if (!(half & 0x80)) {
    /* In view: a box of the radius (wider when it was drawn last frame),
     * then the view frustum after the camera's matrix. */
    const u32 *cam = scr + SCRATCH_CAMERA / 4;
    u32 box = (u32)((s32)(half << 24) >> 16) + ((half & 0x100) << 1), so, shadow_dist;
    box = PORT_LOD_MOBY_BOX(box, moby);
    x = (u32)((s32)(x - cam[0]) >> 2);
    if ((s32)(x + box) <= 0 || (s32)(x - box) >= 0)
      return 0;
    y = (u32)((s32)(cam[1] - AT32(m + MOBY_POS + 4)) >> 2);
    if ((s32)(y + box) <= 0 || (s32)(y - box) >= 0)
      return 0;
    z = (u32)((s32)(cam[2] - AT32(m + MOBY_POS + 8)) >> 2);
    if ((s32)(z + box) <= 0 || (s32)(z - box) >= 0)
      return 0;
    r.r11r12 = cam[3];
    r.r13r21 = cam[4];
    r.r22r23 = cam[5];
    r.r31r32 = cam[6];
    r.r33 = cam[7];
    MatToGte(&r);
    MTC2(x, 11);
    MTC2(y, 9);
    MTC2(z, 10);
    gte_mvmva_rt_ir();
    z = MFC2(27);
    y = MFC2(26);
    x = MFC2(25);
    if ((s32)(z - box) >= 0 || (s32)(z + 0x80) <= 0)
      return 0;
    if ((s32)(((Abs32(x) - 0x66) << 2) - (z + 0x4D) - ((z + 0x4D) << 1)) >= 0)
      return 0;
    shadow_dist = AT32(m + MOBY_SHADOW_DIST);
    if (g_lod_pass)
      shadow_dist = 0;
    model_addr = AT32(g_Models + AT16(m + MOBY_CLASS) * 4);
    model = HPTR(const LitModel, model_addr);
    if ((s32)shadow_dist < 0) {
      AT32(*shadow) = moby; /* the shadow list: moby, shadow byte */
      if ((s32)(z - 0x1100) < 0) {
        *shadow += 8;
        AT32(*shadow - 4) = (u32)(s32)model->m_Shadow;
      }
    }
    {
      const u32 ay = Abs32(y) - 0x79;
      if ((s32)((z + 0x28) - ay - (ay << 1)) <= 0)
        return 0;
    }
    /* Clip unless it's well inside the view. */
    clip = 0x80000000u;
    angles = AT32(m + MOBY_ANGLES);
    {
      const u32 ax = Abs32(x) + 0x66, zx = z - 0x4D;
      so = m[MOBY_SCALE];
      if ((s32)((ax << 2) - zx - (zx << 1)) < 0) {
        const u32 ay = Abs32(y) + 0x79, zy = z - 0x28;
        if ((s32)(zy - ay - (ay << 1)) > 0)
          clip = 0;
      }
    }
    x <<= 1;
    y <<= 1;
    z <<= 1;
    zone = (u32)((s32)z >> 6);
    dqa = 0;
    if (so) {
      MTC2(so, 8);
      MTC2(x, 9);
      MTC2(y, 10);
      MTC2(z, 11);
      gte_gpf0();
      z = (u32)((s32)MFC2(27) >> 5);
      y = (u32)((s32)MFC2(26) >> 5);
      x = (u32)((s32)MFC2(25) >> 5);
    }
  } else {
    /* A HUD model: at its screen position (x, y), z its depth; a fixed
     * matrix (the widescreen hooks narrow it and the offset, present.c). */
    u32 ofx = x << 16;
    const u32 ofy = AT32(m + MOBY_POS + 4) << 16;
    ofx = PORT_WS_HUD_OFX(ofx);
    CTC2(ofx, 24);
    CTC2(ofy, 25);
    x = 0;
    y = 0;
    z = (u32)((s32)AT32(m + MOBY_POS + 8) >> 1);
    zone = (u32)((s32)z >> 6);
    clip = 0;
    angles = AT32(m + MOBY_ANGLES);
    model_addr = AT32(g_Models + AT16(m + MOBY_CLASS) * 4);
    model = HPTR(const LitModel, model_addr);
    r.r11r12 = 0x1000;
    r.r13r21 = 0;
    r.r22r23 = 0xA00;
    r.r31r32 = 0;
    r.r33 = 0x1000;
    r.r11r12 = PORT_WS_HUD_R11(r.r11r12);
    MatToGte(&r);
    dqa = 1;
  }
  CTC2(dqa, 27); /* DQA: 1 for a HUD model */
  drawn = 1;
  PORT_LOD_MOBY_DRAWN(drawn, moby);
  m[MOBY_WAS_DRAWN] = (u8)drawn;
  zone -= (u32)((s32)angles >> 24); /* the depth offset */
  if ((s32)zone < 0)
    zone = 0;
  zbias = z - 0x100;
  zbias = (s32)zbias >= 0 ? zbias << 2 : 0;
  CTC2(x, 5);
  CTC2(y, 6);
  CTC2(z, 7);
  MatTurnZ(&r, (angles >> 15) & 0x1FE);
  MatTurnY(&r, (angles & 0xFF00) >> 7);
  MatTurnX(&r, (angles & 0xFF) << 1);

  /* The vertices, into the scratchpad: screen positions (or clip words),
   * depths from 0x200. */
  verts = model->m_Verts;
  vp = HPTR(const u8, verts);
  n = (u32)(s32)model->m_NumVerts;
  LitVertexToV0(AT32(vp));
  i = 0;
  do {
    u32 sxy, sz;
    gte_rtps();
    vp += 3;
    LitVertexToV0(Load24(vp));
    sxy = MFC2(14);
    sz = MFC2(19);
    if (clip) {
      const u32 flags = ClipFlags(sxy);
      scr[SCRATCH_DEPTHS / 4 + i] = sz;
      all_out &= flags;
      scr[i] = flags;
    } else {
      scr[i] = sxy;
      scr[SCRATCH_DEPTHS / 4 + i] = sz;
    }
  } while (++i != n);
  if (clip && (s32)(all_out & 0xF) > 0)
    return 0; /* every vertex off the same side */

  /* The vertex colours: each normal turned by the matrix, lit (CC) from the
   * moby's base colour, never darker than it. */
  c.colours = HPTR(const u8, model->m_Colors);
  if (model_addr + 0x10 != (verts & 0x7FFFFFFF)) {
    const u32 spec = AT32(m + MOBY_SPECULAR);
    const u32 base = AT32(PSX_ADDR(D_8006E3D8) + (u32)((s32)spec >> 22));
    const u32 floor = (base << 8) >> 8;
    const s8 *nrm = (const s8 *)model + 0x10;
    u32 *out = (u32 *)(D_8006FCF4 + TBL_LIT_COLOURS);
    const u32 count = (u32)(s32)model->m_NumVerts;
    i = 0;
    do {
      u32 col;
      MTC2((u32)(s32)nrm[0], 11);
      MTC2((u32)(s32)nrm[1], 9);
      MTC2((u32)(s32)nrm[2], 10);
      gte_mvmva_rt_ir();
      nrm += 3;
      LightNormal(base);
      col = MFC2(22);
      if ((s32)(col - floor) < 0)
        col = floor;
      else
        col = Highlight(col, base, (base << 4) & 0xFF0);
      out[i] = col;
    } while (++i != count);
    c.colours = (const u8 *)(D_8006FCF4 + TBL_LIT_COLOURS);
  }

  /* The faces, into the buckets by depth (a sum, less the bias, over 32). */
  c.spec = AT32(m + MOBY_SPECULAR);
  f = HPTR(const u32, model->m_Faces);
  f_end = f + model->m_NumFaces * 2;
  CTC2(zone, 28); /* DQB keeps the zone */
  c.prim = *(u32 *)D_800757B0;
  c.lowest = PSX_ADDR(D_8006FCF4 + TBL_BUCKETS) + 0x900;
  c.highest = PSX_ADDR(D_8006FCF4 + TBL_BUCKETS);
  c.zbias = zbias;
  c.flags = clip;
  for (; f != f_end; f += 2) {
    const u32 fw = f[0], cw = f[1];
    u32 idx[4], xy[4];
    idx[0] = (fw >> 21) & 0x1FC;
    idx[1] = (fw >> 14) & 0x1FC;
    idx[2] = (fw >> 7) & 0x1FC;
    idx[3] = fw & 0x1FC;
    xy[0] = WORD_AT(scr, idx[0]);
    xy[1] = WORD_AT(scr, idx[1]);
    xy[2] = WORD_AT(scr, idx[2]);
    xy[3] = WORD_AT(scr, idx[3]);
    if (c.flags & 0x80000000u) {
      if ((s32)(xy[0] & xy[1] & xy[2] & xy[3] & 0xF) > 0)
        continue;
      xy[0] = CLIP_XY(xy[0]);
      xy[1] = CLIP_XY(xy[1]);
      xy[2] = CLIP_XY(xy[2]);
      xy[3] = CLIP_XY(xy[3]);
    }
    MTC2(xy[0], 12);
    MTC2(xy[1], 13);
    MTC2(xy[2], 14);
    gte_nclip();
    if (!(fw & 1))
      LitGouraud(&c, cw, idx, xy);
    else if (!(fw & 2))
      LitFlat(&c, cw, idx, xy);
    else
      LitShiny(&c, cw, idx, xy);
  }

  /* The screen offset back; the buckets, highest first, into one chain
   * for the OT slot of its zone (the HUD OT's first for a HUD model at
   * zone 0), after a draw-mode primitive (the blend mode for its
   * semi-transparent faces) when it has any. */
  CTC2(0x1000000u, 24);
  CTC2(0x780000u, 25);
  *(u32 *)D_800757B0 = c.prim;
  if ((s32)(c.highest - c.lowest) >= 0) {
    u32 b = c.highest, tail = AT32(b), head = AT32(b + 4), ot, slot, last, mode;
    AT32(b) = 0;
    AT32(b + 4) = 0;
    while (b != c.lowest) {
      const u32 h = AT32(b - 4);
      u32 t;
      b -= 8;
      if (!h)
        continue;
      t = MIPS_KUSEG(tail);
      AT16(t) = (u16)h;
      AT8(t + 2) = (u8)(h >> 16);
      tail = AT32(b);
      AT32(b) = 0;
      AT32(b + 4) = 0;
    }
    ot = *(u32 *)g_WorldOT;
    slot = CFC2(28); /* the zone */
    if (!slot && CFC2(27))
      ot = *(u32 *)g_HudOT;
    slot <<= 3;
    slot = MIPS_KUSEG(ot + slot);
    last = AT32(slot);
    mode = c.flags & 0xF;
    if (mode) {
      const u32 dr = c.prim;
      AT32(dr) = 0x02000000u;
      AT32(dr + 4) = 0xE1000000u | 0x600 | ((mode - 1) << 5);
      AT32(dr + 8) = 0;
      *(u32 *)D_800757B0 = dr + 0xC;
      AT32(slot) = dr;
      if (last) {
        const u32 t = MIPS_KUSEG(last);
        AT16(t) = (u16)dr;
        AT8(t + 2) = (u8)(dr >> 16);
      } else {
        AT32(slot + 4) = dr;
      }
      last = dr;
    }
    AT32(slot) = tail;
    if (last) {
      const u32 t = MIPS_KUSEG(last);
      AT16(t) = (u16)head;
      AT8(t + 2) = (u8)(head >> 16);
    } else {
      AT32(slot + 4) = head;
    }
  }
  return 0;
}

static u32 LitDraw(u32 a0, u32 a1, u32 a2, u32 a3) {
  u32 *const scr = (u32 *)SCRATCH_BASE;
  const u32 *cam = (const u32 *)g_Camera;
  const u32 *light = (const u32 *)D_800770C8;
  u32 list = PSX_ADDR(D_8006FCF4 + TBL_LIT);
  u32 shadow = ((u32 *)g_MobyShadows)[2];
  (void)a0;
  (void)a1;
  (void)a2;
  (void)a3;
  hand_spill_regs();
  scr[SCRATCH_CAMERA / 4 + 0] = cam[10]; /* position */
  scr[SCRATCH_CAMERA / 4 + 1] = cam[11];
  scr[SCRATCH_CAMERA / 4 + 2] = cam[12];
  scr[SCRATCH_CAMERA / 4 + 3] = cam[0]; /* matrix */
  scr[SCRATCH_CAMERA / 4 + 4] = cam[1];
  scr[SCRATCH_CAMERA / 4 + 5] = cam[2];
  scr[SCRATCH_CAMERA / 4 + 6] = cam[3];
  scr[SCRATCH_CAMERA / 4 + 7] = cam[4];
  /* the light colour matrix: the light's colour in each column */
  CTC2((light[1] << 16) + light[0], 16);
  CTC2((light[1] << 16) + light[0], 19);
  CTC2((light[0] << 16) + light[2], 17);
  CTC2((light[2] << 16) + light[1], 18);
  CTC2(light[2], 20);
  for (;;) {
    const u32 moby = AT32(list);
    list += 4;
    if (moby && LitMoby(moby, &shadow) == 0)
      continue;
    if (PORT_LOD_MOBY_PASS2(1, 0)) {
      list = PSX_ADDR(D_8006FCF4 + TBL_LIT);
      continue;
    }
    break;
  }
  ((u32 *)g_MobyShadows)[2] = shadow;
  return 0; /* v0 and v1 are dead at every caller (tools/oracle/ignore_ranges.txt) */
}

HAND_RENDERER(func_80022A2C, LitDraw)

/* ---- func_80020F34: the specular (shiny and metal) mobys ----
 *
 * The same records and set-up as func_8001F798 (from func_800208FC's cull,
 * which also copies the moby's specular word into m_Specular), but each
 * vertex keeps its camera-space position next to its depth (TBL_BIG_DEPTHS,
 * 8 bytes: depth, x, y, z), and the triangles flagged bit 2 are lit from
 * their normal (type 0: a highlight, func_80020F34's OP/SQR/CC block) or
 * environment-mapped (types 1 and 2, metal: each vertex's UV from its
 * direction, D_80022110/D_80022148/D_80022180 caching it in the entry). */

extern char D_80074B84[]; /* 1/sqrt by normalised mantissa (s16) */
extern char D_8006D908[]; /* atan by ratio x 64 (u8) */
extern char D_8006D808[]; /* the vertical angle by z/len (s8) */
extern char D_800756F0[]; /* the environment map's texture words (clut, tpage) */

#define gte_op0() m2c_gte_0x170000C()         /* OP, sf 0 */
#define gte_sqr0() m2c_gte_0xA00428()         /* SQR, sf 0 */
#define gte_mvmva_ll_ir0() m2c_gte_0x43E012() /* MVMVA sf 0, LLM x IR */

/* A specular vertex entry: depth (SZ3), then the camera-space position
 * (MAC1-3 after RTPS); x 0x4000 once a metal UV is cached in y/z. */
#define SPEC_CACHED 0x4000

/* Transform as Transform, each entry getting the vertex's position. */
static inline int SpecTransform(VtxStream cur, u32 n, u32 shift, u32 *xy, u16 *ent, int clip) {
  u32 all_out = ~0u, raw = *cur.word++, delta = raw & 1, i = 0;
  Vtx v;
  VtxLoad(&v, raw);
  MTC2(v.z, 1);
  MTC2(v.x + (v.y << 16), 0);
  do {
    u32 sz, sxy;
    raw = StreamNext(&cur, delta);
    gte_rtps();
    VtxNext(&v, raw, delta, shift);
    MTC2(v.x + (v.y << 16), 0);
    MTC2(v.z, 1);
    ent[1] = (u16)MFC2(25);
    ent[2] = (u16)MFC2(26);
    ent[3] = (u16)MFC2(27);
    sz = MFC2(19);
    sxy = MFC2(14);
    ent[0] = (u16)sz;
    ent += 4;
    delta = raw & 1;
    if (clip) {
      u32 flags = ClipFlags(sxy);
      all_out &= flags;
      xy[i] = flags;
    } else {
      xy[i] = sxy;
    }
  } while (++i != n);
  return !clip || (s32)(all_out & 0x1F) <= 0;
}

static inline int SpecTransformBlend(VtxStream cur, VtxStream next, u32 n, u32 progress, u32 shift,
                                     u32 *xy, u16 *ent, int clip) {
  u32 all_out = ~0u, i = 0;
  u32 raw = *cur.word++, delta = raw & 1;
  u32 nraw = *next.word++, ndelta = nraw & 1;
  Vtx v, nv;
  VtxLoad(&v, raw);
  VtxLoad(&nv, nraw);
  MTC2(progress, 8);
  MTC2(v.z, 9);
  MTC2(v.x, 10);
  MTC2(v.y, 11);
  CTC2(nv.z, 21);
  CTC2(nv.x, 22);
  CTC2(nv.y, 23);
  gte_intpl();
  raw = StreamNext(&cur, delta);
  BlendToV0();
  do {
    u32 sz, sxy;
    gte_rtps();
    nraw = StreamNext(&next, ndelta);
    VtxNext(&v, raw, delta, shift);
    delta = raw & 1;
    VtxNext(&nv, nraw, ndelta, shift);
    MTC2(progress, 8);
    MTC2(v.z, 9);
    MTC2(v.x, 10);
    MTC2(v.y, 11);
    CTC2(nv.z, 21);
    CTC2(nv.x, 22);
    CTC2(nv.y, 23);
    ent[1] = (u16)MFC2(25);
    ent[2] = (u16)MFC2(26);
    ent[3] = (u16)MFC2(27);
    sxy = MFC2(14);
    sz = MFC2(19);
    gte_intpl();
    raw = StreamNext(&cur, delta);
    ndelta = nraw & 1;
    BlendToV0();
    ent[0] = (u16)sz;
    ent += 4;
    if (clip) {
      u32 flags = ClipFlags(sxy);
      all_out &= flags;
      xy[i] = flags;
    } else {
      xy[i] = sxy;
    }
  } while (++i != n);
  return !clip || (s32)(all_out & 0x1F) <= 0;
}

/* MIPS div's quotient (division by zero as the R3000A gives it). */
static inline u32 DivLo(u32 a, u32 b) {
  u32 hi, lo;
  mips2c_div(a, b, &hi, &lo);
  return lo;
}

/* 1/sqrt(len2) (scaled), from LZCS/LZCR and the table D_80074B84; 0 for
 * 0 (LZCS still written). */
static inline u32 InvSqrt(u32 len2) {
  u32 lz, k, half, t, r;
  MTC2(len2, 30);
  if (!len2)
    return 0;
  lz = MFC2(31);
  k = lz & ~1u;
  half = (u32)((s32)(0x1F - k) >> 1);
  k -= 0x18;
  t = (s32)k >= 0 ? len2 << (k & 31) : (u32)((s32)len2 >> ((0u - k) & 31));
  t = (t - 0x40) << 1;
  r = (u32)(s32)(s16)AT16(PSX_ADDR(D_80074B84) + t);
  return (r << (half & 31)) >> 12;
}

/* The angle of (a, b) in 256ths of a turn's quadrant-relative form, as the
 * asm builds it: a quadrant base and whether the table's angle (by the
 * smaller over the larger) is subtracted. */
static inline void Octant(u32 a, u32 b, u32 *neg, u32 *base) {
  if ((s32)a < 0) {
    a = 0u - a;
    if ((s32)b < 0) {
      b = 0u - b;
      if ((s32)(a - b) < 0) { *neg = 1; *base = 0xC0; } else { *neg = 0; *base = 0x80; }
    } else {
      if ((s32)(a - b) < 0) { *neg = 0; *base = 0x40; } else { *neg = 1; *base = 0x80; }
    }
  } else if ((s32)b < 0) {
    b = 0u - b;
    if ((s32)(a - b) < 0) { *neg = 0; *base = 0xC0; } else { *neg = 1; *base = 0x100; }
  } else {
    if ((s32)(a - b) < 0) { *neg = 1; *base = 0x40; } else { *neg = 0; *base = 0; }
  }
}

/* min(|a|,|b|) x 64 / max(|a|,|b|) (max at least 1). */
static inline u32 SlopeRatio(u32 a, u32 b) {
  u32 p = Abs32(a), q = Abs32(b);
  if ((s32)(p - q) > 0) {
    u32 t = p;
    p = q;
    q = t;
  }
  if (!q)
    q = 1;
  return DivLo(p << 6, q);
}

/* A metal vertex's UV (subroutine L_800222C0, returning through D_80022110,
 * D_80022148, D_80022180): its direction from the moby's centre turned by
 * the light matrix, the horizontal angle (with the moby's own turn in the
 * specular word's low 9 bits) and the vertical one; type 2 (spec negative)
 * maps them through a circle instead. */
static u32 MetalUV(u32 x, u32 y, u32 z, u32 spec) {
  const u32 tx = CFC2(5), ty = CFC2(6), tz = CFC2(7);
  u32 m1, m2, m3, ratio, len, q, neg, base, ang, v, u, e, sc, idx;
  x -= tx;
  y -= ty;
  z -= tz;
  MTC2(x, 9);
  MTC2(y, 10);
  MTC2(z, 11);
  gte_mvmva_ll_ir0();
  MTC2(x, 9);
  MTC2(y, 10);
  MTC2(z, 11);
  m1 = MFC2(25);
  m2 = MFC2(26);
  m3 = MFC2(27);
  gte_sqr0();
  ratio = SlopeRatio(m1, m2);
  len = MFC2(25) + MFC2(26) + MFC2(27);
  /* the asm reads the ratio inside the 1/sqrt block only: for a zero
   * length the table index is TRX, left in its register */
  idx = len ? ratio : tx;
  len = InvSqrt(len);
  q = DivLo(m3, len);
  Octant(m1, m2, &neg, &base);
  ang = AT8(PSX_ADDR(D_8006D908) + idx);
  ang = neg ? base - ang : base + ang;
  v = (u32)((s32)q >> 5) + 0x80;
  {
    const u32 over = v - 0x100;
    if ((s32)v < 0)
      v = 0;
    if ((s32)over >= 0)
      v = 0xFF;
  }
  sc = (CFC2(13) >> 8) & 31;
  e = (u32)(s32)(s8)AT8(PSX_ADDR(D_8006D808) + v);
  u = (((CFC2(7) << sc) >> 6) + (spec & 0x1FF) + ang) & 0xFF;
  if ((s32)spec >= 0)
    return (e << 8) | (u >> 1);
  if ((s32)(e - 0x3F) > 0)
    e = 0x3F;
  u <<= 1;
  MTC2(e, 8);
  MTC2((u32)(s32)(s16)AT16(PSX_ADDR(D_8006CBF8) + u + 0x80), 9);
  MTC2((u32)(s32)(s16)AT16(PSX_ADDR(D_8006CBF8) + u), 10);
  gte_gpf0();
  return ((((u32)((s32)MFC2(26) >> 12) + 0x40) << 8) | ((u32)((s32)MFC2(25) >> 12) + 0x40));
}

/* A specular (type 0) triangle's colour: its normal (the cross product of
 * two edges, OP, through the rotation matrix's diagonal), its length (SQR),
 * the highlight's strength (spec bits 18-31 over the length) and the light
 * (D_800770C8 words 3-5 as the colour matrix) through CC on the specular
 * colour (spec's three 6-bit fields as the back colour). The asm leaves the
 * matrix's diagonal and the colour matrix that way. e0-e2: the vertices'
 * entries. */
static u32 SpecularLight(const u8 *e0, const u8 *e1, const u8 *e2, u32 spec) {
  const u32 *light = (const u32 *)D_800770C8;
  const u32 x0 = (u32)(s32)(s16)AT16(e0 + 2), w0 = AT32(e0 + 4);
  const u32 x1 = (u32)(s32)(s16)AT16(e1 + 2), w1 = AT32(e1 + 4);
  const u32 x2 = (u32)(s32)(s16)AT16(e2 + 2), w2 = AT32(e2 + 4);
  const u32 rbk = CFC2(13), gbk = CFC2(14);
  u32 nx, ny, nz, len, l1, l2, l3, rgb;
  MTC2(x1 - x0, 9);
  MTC2((u32)((s32)((w1 << 16) - (w0 << 16)) >> 16), 10);
  MTC2((u32)((s32)w1 >> 16) - (u32)((s32)w0 >> 16), 11);
  CTC2(x2 - x0, 0);
  CTC2((u32)((s32)((w2 << 16) - (w0 << 16)) >> 16), 2);
  CTC2((u32)((s32)w2 >> 16) - (u32)((s32)w0 >> 16), 4);
  gte_op0();
  nx = (u32)((s32)MFC2(25) >> 4);
  ny = (u32)((s32)MFC2(27) >> 4);
  nz = (u32)((s32)MFC2(26) >> 4);
  MTC2(nx, 9);
  MTC2(ny, 10);
  MTC2(nz, 11);
  gte_sqr0();
  CTC2((spec >> 6) & 0xFF0, 13);
  CTC2(spec & 0xFF0, 14);
  CTC2((spec << 6) & 0xFF0, 15);
  MTC2(nx, 9);
  MTC2(ny, 10);
  MTC2(nz, 11);
  len = InvSqrt(MFC2(25) + MFC2(26) + MFC2(27));
  len = DivLo((spec >> 18) << 14, len);
  l1 = light[3] & 0xFFFF;
  l2 = light[4] & 0xFFFF;
  l3 = light[5] & 0xFFFF;
  CTC2((l2 << 16) + l1, 16);
  CTC2((l1 << 16) + l3, 17);
  CTC2((l3 << 16) + l2, 18);
  CTC2((l2 << 16) + l1, 19);
  CTC2(l3, 20);
  MTC2(0xFFFFFFu, 6);
  MTC2(len, 8);
  gte_gpf0();
  {
    const u32 a = (u32)((s32)MFC2(25) >> 8), b = (u32)((s32)MFC2(26) >> 8), d = (u32)((s32)MFC2(27) >> 8);
    MTC2(a, 9);
    MTC2(b, 10);
    MTC2(d, 11);
  }
  gte_cc();
  rgb = MFC2(22);
  CTC2(rbk, 13);
  CTC2(gbk, 14);
  return rgb;
}

/* A metal triangle (types 1, 2): POLY_GT3 on the environment map
 * (D_800756F0's texture words), each vertex's UV from MetalUV, cached in its
 * entry; with spec bits 16-23 set, the colours shifted towards red by that.
 * Type 1 moves the u of the vertices below 0x40 up by 0x80 when the three
 * straddle the map's seam. */
static void MetalTriangle(FaceCtx *c, u32 i0, u32 i1, u32 i2, u32 col0, u32 col1, u32 col2,
                          u32 cw, u32 spec) {
  const u32 p = c->prim;
  const u32 bright = (spec >> 16) & 0xFF;
  u8 *const e[3] = {(u8 *)c->depth + i0 * 2, (u8 *)c->depth + i1 * 2, (u8 *)c->depth + i2 * 2};
  const u32 *tex = (const u32 *)D_800756F0;
  u32 k, sum, key, bucket, u0, u1, u2;
  if (bright) {
    static const u8 off[3] = {0x4, 0x10, 0x1C};
    const u32 col[3] = {col0, col1, col2};
    for (k = 0; k < 3; k++) { /* red up, saturating */
      u32 v = (col[k] & 0xFF) + bright;
      if ((s32)(v - 0x100) >= 0)
        v = 0xFF;
      AT8(p + off[k]) = (u8)v;
    }
    for (k = 0; k < 6; k++) { /* green, then blue, down, not below 0 */
      const u32 v = ((col[k % 3] >> (k < 3 ? 8 : 16)) & 0xFF) - bright;
      const u32 at = p + off[k % 3] + (k < 3 ? 1 : 2);
      AT8(at) = (u8)v;
      if ((s32)v < 0)
        AT8(at) = 0;
    }
    AT8(p + 0x7) = 0x34;
  } else {
    AT32(p + 0x10) = col1;
    AT32(p + 0x1C) = col2;
    AT32(p + 0x4) = col0 + 0x34000000u;
  }
  for (k = 0; k < 3; k++) {
    u32 uv;
    if ((s32)(s16)AT16(e[k] + 2) == SPEC_CACHED) {
      uv = AT32(e[k] + 4);
    } else {
      const u32 w = AT32(e[k] + 4);
      uv = MetalUV((u32)(s32)(s16)AT16(e[k] + 2), (u32)((s32)(w << 16) >> 16), (u32)((s32)w >> 16),
                   spec);
      AT16(e[k] + 2) = SPEC_CACHED;
      AT32(e[k] + 4) = uv;
    }
    CTC2(uv, 21 + k); /* RFC-BFC hold them */
  }
  {
    const u32 z0 = AT16(e[0]), z1 = AT16(e[1]), z2 = AT16(e[2]);
    sum = z0 + (z0 >> 1) - c->bias + z1 + (z1 >> 1) + z2;
  }
  if ((s32)sum < 0)
    return;
  key = (u32)((s32)sum >> c->zshift) + FACE_ZOFF(cw);
  if ((s32)key < 0)
    return;
  bucket = (key << 3) + c->buckets;
  u0 = CFC2(21);
  u1 = CFC2(22);
  u2 = CFC2(23);
  if ((s32)spec >= 0) {
    const u32 t0 = (u0 & 0xFF) - 0x40, t1 = (u1 & 0xFF) - 0x40, t2 = (u2 & 0xFF) - 0x40;
    if ((s32)(t0 | t1 | t2) < 0 && (s32)(t0 & t1 & t2) >= 0) {
      const u32 d01 = t0 - t1, d02 = t0 - t2, d12 = t1 - t2;
      if ((s32)(d01 - 0x40) >= 0 || (s32)(d01 + 0x40) <= 0 || (s32)(d02 - 0x40) >= 0 ||
          (s32)(d02 + 0x40) <= 0 || (s32)(d12 - 0x40) >= 0 || (s32)(d12 + 0x40) <= 0) {
        u0 += (t0 >> 31) << 7;
        u1 += (t1 >> 31) << 7;
        u2 += (t2 >> 31) << 7;
      }
    }
  }
  AT32(p) = 0x09000000u;
  AT32(p + 0xC) = tex[0] + c->tex_bias;
  AT32(p + 0x18) = tex[1];
  AT16(p + 0xC) = (u16)u0;
  AT16(p + 0x18) = (u16)u1;
  AT16(p + 0x24) = (u16)u2;
  Link(c, bucket, 0x28);
}

/* A triangle: as DrawTriangle, and with bit 2 lit (type 0) or metal. */
HAND_INLINE const u32 *SpecTriangle(FaceCtx *c, const u32 *f, u32 spec) {
  const u32 fw = f[0], cw = f[1], textured = fw & FACE_TEXTURED;
  const u32 i0 = (fw >> 20) & 0x7FC, i1 = (fw >> 11) & 0x7FC, i2 = (fw >> 2) & 0x7FC;
  const u32 *next = f + (textured ? 5 : 2);
  const u32 p = c->prim;
  u32 xy0 = WORD_AT(c->xy, i0), xy1 = WORD_AT(c->xy, i1), xy2 = WORD_AT(c->xy, i2), m;
  u32 col0, col1, col2;
  if (c->clip) {
    if (CLIP_OUT(xy0 & xy1 & xy2))
      return next;
    xy0 = CLIP_XY(xy0);
    xy1 = CLIP_XY(xy1);
    xy2 = CLIP_XY(xy2);
  }
  MTC2(xy0, 12);
  MTC2(xy1, 13);
  MTC2(xy2, 14);
  gte_nclip();
  AT32(p + 0x8) = xy0;
  if (textured) {
    AT32(p + 0x14) = xy1;
    AT32(p + 0x20) = xy2;
  } else {
    AT32(p + 0x10) = xy1;
    AT32(p + 0x18) = xy2;
  }
  if (fw & FACE_HIDDEN)
    return next;
  m = MFC2(24);
  if (fw & FACE_TWO_SIDED ? m == 0 : (s32)m <= 0)
    return next;
  col0 = (COLOUR(c, (cw >> 17) & 0x7FC) << 8) >> 8;
  col1 = COLOUR(c, (cw >> 8) & 0x7FC);
  col2 = COLOUR(c, (cw << 1) & 0x7FC);
  if (fw & FACE_SPECULAR) {
    if ((s32)spec >> 24) {
      MetalTriangle(c, i0, i1, i2, col0, col1, col2, cw, spec);
      return next;
    }
    col0 = col1 = col2 = SpecularLight(c->depth + i0 * 2, c->depth + i1 * 2, c->depth + i2 * 2, spec);
  }
  EmitTriangle(c, next, i0, i1, i2, col0, col1, col2, cw, textured, 0, 1);
  return next;
}

/* A quad: as DrawQuad, but a two-sided one is drawn whole without the
 * second test. */
HAND_INLINE const u32 *SpecQuad(FaceCtx *c, const u32 *f) {
  const u32 fw = f[0], cw = f[1], w2 = f[2], textured = fw & FACE_TEXTURED;
  const u32 i0 = (fw >> 20) & 0x7FC, i1 = (fw >> 11) & 0x7FC, i2 = (fw >> 2) & 0x7FC;
  const u32 i3 = w2 & 0x7FC;
  const u32 *next = f + (textured ? 6 : 3);
  const u32 p = c->prim;
  const u32 c0 = (cw >> 17) & 0x7FC, c1 = (cw >> 8) & 0x7FC, c3 = (w2 >> 9) & 0x7FC;
  u32 xy0 = WORD_AT(c->xy, i0), xy1 = WORD_AT(c->xy, i1), xy2 = WORD_AT(c->xy, i2);
  u32 xy3 = WORD_AT(c->xy, i3), m, m2;
  if (c->clip) {
    if (CLIP_OUT(xy0 & xy1 & xy2 & xy3))
      return next;
    xy0 = CLIP_XY(xy0);
    xy1 = CLIP_XY(xy1);
    xy2 = CLIP_XY(xy2);
    xy3 = CLIP_XY(xy3);
  }
  MTC2(xy0, 12);
  MTC2(xy1, 13);
  MTC2(xy2, 14);
  if (fw & FACE_HIDDEN)
    return next;
  gte_nclip();
  AT32(p + 0x8) = xy0;
  if (textured) {
    AT32(p + 0x14) = xy1;
    AT32(p + 0x20) = xy2;
    AT32(p + 0x2C) = xy3;
  } else {
    AT32(p + 0x10) = xy1;
    AT32(p + 0x18) = xy2;
    AT32(p + 0x20) = xy3;
  }
  if (fw & FACE_TWO_SIDED) {
    EmitQuad(c, next, i0, i1, i2, i3, c0, c1, c3, cw, textured, 1);
    return next;
  }
  m = MFC2(24);
  MTC2(xy3, 12);
  gte_nclip();
  m2 = MFC2(24);
  if ((s32)m < 0) {
    if ((s32)m2 > 0)
      EmitQuad(c, next, i0, i1, i2, i3, c0, c1, c3, cw, textured, 1);
    else
      EmitTriangle(c, next, i0, i1, i2, (COLOUR(c, c0) << 8) >> 8, COLOUR(c, c1),
                   COLOUR(c, (cw << 1) & 0x7FC), cw, textured, 0, 1);
  } else if ((s32)m2 > 0) {
    AT32(p + 0x8) = xy3;
    EmitTriangle(c, next, i3, i1, i2, (COLOUR(c, c3) << 8) >> 8, COLOUR(c, c1),
                 COLOUR(c, (cw << 1) & 0x7FC), cw, textured, 1, 1);
  }
  return next;
}

/* A metal moby's set-up (asm L_800226F8): the light matrix from
 * D_800770C8 words 6-10, and the specular word's low 9 bits turned by the
 * angle between the camera's heading (g_Camera word 0x14) and the moby's
 * direction (TRX, TRZ); type 2 and up get the top byte 0xFF (the sign
 * MetalUV reads). */
static u32 MetalBegin(u32 spec) {
  const u32 *light = (const u32 *)D_800770C8;
  const u32 tz = CFC2(7), tx = CFC2(5);
  u32 ratio, neg, base, ang;
  if ((spec >> 24) - 1 != 0)
    spec |= 0xFF000000u;
  CTC2(light[6], 8);
  CTC2(light[7], 9);
  CTC2(light[8], 10);
  CTC2(light[9], 11);
  CTC2(light[10], 12);
  ratio = SlopeRatio(tz, tx);
  Octant(tz, tx, &neg, &base);
  ang = AT8(PSX_ADDR(D_8006D908) + ratio);
  ang = neg ? base - ang : base + ang;
  return spec + ((((u32)((s32)((const u32 *)g_Camera)[0x14] >> 3)) - (ang << 1)) & 0x1FF);
}

/* Draw one specular moby; returns the primitive pointer after it. */
static u32 SpecDrawRecord(const MobyRecord *rec, u32 prim) {
  u16 *const ent = (u16 *)(D_8006FCF4 + TBL_BIG_DEPTHS);
  RecState s;
  FaceCtx c;
  const u32 *faces, *end;
  u32 n, spec;

  if (!RecordBegin(rec, &s))
    return prim;
  ReplaceFaces(&s);
  n = s.anim->m_VertCount[s.detail < 0];
  if (!n)
    goto restore;
  if (s.progress) {
    if (!SpecTransformBlend(FrameStreams(s.frame), FrameStreams(HPTR(const MobyFrame, rec->m_NextFrame)),
                            n, s.progress, s.shift, (u32 *)SCRATCH_BASE, ent, s.clip))
      goto restore;
  } else if (!SpecTransform(FrameStreams(s.frame), n, s.shift, (u32 *)SCRATCH_BASE, ent, s.clip)) {
    goto restore;
  }

  faces = PickFaces(&s, &c);
  spec = rec->m_Specular;
  if (spec >> 24)
    spec = MetalBegin(spec);
  FaceCtxBegin(&c, &s, prim);
  c.depth = (const u8 *)ent;
  c.xy = (const u8 *)(uintptr_t)SCRATCH_BASE;
  end = (const u32 *)((const u8 *)(faces + 1) + faces[0]);
  for (faces++; faces != end;) {
    const u32 fw = faces[0];
    if (!(fw & FACE_QUAD))
      faces = SpecTriangle(&c, faces, spec);
    else if (fw & FACE_SPRITE)
      faces = DrawSprite(&c, faces, 1);
    else
      faces = SpecQuad(&c, faces);
  }
  if ((s32)(c.highest - c.lowest) >= 0)
    LinkBuckets(&c, s.zone, s.rbk);
  prim = c.prim;

restore:
  RestoreFaces();
  return prim;
}

static u32 SpecDraw(u32 a0, u32 a1, u32 a2, u32 a3) {
  u32 prim = *(u32 *)D_800757B0;
  u32 rec = PSX_ADDR(D_8006FCF4 + TBL_RECORDS);
  (void)a0;
  (void)a1;
  (void)a2;
  (void)a3;
  hand_spill_regs();
  CTC2(*(u32 *)g_SkipLowPolyWorld, 29); /* ZSF3 */
  for (;; rec += sizeof(MobyRecord)) {
    const MobyRecord *r = (const MobyRecord *)(uintptr_t)rec;
    if (PORT_LOD_RECORDS_FULL(rec, prim))
      break;
    if (!r->m_Flags)
      break;
    prim = SpecDrawRecord(r, prim);
  }
  *(u32 *)D_800757B0 = prim;
  return prim; /* v0 and v1 are dead at every caller (tools/oracle/ignore_ranges.txt) */
}

HAND_RENDERER(func_80020F34, SpecDraw)
