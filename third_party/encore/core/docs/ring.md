# ring

Per-frame GPU memory: vertex and uniform rings with N frames in flight. Each frame bumps a head inside its own
slot; ending the frame moves to the next slot and starts it empty, so nothing is freed one by one. An allocation
that does not fit returns 0 and is counted in `overflows` (the caller drops or splits the batch); nothing wraps into
a slot the GPU may still read. `high` is the most a frame used, for sizing. A frame that wrote nothing keeps its
slot: a GPU thread that runs an empty frame's job without waiting is never overtaken.

The ring does not wait for the GPU: the caller keeps at most `nslots - 1` frames ahead of it (on the Vita a
notification or `sceGxmFinish` per frame).

- `enc_ring_init(r, mem, slot, nslots)`: `mem` is `nslots * slot` bytes of GPU-mapped memory.
- `enc_ring_alloc(r, bytes, align)`: `align` a power of two; 0 when it does not fit.
- `enc_ring_frame(r)`: at the end of each frame.

No system headers, so a game can build it with its own flags (kh1-decomp builds it into the host runner, ARM
Linux and the Vita). Sizes are the game's: kh1-decomp keeps 4 MiB of vertices and 512 KiB of uniforms a frame,
two frames in flight.

spyro-vita has no equivalent: it draws through vitaGL, which owns its vertex memory. Its `gpu_thread.c` ring is a
different thing (a wrapping command queue between two threads).

**From:** kh1-decomp `host/port/ring.c` and `include/platform/ring.h` @ `85befa4a` (the frame count became a
parameter; the vertex-size helpers stayed in the game).

**Test:** `core/test/ring_test.c` (any host compiler; from `core/`):

```bash
cc -std=c99 -Wall -Iinclude src/enc_ring.c test/ring_test.c -o ring_test && ./ring_test
```
