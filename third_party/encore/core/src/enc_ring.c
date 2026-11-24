/* Per-frame vertex and uniform rings (enc_ring.h).
 * From kh1-decomp host/port/ring.c @ 85befa4a (the frame count is now a parameter). */
#include "../include/encore/ring.h" /* relative: games build this through the compat .c, without -I core/include */

void enc_ring_init(EncRing *r, void *mem, unsigned int slot, unsigned int nslots) {
    r->base = (unsigned char *)mem;
    r->slot = slot;
    r->nslots = nslots;
    r->cur = 0;
    r->head = 0;
    r->frames = 0;
    r->high = 0;
    r->overflows = 0;
}

void *enc_ring_alloc(EncRing *r, unsigned int bytes, unsigned int align) {
    unsigned int at = (r->head + align - 1) & ~(align - 1);
    if (at > r->slot || bytes > r->slot - at) {
        r->overflows++;
        return 0;
    }
    r->head = at + bytes;
    return r->base + r->cur * r->slot + at;
}

/* a frame that wrote nothing keeps its slot, so the next frame does not write the slot the frame before it may
 * still be reading (kh1-decomp session 145: the Vita's GPU thread runs an empty frame's job without waiting) */
void enc_ring_frame(EncRing *r) {
    if (r->head > r->high) r->high = r->head;
    if (r->head) r->cur = (r->cur + 1) % r->nslots;
    r->head = 0;
    r->frames++;
}
