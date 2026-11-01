/* The moby (object) renderers of asm/renderers/r_moby.s (hand-written asm in
 * the original), written by hand from the asm as C (decisions D69, the D33
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
 * the world OT. */
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
