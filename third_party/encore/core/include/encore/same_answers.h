/* Same answers: a per-polygon FNV-1a stream, so a renderer or vertex-cache change can show that every polygon it
 * did not mean to change got the same answers as before (core/docs/same_answers.md; tools/fnvcmp.py compares two
 * streams, tools/same_answers_pairs.py runs base and fix over a list of routes).
 *
 * The walk calls, per polygon:
 *
 *     EncSaPoly p;
 *     enc_sa_begin(&p);
 *     enc_sa_u32(&p, n);                         inputs: vertex count, keys, the polygon's packet words ...
 *     for (i = 0; i < n; i++) enc_sa_f32(&p, out[i].x), enc_sa_f32(&p, out[i].y), enc_sa_f32(&p, out[i].w);
 *     enc_sa_end(&p, found ? ENC_SA_HANDLED : 0); outputs above, then the polygon is folded into the stream
 *
 * Off twice over. Without ENC_SAME_ANSWERS defined (a release build, the matching build) every call is an empty
 * macro: nothing is compiled, no header is included and the arguments are only referenced (no unused-variable
 * warnings, no code). With it defined, nothing is written unless the environment variable ENC_SAME_ANSWERS names
 * the output file when the first polygon ends. ENC_SAME_ANSWERS_EVERY (default 1000000) is the checkpoint interval;
 * ENC_SAME_ANSWERS_POLYS=0 writes the checkpoints only (no per-polygon records: smaller, but fnvcmp can then only
 * say where two runs first differ, not which polygons changed).
 *
 * Exactly one .c file of the port defines ENC_SAME_ANSWERS_IMPLEMENTATION before including this header (the walk's
 * file is the natural one); the others include it plainly. One thread writes the stream: a port that walks
 * polygons on two threads runs the second inline for these runs (the order must be deterministic anyway).
 *
 * The stream (little-endian; fnvcmp.py reads it):
 *   header, 16 bytes:  "ENCSAME" 0x01, u32 checkpoint interval, u32 options (bit 0: per-polygon records)
 *   'P' record, 8:     u32 the polygon's hash, u8 'P', u8 flags (bit 0 ENC_SA_HANDLED, bits 1-7 the port's), u16 0
 *   'C' record, 16:    u32 the running hash, u8 'C', u8 0, u16 0, u64 polygons so far (every interval polygons)
 *   'E' record, 16:    as 'C', written once by enc_sa_close (and at exit): a stream without one was cut short
 * The running hash folds in each polygon's hash and flags, so a checkpoint covers every polygon before it.
 *
 * i386 without SSE (a linux32 host build for i686, -mfpmath=387): an expression can stay at x87 long double
 * precision until it is stored, and inlining can drop the rounding a float parameter would have made. Two builds
 * that compute the same float answer can then hash different bits. enc_sa_f32 stores through a volatile float on
 * such a build, so what is hashed is always the rounded float. Learned in spyro-vita (D168: an offset 1/256 pixel
 * apart once `record()` was inlined, intro, vertex 603,817). Hash a float through enc_sa_f32, never by its bits.
 *
 * From spyro-vita's #230/#231 gates (runtime/autopilot/runs/vita/issue-230-1/host/fnv_insert2.py, never committed
 * there) and D168's same-answers check. */
#ifndef ENC_SAME_ANSWERS_H
#define ENC_SAME_ANSWERS_H

#define ENC_SA_HANDLED 0x01u /* the change's subject handled this polygon (found in the cache, drawn natively ...) */

#ifdef ENC_SAME_ANSWERS

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t h;
} EncSaPoly;

#define ENC_SA_FNV_BASIS 2166136261u
#define ENC_SA_FNV_PRIME 16777619u

static inline void enc_sa_begin(EncSaPoly *p) { p->h = ENC_SA_FNV_BASIS; }

static inline void enc_sa_bytes(EncSaPoly *p, const void *data, unsigned n) {
    const unsigned char *b = (const unsigned char *)data;
    uint32_t h = p->h;
    while (n--) {
        h ^= *b++;
        h *= ENC_SA_FNV_PRIME;
    }
    p->h = h;
}

/* little-endian whatever the host, so a big-endian host writes the same stream */
static inline void enc_sa_u32(EncSaPoly *p, uint32_t v) {
    unsigned char b[4];
    b[0] = (unsigned char)v, b[1] = (unsigned char)(v >> 8), b[2] = (unsigned char)(v >> 16),
    b[3] = (unsigned char)(v >> 24);
    enc_sa_bytes(p, b, 4);
}

static inline void enc_sa_i32(EncSaPoly *p, int32_t v) { enc_sa_u32(p, (uint32_t)v); }

static inline void enc_sa_f32(EncSaPoly *p, float v) {
#if defined(__i386__) && !defined(__SSE_MATH__)
    volatile float r = v; /* x87: round to float here, whatever precision v was carried at (D168) */
    float f = r;
#else
    float f = v;
#endif
    uint32_t u;
    memcpy(&u, &f, 4);
    enc_sa_u32(p, u);
}

int enc_sa_on(void);                         /* 1 when the stream is being written (opens it at the first call) */
void enc_sa_end(EncSaPoly *p, unsigned flags); /* the polygon is done: its record, the running hash, a checkpoint */
void enc_sa_close(void);                     /* the end record and close (also run at exit); later calls write nothing */

#ifdef __cplusplus
}
#endif

#ifdef ENC_SAME_ANSWERS_IMPLEMENTATION
#include <stdio.h>
#include <stdlib.h>

static struct {
    FILE *f;
    int state; /* 0 not looked yet, 1 writing, -1 off or closed */
    int polys;
    uint32_t run;
    uint64_t n, every;
} enc_sa_s;

static void enc_sa_put(const unsigned char *b, size_t n) {
    if (fwrite(b, 1, n, enc_sa_s.f) != n) {
        fprintf(stderr, "enc-same-answers: write failed, stream stopped\n");
        fclose(enc_sa_s.f);
        enc_sa_s.f = NULL;
        enc_sa_s.state = -1;
    }
}

static void enc_sa_le(unsigned char *b, uint64_t v, int n) {
    int i;
    for (i = 0; i < n; i++)
        b[i] = (unsigned char)(v >> (8 * i));
}

static void enc_sa_mark(char kind) {
    unsigned char b[16] = {0};
    enc_sa_le(b, enc_sa_s.run, 4);
    b[4] = (unsigned char)kind;
    enc_sa_le(b + 8, enc_sa_s.n, 8);
    enc_sa_put(b, 16);
}

static void enc_sa_atexit(void) { enc_sa_close(); }

int enc_sa_on(void) {
    if (enc_sa_s.state == 0) {
        const char *path = getenv("ENC_SAME_ANSWERS"), *e = getenv("ENC_SAME_ANSWERS_EVERY"),
                   *pp = getenv("ENC_SAME_ANSWERS_POLYS");
        unsigned char h[16] = {'E', 'N', 'C', 'S', 'A', 'M', 'E', 1};
        enc_sa_s.state = -1;
        if (!path || !*path)
            return 0;
        enc_sa_s.every = e && *e ? strtoull(e, NULL, 10) : 1000000u;
        if (enc_sa_s.every == 0 || enc_sa_s.every > 0xFFFFFFFFu)
            enc_sa_s.every = 1000000u;
        enc_sa_s.polys = !(pp && pp[0] == '0');
        enc_sa_s.f = fopen(path, "wb");
        if (!enc_sa_s.f) {
            fprintf(stderr, "enc-same-answers: cannot open %s\n", path);
            return 0;
        }
        setvbuf(enc_sa_s.f, NULL, _IOFBF, 1 << 20);
        enc_sa_s.state = 1;
        enc_sa_s.run = ENC_SA_FNV_BASIS;
        enc_sa_le(h + 8, enc_sa_s.every, 4);
        enc_sa_le(h + 12, (uint64_t)enc_sa_s.polys, 4);
        enc_sa_put(h, 16);
        atexit(enc_sa_atexit);
        fprintf(stderr, "enc-same-answers: writing %s, a checkpoint every %llu polygons%s\n", path,
                (unsigned long long)enc_sa_s.every, enc_sa_s.polys ? ", per-polygon records" : "");
    }
    return enc_sa_s.state == 1;
}

void enc_sa_end(EncSaPoly *p, unsigned flags) {
    unsigned char b[8] = {0};
    EncSaPoly r;
    if (!enc_sa_on())
        return;
    enc_sa_le(b, p->h, 4);
    b[4] = 'P';
    b[5] = (unsigned char)flags;
    if (enc_sa_s.polys)
        enc_sa_put(b, 8);
    r.h = enc_sa_s.run;
    enc_sa_bytes(&r, b, 6); /* the hash and flags, as recorded */
    enc_sa_s.run = r.h;
    if (++enc_sa_s.n % enc_sa_s.every == 0 && enc_sa_s.state == 1) {
        enc_sa_mark('C');
        if (enc_sa_s.f)
            fflush(enc_sa_s.f); /* a run cut short still has every checkpoint up to here */
    }
}

void enc_sa_close(void) {
    if (enc_sa_s.state != 1)
        return;
    enc_sa_mark('E');
    if (enc_sa_s.f) {
        fclose(enc_sa_s.f);
        enc_sa_s.f = NULL;
    }
    enc_sa_s.state = -1;
}
#endif /* ENC_SAME_ANSWERS_IMPLEMENTATION */

#else /* !ENC_SAME_ANSWERS: nothing at all */

typedef struct {
    int unused_;
} EncSaPoly;
#define enc_sa_begin(p) ((void)(p))
#define enc_sa_bytes(p, d, n) ((void)(p), (void)(d), (void)(n))
#define enc_sa_u32(p, v) ((void)(p), (void)(v))
#define enc_sa_i32(p, v) ((void)(p), (void)(v))
#define enc_sa_f32(p, v) ((void)(p), (void)(v))
#define enc_sa_on() 0
#define enc_sa_end(p, flags) ((void)(p), (void)(flags))
#define enc_sa_close() ((void)0)

#endif /* ENC_SAME_ANSWERS */
#endif /* ENC_SAME_ANSWERS_H */
