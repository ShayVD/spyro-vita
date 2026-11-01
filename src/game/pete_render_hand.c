/* Spyro's renderer, func_80023AC4 (asm/renderers/r_pete.s, hand-written
 * asm in the original), written by hand from the asm as C (decisions D72,
 * the D33 method). It replaces the mips2c translation when fast_render=1
 * (render_hand.h HAND_RENDERER); fast_render=0 runs the translation, which
 * stays linked as func_80023AC4_gen. Checked against PCSX-Redux by the
 * oracle replay and against the translation by host runs
 * (docs/translation-log.md).
 *
 * Spyro is three parts, body, head and tail, each animated on its own
 * (g_Spyro's animation, frame and progress bytes) and each optionally
 * blended towards a second animation frame. The renderer
 *  1. sets up a record per part in the work area (the frame's vertex
 *     streams), and the head's and tail's joint offsets from the body frame;
 *  2. transforms Spyro's position by the camera and gives up if it's off
 *     screen or too far;
 *  3. builds each part's matrix: the camera's, turned by the body's
 *     rotation, then the head's or the tail's (the computed jumps through
 *     D_80024054, D_800240F8 and D_800241A8 are the returns from that
 *     rotation subroutine, RotateMatrix below), and gives the flame the
 *     head's;
 *  4. decodes and transforms every vertex (delta-coded streams, blended
 *     with INTPL when the part blends), keeping each one's screen position
 *     and depth;
 *  5. optionally works out each vertex's height above the floor
 *     (g_Spyro.m_FloorDistance non-zero: Spyro is partly under a surface),
 *     and the colours with the colour filter (Fairy Kiss);
 *  6. emits a textured Gouraud triangle or quad (POLY_GT3/GT4) per visible
 *     face into depth buckets, clipping the faces against the floor in the
 *     second case; and
 *  7. links the buckets into the world OT.
 *
 * The GTE sees the same moves and commands as in the asm, in an order that
 * gives the same registers (moves to different registers commute), so the
 * pipelined V0 (the vertex after the last one) and the MFC2 read-backs
 * (VZ0, SZ3, IR1-3) leave the GTE as the asm does. */
#include "render_hand.h"

extern char g_Spyro[];
extern char g_Models[];     /* the model table's address */
extern char g_Camera[];
extern char g_SpyroFlame[];
extern char D_8006CBF8[];   /* sine, 256 halfwords */
extern char D_8006CC78[];   /* cosine, 256 halfwords */
extern char D_8006D614[];   /* the byte-coded vertex deltas, 128 words */
extern char D_8006FCF4[];   /* the renderers' work area */
extern char D_800757B0[];   /* the primitive buffer's next free byte */
extern char g_WorldOT[];

/* g_Spyro (include/spyro.h) */
enum {
  SP_POS = 0x00,          /* m_Position x, y, z */
  SP_ROTATION = 0x0C,     /* body, head, tail rotations (x, y, z bytes, pad) */
  SP_ANIM = 0x18,         /* animation, next animation, per part */
  SP_FRAME = 0x1E,        /* frame, next frame, per part */
  SP_PROGRESS = 0x24,     /* blend towards the next frame, per part */
  SP_SORT_DEPTH = 0x27,   /* m_sortingDepth */
  SP_COLOUR_FILTER = 0x28,/* r, g, b, amount */
  SP_FLOOR_DIST = 0x30,   /* m_FloorDistance */
  SP_MATRIX = 0x34,       /* m_RotationMatrix */
};

/* An animation header (g_Models + 0x38, by animation). */
enum {
  AN_COLOUR_COUNT = 0x02, /* halfword */
  AN_PART_END = 0x08,     /* each part's vertex end (bytes 8, 9, 10) */
  AN_DEPTH_SHIFT = 0x0B,
  AN_DELTAS = 0x10,       /* the vertices' base words */
  AN_FACES = 0x14,        /* byte length, then the faces */
  AN_COLOURS = 0x18,
  AN_FRAMES = 0x24,       /* packed frame pointers and format flags */
};

/* The work area, D_8006FCF4 */
#define WORK ((u32)(uintptr_t)D_8006FCF4)
enum {
  W_BUCKETS = 0x000,  /* depth buckets: last primitive, first (8 bytes) */
  W_BUCKETS_END = 0x900,
  W_VERTS = 0x900,    /* per vertex: SZ, VXY0 (then height), VZ0, unused */
  W_PARTS = 0x1900,   /* part records, PartRec */
  W_JOINTS = 0x19CC,  /* each part's joint offset (the body's is 0) */
  W_COLOURS = 0x1D00, /* the colours with the colour filter */
};

/* A part record (0x44 bytes; three, then a zero word). */
enum {
  PR_INFO = 0,        /* end << 16 (vertices * 4 above bit 14) | b fmt 0x2000
                         | a fmt 0x1000 | blend << 8; bit 31: own matrix */
  PR_TR = 1,          /* 3 words */
  PR_MATRIX = 4,      /* 5 words, as the GTE's R11R12..R33 */
  PR_A = 9,           /* the frame: base words, byte stream, halfword stream */
  PR_B = 13,          /* the frame blended to */
  PR_WORDS = 17,
};

/* GTE commands (gte_inline.h), as the asm's opcodes. */
#define gte_mvmva_ir() m2c_gte_0x49E012()   /* MVMVA sf R * IR, no TR */
#define gte_mvmva_v0() m2c_gte_0x486012()   /* MVMVA sf R * V0, no TR */
#define gte_mvmva_v0_tr() m2c_gte_0x480012() /* MVMVA sf R * V0 + TR */
#define gte_intpl() m2c_gte_0x980011()

#define TAG_GT3 0x09000000u
#define TAG_GT4 0x0C000000u

/* The asm's field extracts: sra after sll. */
#define SRA(x, n) ((u32)((s32)(u32)(x) >> (n)))
#define FIELD(x, l, r) SRA((u32)(x) << (l), (r))

static inline void SetMatrix(const u32 m[5]) {
  CTC2(m[0], 0);
  CTC2(m[1], 1);
  CTC2(m[2], 2);
  CTC2(m[3], 3);
  CTC2(m[4], 4);
}

/* asm 800241E0-80024400, entered with the return address in t8: the matrix
 * M (the CPU's copy, a3 t0 t1 t2 t3) turned by the angle bytes of ANGLES
 * (z, y, x; byte 2, 1, 0) in turn, each by two MVMVAs of the GTE's R (the
 * same matrix) on a cosine/sine vector. */
static void RotateMatrix(u32 m[5], u32 angles) {
  const u16 *sine = (const u16 *)D_8006CBF8, *cosine = (const u16 *)D_8006CC78;
  u32 i, s, c, a0, a1, a2, x, y, z;

  i = (angles >> 16) & 0xFF;
  if (i) {
    c = cosine[i];
    s = sine[i];
    MTC2(c, 0);
    MTC2(s, 1);
    gte_mvmva_v0();
    a0 = MFC2(9);
    a1 = MFC2(10);
    a2 = MFC2(11);
    MTC2((0u - s) & 0xFFFF, 0);
    MTC2(c, 1);
    gte_mvmva_v0();
    m[0] = (m[0] & 0xFFFF0000u) + (a0 & 0xFFFF);
    m[3] = (m[3] & 0xFFFF0000u) + (a2 & 0xFFFF);
    x = MFC2(9);
    y = MFC2(10);
    z = MFC2(11);
    m[1] = (x & 0xFFFF) + (a1 << 16);
    m[2] = (m[2] & 0xFFFF) + (y << 16);
    m[4] = z & 0xFFFF;
    SetMatrix(m);
  }
  i = (angles >> 8) & 0xFF;
  if (i) {
    c = cosine[i];
    s = sine[i];
    MTC2(c << 16, 0);
    MTC2(s, 1);
    gte_mvmva_v0();
    a0 = MFC2(9);
    a1 = MFC2(10);
    a2 = MFC2(11);
    MTC2(0u - (s << 16), 0);
    MTC2(c, 1);
    gte_mvmva_v0();
    m[0] = (m[0] & 0xFFFF) + (a0 << 16);
    m[3] = (m[3] & 0xFFFF) + (a2 << 16);
    x = MFC2(9);
    y = MFC2(10);
    z = MFC2(11);
    m[1] = (m[1] & 0xFFFF0000u) + (x & 0xFFFF);
    m[2] = (y << 16) + (a1 & 0xFFFF);
    m[4] = z & 0xFFFF;
    SetMatrix(m);
  }
  i = angles & 0xFF;
  if (i) {
    c = cosine[i];
    s = sine[i];
    MTC2(0, 1);
    MTC2((s << 16) + c, 0);
    gte_mvmva_v0();
    a0 = MFC2(9);
    a1 = MFC2(10);
    a2 = MFC2(11);
    MTC2(((0u - s) & 0xFFFF) + (c << 16), 0);
    MTC2(0, 1);
    gte_mvmva_v0();
    m[1] = (m[1] & 0xFFFF) + (a1 << 16);
    x = MFC2(9);
    y = MFC2(10);
    z = MFC2(11);
    m[0] = (x << 16) + (a0 & 0xFFFF);
    m[3] = (z << 16) + (a2 & 0xFFFF);
    m[2] = (m[2] & 0xFFFF0000u) + (y & 0xFFFF);
    SetMatrix(m);
  }
}

/* A part's frame into a part record's three stream pointers (PR_A or PR_B):
 * the base words (the animation's, past the earlier parts' vertices), the
 * byte stream and the halfword stream (offsets in the frame's words 2 and
 * 3, part by part). Returns the frame's format flag for the part's first
 * vertex and the frame's address (the frame word holds it as a 21-bit
 * halfword index into RAM: mips2c's packed-pointer rebase). */
static u32 PartStreams(u32 *streams, u32 anim, u32 frame_no, int part, u32 *frame_out) {
  const u8 *hdr = HPTR(const u8, anim);
  u32 fw = ((const u32 *)(hdr + AN_FRAMES))[frame_no];
  u32 frame = ((fw << 11) >> 10) + (u32)g_psx_arena_base;
  u32 data = frame + 0x18;
  u32 w2 = AT32(frame + 0x8);
  u32 deltas = *(const u32 *)(hdr + AN_DELTAS);
  switch (part) {
  case 0:
    streams[0] = deltas;
    streams[1] = (w2 >> 20) + data;
    streams[2] = data;
    break;
  case 1:
    streams[0] = deltas + ((u32)hdr[AN_PART_END] << 2);
    streams[1] = ((w2 >> 10) & 0x3FF) + data;
    streams[2] = (AT32(frame + 0xC) >> 16) + data;
    break;
  default:
    streams[0] = deltas + ((u32)hdr[AN_PART_END + 1] << 2);
    streams[1] = (w2 & 0x3FF) + data;
    streams[2] = (AT32(frame + 0xC) & 0xFFFF) + data;
    break;
  }
  *frame_out = frame;
  return (fw >> (21 + part)) & 1;
}

/* A frame's vertex stream (one of a part record's PR_A/PR_B). Each vertex
 * is its base word (11-bit z, 10-bit x, 11-bit y, packed) plus a delta: a
 * byte indexing D_8006D614 (bit 0: the next vertex is halfword-coded), or a
 * halfword of small deltas (bit 14: the next vertex's format), or, with
 * bit 15 set, a halfword pair holding the vertex itself. z, x, y
 * accumulate (the asm's a1, a2, a3 or t5, t6, t7). */
typedef struct {
  const u32 *base;
  const u8 *bytes;
  const u16 *halves;
  u32 fmt; /* non-zero: the next vertex is halfword-coded */
  u32 z, x, y;
} Stream;

static inline void StreamInit(Stream *st, const u32 *rec, int which, u32 fmt) {
  st->base = HPTR(const u32, rec[which]);
  st->bytes = HPTR(const u8, rec[which + 1]);
  st->halves = HPTR(const u16, rec[which + 2]);
  st->fmt = fmt;
  st->z = st->x = st->y = 0;
}

static inline void AddPacked(Stream *st, u32 w) {
  st->z += SRA(w, 21);
  st->x -= FIELD(w, 11, 21);
  st->y -= FIELD(w, 22, 21);
}

/* The A stream's decoding (asm 800244E0.., 80024580..; 800246BC..,
 * 800247D8..): the base word and the delta summed, then unpacked. */
static inline void DecodeA(Stream *st) {
  const u32 *base = st->base++; /* an absolute vertex doesn't read it */
  if (!st->fmt) {
    u32 b = *st->bytes++;
    st->fmt = b & 1;
    AddPacked(st, WORD_AT(D_8006D614, (b << 1) & 0x1FC) + *base);
  } else {
    u32 h = (u32)(s32)(s16)*st->halves++;
    st->fmt = h & 0x4000;
    if ((s32)h >= 0) {
      st->z += FIELD(h, 18, 25);
      st->x -= FIELD(h, 23, 25);
      st->y -= FIELD(h, 28, 25);
      AddPacked(st, *base);
    } else {
      u32 h2 = *st->halves++;
      st->z = FIELD(h, 18, 21);
      st->x = 0u - SRA((h2 << 12) | (h << 28), 21);
      st->y = 0u - FIELD(h2, 22, 21);
    }
  }
}

/* The B stream (asm 80024718.., 80024848.., 80024960..): the same, but the
 * halfword-coded vertices add the base word first. */
static inline void DecodeB(Stream *st) {
  u32 base = *st->base++;
  if (!st->fmt) {
    u32 b = *st->bytes++;
    AddPacked(st, WORD_AT(D_8006D614, (b << 1) & 0x1FC) + base);
    st->fmt = b & 1;
  } else {
    u32 h = (u32)(s32)(s16)st->halves[0];
    AddPacked(st, base);
    st->fmt = h & 0x4000;
    if ((s32)h >= 0) {
      st->halves += 1;
      st->z += FIELD(h, 18, 25);
      st->x -= FIELD(h, 23, 25);
      st->y -= FIELD(h, 28, 25);
    } else {
      u32 h2 = st->halves[1];
      st->halves += 2;
      st->z = FIELD(h, 18, 21);
      st->x = 0u - SRA((h2 << 12) | (h << 28), 21);
      st->y = 0u - FIELD(h2, 22, 21);
    }
  }
}

/* asm 80024404-80024A50: every part's vertices. Each step transforms the
 * vertex in V0 (decoded by the step before), decodes the next into V0 and
 * stores the transformed one: screen position to the scratchpad, SZ (and,
 * with FLOOR, the untransformed VXY0/VZ0) to the vertex records. The first
 * step of the first run transforms whatever V0 held; the run ends at FP ==
 * STOP, and the asm then runs again from the same slot to the end
 * (`mflo`, the end kept in LO), so the first slot is written twice. */
static void PartVertices(u32 floor) {
  u32 *rec = (u32 *)(WORK + W_PARTS);
  u32 info = rec[PR_INFO];
  u32 fp = SCRATCH_BASE, sp = WORK + W_VERTS;

  do {
    Stream a, b;
    u32 blend, last, stop;
    CTC2(rec[PR_TR], 5);
    CTC2(rec[PR_TR + 1], 6);
    CTC2(rec[PR_TR + 2], 7);
    if ((s32)info < 0)
      SetMatrix(rec + PR_MATRIX);
    last = SCRATCH_BASE + ((info >> 14) & 0x3FF) - 4;
    stop = fp + 4;
    StreamInit(&a, rec, PR_A, info & 0x1000);
    blend = info & 0xFFF;
    if (!blend) {
      for (;;) {
        do {
          gte_rtps();
          DecodeA(&a);
          sp += 0x10;
          fp += 4;
          AT32(sp - 0x10) = MFC2(19);
          if (floor) {
            AT32(sp - 0xC) = MFC2(0);
            AT32(sp - 0x8) = MFC2(1);
          }
          MTC2((a.y << 16) + a.x, 0);
          MTC2(a.z, 1);
          AT32(fp - 4) = MFC2(14);
        } while (fp != stop);
        if (last == stop)
          break;
        stop = last;
        sp -= 0x10;
        fp -= 4;
      }
    } else {
      StreamInit(&b, rec, PR_B, info & 0x2000);
      for (;;) {
        do {
          u32 sxy, sz, m1, m2, m3;
          gte_rtps();
          DecodeA(&a);
          sp += 0x10;
          fp += 4;
          DecodeB(&b);
          MTC2(blend, 8);
          MTC2(a.z, 9);
          MTC2(a.x, 10);
          MTC2(a.y, 11);
          CTC2(b.z, 21);
          CTC2(b.x, 22);
          CTC2(b.y, 23);
          sxy = MFC2(14);
          sz = MFC2(19);
          gte_intpl();
          AT32(fp - 4) = sxy;
          AT32(sp - 0x10) = sz;
          if (floor) {
            AT32(sp - 0xC) = MFC2(0);
            AT32(sp - 0x8) = MFC2(1);
          }
          m1 = MFC2(25);
          m2 = MFC2(26);
          m3 = MFC2(27);
          MTC2(m1, 1);
          MTC2(m2 + (m3 << 16), 0);
        } while (fp != stop);
        if (last == stop)
          break;
        stop = last;
        sp -= 0x10;
        fp -= 4;
      }
    }
    /* asm 80024A14: the part's last vertex */
    gte_rtps();
    sp += 0x10;
    fp += 4;
    rec += PR_WORDS;
    info = rec[PR_INFO];
    AT32(fp - 4) = MFC2(14);
    AT32(sp - 0x10) = MFC2(19);
    if (floor) {
      AT32(sp - 0xC) = MFC2(0);
      AT32(sp - 0x8) = MFC2(1);
    }
  } while (info != 0);
}

/* asm 80024A5C-80024B5C: each vertex's height above the floor, in place of
 * its VXY0: Spyro's z less the floor distance, less the vertex's y in
 * Spyro's own frame (m_RotationMatrix, translated by the part's joint).
 * The loop pipelines, loading the next vertex before storing, so V0 ends
 * holding the vertex after the last. */
static void FloorHeights(const u8 *hdr, u32 floor) {
  const u8 *spyro = (const u8 *)g_Spyro;
  const u32 level = AT32(spyro + SP_POS + 8) - floor;
  u32 g = WORK + W_VERTS;
  int part;

  SetMatrix((const u32 *)(spyro + SP_MATRIX));
  for (part = 0; part < 3; part++) {
    const u32 end = WORK + W_VERTS + ((u32)hdr[AN_PART_END + part] << 4);
    const u32 *joint = (const u32 *)(WORK + W_JOINTS + part * 12);
    u32 m1, m2, m3;
    MTC2(0u - joint[1], 9);
    MTC2(0u - joint[2], 10);
    MTC2(joint[0], 11);
    gte_mvmva_ir();
    m1 = MFC2(25);
    m2 = MFC2(26);
    m3 = MFC2(27);
    CTC2(m1, 5);
    CTC2(m2, 6);
    CTC2(m3, 7);
    MTC2(AT32(g + 4), 0);
    MTC2(AT32(g + 8), 1);
    g += 0x10;
    for (;;) {
      u32 nxy, nz, y;
      int more;
      gte_mvmva_v0_tr();
      nxy = AT32(g + 4);
      nz = AT32(g + 8);
      y = MFC2(26);
      MTC2(nxy, 0);
      MTC2(nz, 1);
      AT32(g - 0xC) = level - y;
      more = g != end;
      g += 0x10;
      if (!more)
        break;
    }
    g -= 0x10;
  }
}

/* asm 80024B7C-80024C2C: the colours blended towards the colour filter
 * (Fairy Kiss) into W_COLOURS, by INTPL; pipelined like the vertices, so
 * IR1-3 end holding the colour after the last. */
static u32 FilterColours(const u8 *hdr, u32 colours, u32 filter) {
  const u32 *c = HPTR(const u32, colours);
  const u32 *end;
  u32 out = WORK + W_COLOURS;
  u32 w;

  MTC2((filter >> 24) << 4, 8);
  CTC2((filter << 4) & 0xFF0, 21);
  CTC2((filter >> 4) & 0xFF0, 22);
  CTC2((filter >> 12) & 0xFF0, 23);
  w = *c++;
  MTC2((w << 4) & 0xFF0, 9);
  MTC2((w >> 4) & 0xFF0, 10);
  MTC2((w >> 12) & 0xFF0, 11);
  end = c + *(const u16 *)(hdr + AN_COLOUR_COUNT);
  do {
    u32 rgb;
    gte_intpl();
    w = *c++;
    rgb = MFC2(22);
    AT32(out) = rgb;
    MTC2((w << 4) & 0xFF0, 9);
    MTC2((w >> 4) & 0xFF0, 10);
    MTC2((w >> 12) & 0xFF0, 11);
    out += 4;
  } while (c != end);
  return WORK + W_COLOURS;
}

/* The face loops' state: the primitive buffer, the colours, the depth
 * buckets in use (lowest, highest) and the depth parameters (the asm's
 * s6, s7, from the GTE's BBK and RBK). */
typedef struct {
  u32 prim;
  const u8 *colours;
  u32 lo, hi;
  u32 bias, shift;
} Faces;

/* A face's depth bucket from its summed SZ (with the face's bias, its
 * colour word's top 4 bits), or 0 if it's behind. */
static inline u32 Bucket(const Faces *f, u32 sum, u32 w1) {
  sum += (u32)(SRA(w1, 28) << 2) << (f->shift & 31);
  if ((s32)sum <= 0)
    return 0;
  return ((sum >> (f->shift & 31)) << 3) + WORK + W_BUCKETS;
}

/* A new primitive at the end of its bucket's chain: the first in a bucket
 * also widens the range of buckets in use. TAIL is the bucket's last word,
 * read before the primitive was written (as the asm does). */
static inline void LinkPrim(Faces *f, u32 bucket, u32 tail, u32 size) {
  AT32(bucket) = f->prim;
  if (tail) {
    u32 t = MIPS_KUSEG(tail);
    AT16(t) = (u16)f->prim;
    AT8(t + 2) = (u8)(f->prim >> 16);
  } else {
    if ((s32)(f->lo - bucket) > 0)
      f->lo = bucket;
    AT32(bucket + 4) = f->prim;
    if ((s32)(f->hi - bucket) < 0)
      f->hi = bucket;
  }
  f->prim += size;
}

/* SZ of a vertex: the vertex records by byte offset index*4 (path A). */
#define SZ_A(i4) ((u32)AT16(WORK + W_VERTS + ((i4) << 2)))

/* asm 80024F3C-80025044: a POLY_GT3, vertices I0 I1 I2 (byte offsets of
 * index * 4), colours C0 C1 (offsets into the colour table) and the face's
 * C2. FACE is past the face; its last three words are the texture words.
 * FROM_QUAD: the quad's other half, whose first vertex is the quad's
 * fourth, takes its texture coordinates from the third word's top half. */
static void EmitGT3(Faces *f, u32 face, u32 w1, u32 i0, u32 i1, u32 i2, u32 c0, u32 c1,
                    int from_quad) {
  const u32 prim = f->prim;
  u32 col0, col1, col2, z0, z1, uv2, bucket, tail;
  col0 = WORD_AT(f->colours, c0);
  col1 = WORD_AT(f->colours, c1);
  col2 = WORD_AT(f->colours, (w1 << 1) & 0x7FC);
  col0 = (col0 & 0xFFFFFF) + 0x34000000u;
  z0 = SZ_A(i0);
  z1 = SZ_A(i1);
  bucket = Bucket(f, z0 + (z0 >> 1) - f->bias + z1 + (z1 >> 1) + SZ_A(i2), w1);
  if (!bucket)
    return;
  tail = AT32(bucket);
  uv2 = AT32(face - 4);
  AT32(prim + 0x10) = col1;
  AT32(prim + 0x1C) = col2;
  AT32(prim + 0x4) = col0 + ((w1 & 1) << 25);
  AT32(prim + 0xC) = AT32(face - 0xC);
  if (from_quad)
    AT16(prim + 0xC) = (u16)(uv2 >> 16);
  AT32(prim) = TAG_GT3;
  AT32(prim + 0x18) = AT32(face - 0x8);
  AT32(prim + 0x24) = uv2;
  LinkPrim(f, bucket, tail, 0x28);
}

/* asm 80024C70-80025044: the faces, when Spyro is clear of the floor.
 * A face is a word of vertex indices (bit 31: a quad, bit 0: two-sided), a
 * word of colour indices (top 4 bits: depth bias, bit 0: semi-transparent),
 * a quad's fourth vertex and colour, and three texture words. The
 * primitive's positions are written before the face is culled, as the asm
 * does (stray stores past the last primitive). */
static void DrawFaces(Faces *f, u32 face, u32 end) {
  const u32 *scr = (const u32 *)SCRATCH_BASE;

  while (face != end) {
    const u32 w0 = AT32(face), w1 = AT32(face + 4);
    const u32 prim = f->prim;
    const u32 i0 = (w0 >> 20) & 0x7FC, i1 = (w0 >> 11) & 0x7FC, i2 = (w0 >> 2) & 0x7FC;
    const u32 c0 = (w1 >> 17) & 0x7FC, c1 = (w1 >> 8) & 0x7FC;
    u32 xy0 = WORD_AT(scr, i0), xy1 = WORD_AT(scr, i1), xy2 = WORD_AT(scr, i2);

    if ((s32)w0 >= 0) {
      /* asm 80024EA8: a triangle */
      MTC2(xy0, 12);
      MTC2(xy1, 13);
      MTC2(xy2, 14);
      AT32(prim + 0x8) = xy0;
      AT32(prim + 0x14) = xy1;
      gte_nclip();
      AT32(prim + 0x20) = xy2;
      face += 0x14;
      if (!(w0 & 1) && (s32)MFC2(24) <= 0)
        continue;
      EmitGT3(f, face, w1, i0, i1, i2, c0, c1, 0);
    } else {
      /* asm 80024C9C: a quad, drawn whole or as the half facing the camera */
      const u32 w2 = AT32(face + 8);
      const u32 i3 = w2 & 0x7FC, c3 = (w2 >> 9) & 0x7FC;
      const u32 xy3 = WORD_AT(scr, i3);
      u32 col0, sum, bucket, tail, uv2;
      MTC2(xy0, 12);
      MTC2(xy1, 13);
      MTC2(xy2, 14);
      gte_nclip();
      AT32(prim + 0x8) = xy0;
      AT32(prim + 0x14) = xy1;
      AT32(prim + 0x20) = xy2;
      AT32(prim + 0x2C) = xy3;
      face += 0x18;
      if (!(w0 & 1)) {
        s32 n012 = (s32)MFC2(24), n312;
        MTC2(xy3, 12);
        gte_nclip();
        n312 = (s32)MFC2(24);
        if (n012 >= 0) {
          if (n312 <= 0)
            continue;
          AT32(prim + 0x8) = xy3;
          EmitGT3(f, face, w1, i3, i1, i2, c3, c1, 1);
          continue;
        }
        if (n312 <= 0) {
          EmitGT3(f, face, w1, i0, i1, i2, c0, c1, 0);
          continue;
        }
      }
      /* asm 80024D98: POLY_GT4 */
      col0 = WORD_AT(f->colours, c0);
      sum = SZ_A(i0) - f->bias + SZ_A(i1) + SZ_A(i2);
      bucket = Bucket(f, sum + SZ_A(i3), w1);
      if (!bucket)
        continue;
      tail = AT32(bucket);
      uv2 = AT32(face - 4);
      AT32(prim + 0x10) = WORD_AT(f->colours, c1);
      AT32(prim + 0x1C) = WORD_AT(f->colours, (w1 << 1) & 0x7FC);
      AT32(prim + 0x28) = WORD_AT(f->colours, c3);
      AT32(prim + 0x4) = (col0 & 0xFFFFFF) + 0x3C000000u + ((w1 & 1) << 25);
      AT32(prim) = TAG_GT4;
      AT32(prim + 0xC) = AT32(face - 0xC);
      AT32(prim + 0x18) = AT32(face - 0x8);
      AT32(prim + 0x24) = uv2;
      AT32(prim + 0x30) = uv2 >> 16;
      LinkPrim(f, bucket, tail, 0x34);
    }
  }
}

/* asm 80025310..80025430 (and its two copies): the point where edge P-Q
 * crosses the floor (P's height HP, Q's HQ, opposite signs), its colour,
 * screen position and texture coordinates interpolated by INTPL at
 * HP / (HP - HQ), appended at OUT (colour, position, uv halfword). */
static u32 ClipEdge(u32 out, u32 hp, u32 hq, u32 cp, u32 cq, u32 xyp, u32 xyq, u32 uvp,
                    u32 uvq) {
  u32 hi, lo, m1, m2, m3;
  mips2c_div(hp << 12, hp - hq, &hi, &lo);
  (void)hi;
  MTC2(cp & 0xFF, 9);
  CTC2(cq & 0xFF, 21);
  MTC2((cp >> 8) & 0xFF, 10);
  CTC2((cq >> 8) & 0xFF, 22);
  MTC2((cp >> 16) & 0xFF, 11);
  CTC2((cq >> 16) & 0xFF, 23);
  MTC2(lo, 8);
  gte_intpl();
  m3 = MFC2(27);
  m2 = MFC2(26);
  m1 = MFC2(25);
  AT32(out + 0x4) = (((m3 << 8) + m2) << 8) + m1;
  MTC2(SRA(xyp << 16, 16), 9);
  CTC2(SRA(xyq << 16, 16), 21);
  MTC2(SRA(xyp, 16), 10);
  CTC2(SRA(xyq, 16), 22);
  gte_intpl();
  m2 = MFC2(26);
  m1 = MFC2(25);
  AT32(out + 0x8) = (m1 & 0xFFFF) + (m2 << 16);
  MTC2(uvp & 0xFF, 9);
  CTC2(uvq & 0xFF, 21);
  MTC2((uvp >> 8) & 0xFF, 10);
  CTC2((uvq >> 8) & 0xFF, 22);
  gte_intpl();
  m2 = MFC2(26);
  m1 = MFC2(25);
  AT16(out + 0xC) = (u16)(m1 + (m2 << 8));
  return out + 0xC;
}

/* asm 80025140-8002579C: one triangle when Spyro is partly under the floor.
 * V0 V1 V2: vertex record offsets (index * 16); C0 C1 C2: colour offsets;
 * UV0-2: texture words. FLAGS: bit 31, the quad's first half (it faces the
 * other way), bit 0, two-sided. A triangle wholly below the floor is
 * dropped, one wholly above is a POLY_GT3, and one crossing it is clipped
 * at the floor to a GT3 or GT4 (the points above, and where the edges
 * cross). */
static void ClipTriangle(Faces *f, u32 flags, u32 w1, u32 v0, u32 v1, u32 v2, u32 c0, u32 c1,
                         u32 c2, u32 uv0, u32 uv1, u32 uv2) {
  const u32 *scr = (const u32 *)SCRATCH_BASE;
  const u32 r0 = WORK + W_VERTS + v0, r1 = WORK + W_VERTS + v1, r2 = WORK + W_VERTS + v2;
  const u32 prim = f->prim;
  const u32 h0 = AT32(r0 + 4), h1 = AT32(r1 + 4), h2 = AT32(r2 + 4);
  u32 sum, z1, z2, xy0, xy1, xy2, col0, col1, col2, bucket, tail, out;

  if ((s32)(h0 & h1 & h2) < 0)
    return;
  sum = AT32(r0);
  z1 = AT32(r1);
  z2 = AT32(r2);
  xy0 = WORD_AT(scr, v0 >> 2);
  xy1 = WORD_AT(scr, v1 >> 2);
  xy2 = WORD_AT(scr, v2 >> 2);
  MTC2(xy0, 12);
  MTC2(xy1, 13);
  MTC2(xy2, 14);
  AT32(prim + 0x8) = xy0;
  AT32(prim + 0x14) = xy1;
  gte_nclip();
  AT32(prim + 0x20) = xy2;
  sum += sum >> 1;
  sum += z1 >> 1;
  if (!(flags & 1)) {
    u32 n = MFC2(24);
    if ((s32)flags < 0)
      n = 0u - n;
    if ((s32)n <= 0)
      return;
  }
  col0 = WORD_AT(f->colours, c0);
  col1 = WORD_AT(f->colours, c1);
  col2 = WORD_AT(f->colours, c2);
  if ((s32)(h0 | h1 | h2) >= 0) {
    /* wholly above */
    bucket = Bucket(f, sum - f->bias + z1 + z2, w1);
    if (!bucket)
      return;
    tail = AT32(bucket);
    AT32(prim + 0x4) = ((col0 << 8) >> 8) + 0x34000000u;
    AT32(prim + 0x10) = col1;
    AT32(prim + 0x1C) = col2;
    AT32(prim + 0xC) = uv0;
    AT32(prim + 0x18) = uv1;
    AT32(prim + 0x24) = uv2;
    AT32(prim) = TAG_GT3;
    LinkPrim(f, bucket, tail, 0x28);
    return;
  }

  /* asm 800252C0: crossing the floor */
  AT32(prim + 0xC) = uv0;
  AT32(prim + 0x18) = uv1;
  bucket = Bucket(f, sum - f->bias + z1 + z2, w1);
  if (!bucket)
    return;
  out = prim;
  if ((s32)h0 >= 0) {
    AT32(out + 0x4) = col0;
    AT32(out + 0x8) = xy0;
    AT16(out + 0xC) = (u16)uv0;
    out += 0xC;
  }
  if ((s32)(h0 ^ h1) < 0)
    out = ClipEdge(out, h0, h1, col0, col1, xy0, xy1, uv0, uv1);
  if ((s32)h1 >= 0) {
    AT32(out + 0x4) = col1;
    AT32(out + 0x8) = xy1;
    AT16(out + 0xC) = (u16)uv1;
    out += 0xC;
  }
  if ((s32)(h1 ^ h2) < 0)
    out = ClipEdge(out, h1, h2, col1, col2, xy1, xy2, uv1, uv2);
  if ((s32)h2 >= 0) {
    AT32(out + 0x4) = col2;
    AT32(out + 0x8) = xy2;
    AT16(out + 0xC) = (u16)uv2;
    out += 0xC;
  }
  if ((s32)(h2 ^ h0) < 0)
    out = ClipEdge(out, h2, h0, col2, col0, xy2, xy0, uv2, uv0);
  tail = AT32(bucket);
  if (out - prim - 0x30 != 0) {
    /* three points: POLY_GT3 */
    AT8(prim + 7) = 0x34;
    AT32(prim) = TAG_GT3;
    LinkPrim(f, bucket, tail, 0x28);
  } else {
    /* four: POLY_GT4, the last two points swapped into strip order */
    u32 a = AT32(prim + 0x1C), b = AT32(prim + 0x20), c = AT32(prim + 0x24);
    u32 d = AT32(prim + 0x28), e = AT32(prim + 0x2C), g = AT32(prim + 0x30);
    AT32(prim + 0x1C) = d;
    AT32(prim + 0x20) = e;
    AT32(prim + 0x24) = g;
    AT32(prim + 0x28) = a;
    AT32(prim + 0x2C) = b;
    AT32(prim + 0x30) = c;
    AT8(prim + 7) = 0x3C;
    AT32(prim) = TAG_GT4;
    LinkPrim(f, bucket, tail, 0x34);
  }
}

/* asm 80025048-8002579C: the faces, when Spyro is partly under the floor
 * (the vertex records hold each vertex's height). The same face words as
 * DrawFaces, indices pre-multiplied by 16; a quad is its two halves. */
static void DrawFacesClipped(Faces *f, u32 face, u32 end) {
  while (face != end) {
    const u32 w0 = AT32(face), w1 = AT32(face + 4);
    u32 uv0, uv1, uv2;
    if ((s32)w0 < 0) {
      uv0 = AT32(face + 0xC);
      uv1 = AT32(face + 0x10);
      uv2 = AT32(face + 0x14);
      face += 0x18;
    } else {
      uv0 = AT32(face + 0x8);
      uv1 = AT32(face + 0xC);
      uv2 = AT32(face + 0x10);
      face += 0x14;
    }
    ClipTriangle(f, w0, w1, (w0 >> 18) & 0xFF0, (w0 >> 9) & 0xFF0, w0 & 0xFF0,
                 (w1 >> 17) & 0x7FC, (w1 >> 8) & 0x7FC, (w1 << 1) & 0x7FC, uv0, uv1, uv2);
    if ((s32)w0 < 0) {
      /* asm 800250B8: the other half, the fourth vertex first */
      const u32 w2 = AT32(face - 0x10);
      ClipTriangle(f, (w0 << 1) >> 1, w1, (w2 << 2) & 0xFF0, (w0 >> 9) & 0xFF0, w0 & 0xFF0,
                   (w2 >> 9) & 0x7FC, (w1 >> 8) & 0x7FC, (w1 << 1) & 0x7FC,
                   ((uv0 >> 16) << 16) | (uv2 >> 16), uv1, uv2);
    }
  }
}

/* asm 800257A0-800258AC: the used buckets, highest first, into the world
 * OT: STEP buckets per OT slot from the slot for Spyro's depth (GBK) down,
 * each slot's chain appended to what's there; the buckets are cleared. */
static void LinkBuckets(const Faces *f) {
  const u32 shift = CFC2(13);
  const u32 ot = *(u32 *)g_WorldOT;
  const u32 last = f->lo - 8;
  s32 above;
  u32 slot, skip, step, at, a0;

  if ((s32)(f->hi - f->lo) < 0)
    return; /* nothing drawn */
  slot = (CFC2(14) << 3) + ((0x20u << (shift & 31)) - 8);
  above = (s32)(WORK + 0x800 - f->hi);
  skip = above >= 0 ? (u32)above << (shift & 31) : 0;
  slot -= (skip >> 8) << 3;
  if ((s32)slot < 0)
    slot = 0;
  step = 0x100u >> (shift & 31);
  at = slot + ot;
  a0 = f->hi;
  for (;;) {
    u32 tail = AT32(MIPS_KUSEG(at));
    u32 stop = a0 - step;
    if ((s32)(last - stop) > 0)
      stop = last;
    if (!tail) {
      /* an empty slot starts with the first non-empty bucket */
      while (a0 != stop) {
        u32 head = AT32(a0 + 4);
        a0 -= 8;
        if (head) {
          tail = AT32(a0 + 8);
          AT32(MIPS_KUSEG(at) + 4) = head;
          AT32(a0 + 0xC) = 0;
          AT32(a0 + 0x8) = 0;
          break;
        }
      }
    }
    while (a0 != stop) {
      u32 head = AT32(a0 + 4), t;
      a0 -= 8;
      if (!head)
        continue;
      t = MIPS_KUSEG(tail);
      tail = AT32(a0 + 8);
      AT16(t) = (u16)head;
      AT8(t + 2) = (u8)(head >> 16);
      AT32(a0 + 0x8) = 0;
      AT32(a0 + 0xC) = 0;
    }
    AT32(MIPS_KUSEG(at)) = tail;
    if (stop == last)
      break;
    if (at != ot)
      at -= 8;
  }
  *(u32 *)D_800757B0 = f->prim;
}

static u32 SpyroDraw(u32 a0_, u32 a1_, u32 a2_, u32 a3_) {
  const u8 *spyro = (const u8 *)g_Spyro;
  const u32 *models = HPTR(const u32, *(u32 *)g_Models) + 0x38 / 4;
  const u32 *cam = (const u32 *)g_Camera;
  u32 *const rec0 = (u32 *)(WORK + W_PARTS);
  u32 *rec = rec0;
  u32 shift_a = 0, shift_b = 0, body_hdr = 0, body_a = 0, body_b = 0, blend0 = 0;
  u32 head[3], tail[3];
  u32 x, y, z, sort, far, m[5], saved[5], angles, floor, filter, colours;
  const u8 *hdr;
  Faces f;
  int part;
  (void)a0_;
  (void)a1_;
  (void)a2_;
  (void)a3_;

  hand_spill_regs();

  /* asm 80023B2C-80023E08: the part records */
  for (part = 0; part < 3; part++) {
    u32 anim = models[spyro[SP_ANIM + 2 * part]], frame, info;
    u32 blend = spyro[SP_PROGRESS + part];
    info = PartStreams(rec + PR_A, anim, spyro[SP_FRAME + 2 * part], part, &frame) << 12;
    info += blend << 8;
    info += (u32)HPTR(const u8, anim)[AN_PART_END + part] << 16;
    if (part == 0) {
      body_hdr = anim;
      body_a = frame;
      blend0 = blend;
      shift_a = HPTR(const u8, anim)[AN_DEPTH_SHIFT];
    }
    if (blend) {
      u32 anim_b = models[spyro[SP_ANIM + 2 * part + 1]];
      info += PartStreams(rec + PR_B, anim_b, spyro[SP_FRAME + 2 * part + 1], part, &frame) << 13;
      if (part == 0) {
        body_b = frame;
        shift_b = HPTR(const u8, anim_b)[AN_DEPTH_SHIFT];
      }
    }
    rec[PR_INFO] = info;
    if (part < 2)
      rec += PR_WORDS;
  }
  rec[PR_WORDS] = 0;

  /* asm 80023E0C: the head's and tail's joints, from the body's frame
   * (words 4 and 5), blended like its vertices */
  {
    u32 jh = AT32(body_a + 0x10), jt = AT32(body_a + 0x14);
    if (!blend0) {
      tail[2] = FIELD(jt, 22, 21);
      tail[1] = FIELD(jt, 11, 21);
      tail[0] = SRA(jt, 21);
      head[2] = FIELD(jh, 22, 21);
      head[1] = FIELD(jh, 11, 21);
      head[0] = SRA(jh, 21);
    } else {
      u32 bh = AT32(body_b + 0x10), bt = AT32(body_b + 0x14);
      MTC2(blend0 << 8, 8);
      MTC2(SRA(jh, 21), 9);
      MTC2(FIELD(jh, 11, 21), 10);
      MTC2(FIELD(jh, 22, 21), 11);
      CTC2(SRA(bh, 21), 21);
      CTC2(FIELD(bh, 11, 21), 22);
      CTC2(FIELD(bh, 22, 21), 23);
      gte_intpl();
      MTC2(SRA(jt, 21), 9);
      MTC2(FIELD(jt, 11, 21), 10);
      MTC2(FIELD(jt, 22, 21), 11);
      CTC2(SRA(bt, 21), 21);
      CTC2(FIELD(bt, 11, 21), 22);
      CTC2(FIELD(bt, 22, 21), 23);
      head[0] = MFC2(25);
      head[1] = MFC2(26);
      head[2] = MFC2(27);
      gte_intpl();
      tail[0] = MFC2(25);
      tail[1] = MFC2(26);
      tail[2] = MFC2(27);
    }
  }

  /* asm 80023EE0: Spyro's position by the camera; give up if he's behind,
   * too far or off the sides */
  x = AT32(spyro + SP_POS) - cam[10];
  y = cam[11] - AT32(spyro + SP_POS + 4);
  z = cam[12] - AT32(spyro + SP_POS + 8);
  SetMatrix(cam);
  CTC2(0, 5);
  CTC2(0, 6);
  CTC2(0, 7);
  MTC2(x, 11);
  MTC2(y, 9);
  MTC2(z, 10);
  gte_mvmva_ir();
  z = MFC2(27);
  y = MFC2(26);
  x = MFC2(25);
  if ((s32)(z - 0x4000) >= 0)
    return 0;
  far = z + 0x800;
  if ((s32)(far - x) <= 0 || (s32)(far + x) <= 0 || (s32)(far - y) <= 0 ||
      (s32)(far + y) <= 0)
    return 0;

  /* asm 80023F8C: the depth parameters, kept in the GTE's back colour:
   * RBK the larger depth shift, GBK Spyro's OT slot, BBK his depth less
   * a margin */
  sort = SRA(z, 7) - spyro[SP_SORT_DEPTH];
  if ((s32)sort < 0)
    sort = 0;
  if ((s32)(shift_b - shift_a) < 0)
    shift_b = shift_a;
  far = z - (0x200u << (shift_b & 31));
  if ((s32)far < 0)
    far = 0;
  CTC2(shift_b, 13);
  CTC2(sort, 14);
  CTC2(far, 15);

  {
    u32 *joints = (u32 *)(WORK + W_JOINTS);
    joints[0] = joints[1] = joints[2] = 0;
    joints[3] = head[0];
    joints[4] = head[1];
    joints[5] = head[2];
    joints[6] = tail[0];
    joints[7] = tail[1];
    joints[8] = tail[2];
  }
  CTC2(x, 5);
  CTC2(y, 6);
  CTC2(z, 7);
  rec0[PR_TR] = x;
  rec0[PR_TR + 1] = y;
  rec0[PR_TR + 2] = z;
  m[0] = CFC2(0);
  m[1] = CFC2(1);
  m[2] = CFC2(2);
  m[3] = CFC2(3);
  m[4] = CFC2(4);

  /* asm 80024030: the body's matrix (RotateMatrix returns to D_80024054) */
  RotateMatrix(m, AT32(spyro + SP_ROTATION));
  for (part = 0; part < 5; part++)
    rec0[PR_MATRIX + part] = m[part];
  /* the head's and tail's positions: their joints by the body's matrix */
  MTC2((0u - head[1]) + ((0u - head[2]) << 16), 0);
  MTC2(head[0], 1);
  gte_rtps();
  head[0] = MFC2(9);
  MTC2((0u - tail[1]) + ((0u - tail[2]) << 16), 0);
  MTC2(tail[0], 1);
  head[1] = MFC2(10);
  head[2] = MFC2(11);
  gte_rtps();
  rec0[PR_WORDS + PR_TR] = head[0];
  rec0[PR_WORDS + PR_TR + 1] = head[1];
  rec0[PR_WORDS + PR_TR + 2] = head[2];
  rec0[2 * PR_WORDS + PR_TR] = MFC2(9);
  rec0[2 * PR_WORDS + PR_TR + 1] = MFC2(10);
  rec0[2 * PR_WORDS + PR_TR + 2] = MFC2(11);
  for (part = 0; part < 5; part++)
    saved[part] = m[part];

  /* the head's matrix (D_800240F8), which the flame also uses */
  angles = AT32(spyro + SP_ROTATION + 4);
  if ((s32)angles > 0)
    RotateMatrix(m, angles);
  if (((const u8 *)g_SpyroFlame)[0x9A]) {
    u32 *fm = (u32 *)(g_SpyroFlame + 0xB8);
    for (part = 0; part < 5; part++)
      fm[part] = m[part];
  }
  if (angles) {
    for (part = 0; part < 5; part++) {
      rec0[PR_WORDS + PR_MATRIX + part] = m[part];
      m[part] = saved[part];
    }
    AT8((u32)(uintptr_t)(rec0 + PR_WORDS) + 3) = 0x80;
    SetMatrix(saved);
    for (part = 0; part < 5; part++)
      rec0[2 * PR_WORDS + PR_MATRIX + part] = m[part];
    AT8((u32)(uintptr_t)(rec0 + 2 * PR_WORDS) + 3) = 0x80;
  }
  /* the tail's (D_800241A8) */
  angles = AT32(spyro + SP_ROTATION + 8);
  if ((s32)angles > 0) {
    RotateMatrix(m, angles);
    for (part = 0; part < 5; part++)
      rec0[2 * PR_WORDS + PR_MATRIX + part] = m[part];
    AT8((u32)(uintptr_t)(rec0 + 2 * PR_WORDS) + 3) = 0x80;
    SetMatrix(saved);
  }

  /* asm 80024404: the vertices */
  floor = AT32(spyro + SP_FLOOR_DIST);
  PartVertices(floor);
  hdr = HPTR(const u8, body_hdr);
  if (floor)
    FloorHeights(hdr, floor);

  /* asm 80024B60: the colours */
  filter = AT32(spyro + SP_COLOUR_FILTER);
  colours = AT32(hdr + AN_COLOURS);
  if (filter >> 24)
    colours = FilterColours(hdr, colours, filter);

  /* asm 80024C30: the faces */
  f.lo = WORK + W_BUCKETS_END;
  f.hi = WORK + W_BUCKETS;
  f.bias = CFC2(15) << 2;
  f.shift = CFC2(13) + 4;
  f.prim = *(u32 *)D_800757B0;
  f.colours = HPTR(const u8, colours);
  {
    u32 faces = AT32(hdr + AN_FACES);
    const u8 *list = HPTR(const u8, faces);
    u32 start = (u32)(uintptr_t)list + 4;
    u32 end = start + *(const u32 *)list;
    if (!floor)
      DrawFaces(&f, start, end);
    else
      DrawFacesClipped(&f, start, end);
  }
  LinkBuckets(&f);
  return 0; /* v0 is dead at every caller (tools/oracle/ignore_ranges.txt) */
}

HAND_RENDERER(func_80023AC4, SpyroDraw)
