# discimage

The user's own disc image, read in place by sector. PS1 and PS2 CDs use the same sectors, so one layer serves
both:

- a 2048-byte `.iso` (PS2 DVDs are only this);
- raw images of 2352-byte sectors, or 2448 with subchannel data (`.bin`, a CloneCD `.img`), mode 1 or mode 2
  form 1, with any pregap. The layout comes from the first sector whose sync pattern is followed by another one,
  its MSF header gives the first LBA, and sector 16's mode byte gives the data offset (16 or 24).

`vpk_disc_sniff` tells these apart from formats to convert first (CHD, PSP PBP, ECM), so a game can tell the
player what to do. `vpk_disc_open` (or `vpk_disc_open_any`, which also checks the volume descriptor) opens the
image. Reads go straight to the file, or (`windows = 1`) through two 32-sector windows with one file-system read
each, which suits a game streaming music or loading a level sector by sector. The optional `now` / `io` hooks
count the file-system time.

- `vpk_disc_read_raw(d, lba, off, buf, n)`: bytes of a sector as the file holds it (off 16 the XA subheader, 24
  form 1's data).
- `vpk_disc_read(d, lba, count, buf)`: 2048 data bytes a sector.
- `vpk_disc_find(d, "\\DIR\\FILE.EXT;1", &lba, &size)`: a file by ISO9660 path (either separator, case and
  `;version` ignored).
- `vpk_disc_walk(d, fn, user)`: every file in the tree with its CD-XA attributes (form 2, interleaved, CD-DA).
- `vpk_sha1_*`: for checking the disc is the right release.

Offsets are 64-bit everywhere: `sceIo` on the Vita (newlib's `fseek` takes a 32-bit `long`), `fseeko` with
`_FILE_OFFSET_BITS 64` on hosts (a plain `fopen` of a 3.3 GiB image fails on 32-bit glibc).

What stays in the game: where the image lives and what it is called, the cue sheet and folder search with its
messages to the player (spyro-vita), which release it must be (hashes), and anything read once it is open
(kh1-decomp's language patch).

**From:** kh1-decomp `host/port/cd_image.c` @ `85befa4a` (2048 / 2352 images, 64-bit reads, the path lookup) and
spyro-vita `src/platform/common/cdimage.c` @ `987363e` (sniffing, sync-pattern layout and pregap, the windows, the
directory walk, SHA-1; its decisions D58, D60).

**Test:** `test/discimage_test.c` writes a small ISO9660 tree as a `.iso`, raw mode 1, raw mode 2 (with and
without a pregap) and raw 2448 images, and checks sniffing, lookups, the walk, straight and windowed reads, the
ends, a sparse `.iso` read past 3 GiB, and SHA-1's FIPS vectors (`../test/run_host_tests.sh`, with a scratch
directory).
