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

## Finding the image in a data folder

`vpk_disc_find_image(dir, &search, &found)` picks the image a player copied into the game's folder, the way
spyro-vita has since D60:

- Candidates are the files named `.cue`, `.bin`, `.img`, `.iso`, `.chd`, `.pbp` or `.ecm`, the extension in any
  case. Every `.cue` is tried first, then the rest; each group in name order ignoring case (names that differ only
  in case in byte order, so every file system gives one order). The first usable one wins. With no `.cue`, a
  Redump multi-track dump's `(Track 01).bin` comes first by name, and audio tracks never sniff as an image.
- A `.cue` is followed to its data track's `FILE`: the first track that isn't `AUDIO` or `CDG` (track 1 on a PS1
  or PS2 disc), which in a multi-file sheet need not be the first `FILE`. The name is looked up in the folder
  ignoring case and any folder in it (the Vita's memory card ignores case, Linux doesn't).
- What a file is comes from its data (`vpk_disc_sniff`), never its name: a raw image named `.iso` is raw, a cooked
  one named `.bin` is an `.iso`.
- The game says what it takes: `search.kinds` (bits `1u << VPK_DISC_*`; default raw and `.iso`: spyro-vita takes
  raw only, since an `.iso` has lost the XA music; kh1-vita's PS2 DVD is an `.iso`) and `search.cue_modes` (bits
  `1u << VPK_CUE_*`; default any; spyro-vita: `MODE2_2352` and `MODE2_2448`). A candidate it can't take is passed
  over for the next; a `.cue` it can't use leaves its `.bin` to be tried on its own.
- It returns 0 with `found.path` and `found.l` for `vpk_disc_open`, or -1 with the first candidate's reason
  (`VPK_IMG_NONE` when there was none). `search.seen` sees every candidate tried, for the log.

The reasons (`found.status`): `VPK_IMG_KIND` (`found.kind`: an `.iso`, CHD, PBP, ECM, unknown or unreadable),
`VPK_IMG_CUE_UNREADABLE`, `VPK_IMG_CUE_BAD` (no `FILE` with a `TRACK`), `VPK_IMG_CUE_TRACK` (no data track, or a mode
it doesn't take: `found.mode`), `VPK_IMG_CUE_MISSING` (`found.image` isn't in the folder). The game turns each into
its own message: the `chdman` line for a CHD, "dump bin/cue" for an `.iso`, the folder's name.

`vpk_cue_parse(text, n, &cue)` is the sheet on its own: `FILE` (quoted, or unquoted up to its type word), `TRACK`
with its mode and sector size, `INDEX 00` / `01` and `PREGAP` in sectors, keywords in any case, CRLF, a UTF-8 BOM,
up to 99 tracks; `vpk_cue_data_track` and `vpk_cue_file` read it. The layout still comes from the data: a pregap
kept in the file has sync headers or none, and `vpk_disc_sniff` finds the first LBA either way.

`vpk_disc_boot_serial(cnf, n, serial, cap)` reads `SYSTEM.CNF`'s `BOOT` (PS1) or `BOOT2` (PS2) line as a serial
(`cdrom:\SCUS_942.28;1` gives `SCUS-94228`), for telling the player which disc or region they copied.

What stays in the game: where the folder is, its messages to the player, which release it must be (hashes, the
marker that skips the big hash), and anything read once it is open (kh1-decomp's language patch).

**From:** kh1-decomp `host/port/cd_image.c` @ `85befa4a` (2048 / 2352 images, 64-bit reads, the path lookup) and
spyro-vita `src/platform/common/cdimage.c` @ `987363e` (sniffing, sync-pattern layout and pregap, the windows, the
directory walk, SHA-1; its decisions D58, D60) and @ `bb62f38` (the folder search, the cue sheet, the boot serial;
D60), generalised: the data track of a multi-file cue, the modes and kinds as the game's choice, any file the cue
names (spyro-vita looks only among the candidates).

**Test:** `test/discimage_test.c` writes a small ISO9660 tree as a `.iso`, raw mode 1, raw mode 2 (with and
without a pregap) and raw 2448 images, and checks sniffing, lookups, the walk, straight and windowed reads, the
ends, a sparse `.iso` read past 3 GiB, and SHA-1's FIPS vectors. It parses cue sheets (multi-file, pregaps, every
mode, BOM, CRLF, errors), and writes folders of images and sheets that check the search: cues first, name order,
a cue's file in another case and folder, a cue passed over for its `.bin`, a multi-file cue's data track, a Redump
dump without its cue, the first reason when nothing is usable; and the boot serial. `-DDISCIMAGE_MUTANT=1` (no
cue-first pass) and `=2` (a cue's first track, audio or not) must fail (`../test/run_host_tests.sh`, with a scratch
directory).
