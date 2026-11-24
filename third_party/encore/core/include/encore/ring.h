/* Per-frame GPU memory: the vertex and uniform rings a renderer writes each batch into.
 *
 * A ring is nslots equal slots, one per frame in flight (two = double-buffered: the GPU reads frame n's slot while
 * the CPU writes frame n + 1's).  enc_ring_alloc bumps the current slot's head (aligned); enc_ring_frame moves to the
 * next slot and starts it empty, so nothing is ever freed one by one.  The caller must not write a slot before the
 * GPU is done with the frame that last used it (on the Vita a notification or sceGxmFinish per frame: the ring does
 * not wait).  An allocation that does not fit in what is left of the slot returns 0 and is counted (`overflows`):
 * the caller drops or splits the batch; nothing wraps into the slot the GPU may be reading.  A frame that wrote
 * nothing keeps its slot, so a GPU thread that runs an empty frame's job without waiting is never overtaken.
 *
 * No system headers, so a game can build it with its own types and flags.
 * From kh1-decomp include/platform/ring.h and host/port/ring.c @ 85befa4a. */
#ifndef ENC_RING_H
#define ENC_RING_H

typedef struct {
    unsigned char *base;    /* nslots x slot bytes */
    unsigned int slot;      /* bytes a frame */
    unsigned int nslots;    /* frames in flight */
    unsigned int cur;       /* the frame slot being written */
    unsigned int head;      /* bytes used in it */
    unsigned int frames;    /* frames ended */
    unsigned int high;      /* the most bytes a frame used */
    unsigned int overflows; /* allocations that did not fit */
} EncRing;

/* a ring over mem (nslots x slot bytes; slot a multiple of the largest alignment asked for) */
void enc_ring_init(EncRing *r, void *mem, unsigned int slot, unsigned int nslots);
/* bytes from the current frame's slot, aligned to align (a power of two); 0 when it does not fit */
void *enc_ring_alloc(EncRing *r, unsigned int bytes, unsigned int align);
/* the frame ends: its use into the high-water mark; when it wrote anything the next slot starts empty */
void enc_ring_frame(EncRing *r);

#endif /* ENC_RING_H */
