#!/usr/bin/env python3
"""Make a PS1 Classic-shaped EBOOT.PBP from your own disc image, for testing the port's PBP reader
(src/platform/common/pbp.c, decisions D157) without a PlayStation Store copy. Standard library only.

    tests/pbp/make_pbp.py "disc/image/Spyro the Dragon (USA).bin" build/linux32/pbp-made/DUMP-US/EBOOT.PBP
    tests/pbp/make_pbp.py IMAGE.bin OUT/EBOOT.PBP [--stored] [--keep-codes] [--disc-id SCUS94228] [--title TEXT]

The image is a raw dump (2352-byte sectors from LBA 0, the .bin of a .bin/.cue). The output is the shape
PSX2PSP writes and the port reads: a PBP header, a PARAM.SFO with DISC_ID and TITLE, and a DATA.PSAR that is a
PSISOIMG0000 image with a plain header (no PGD container) and a block table at 0x4000; each block is 16 sectors,
compressed with raw deflate (or stored whole when that isn't smaller; --stored stores every block). As in
Sony's images, each sector's sync, MSF header and error codes (a Form 2 sector's EDC, a Form 1 sector's EDC
and P/Q parity) are zeroed unless --keep-codes is given, so the port's extractor has to compute them again
and tests/pbp compares the result with the dump byte for byte.

What this doesn't make, and so doesn't test: Sony's own shape (LZRC blocks behind a PGD-encrypted header).
tests/pbp/test_pbp.c codes LZRC blocks itself; the PGD path needs a real copy under disc/psn/.

The output holds the game's data: write it under build/ or disc/ (both gitignored), never into the repository.
Then:  tools/tc build/linux32/tests/pbp/test_pbp build/linux32/tests/pbp/work-made build/linux32/pbp-made disc/files
       (ctest's pbp_made does the same), and the game:  --disc build/linux32/pbp-made/DUMP-US
"""
import argparse
import os
import struct
import sys
import zlib

SECTOR = 2352
BLOCK_SECTORS = 16
BLOCK = SECTOR * BLOCK_SECTORS
TABLE_AT = 0x4000
DATA_AT = 0x100000
PSAR_AT = 0x800  # where DATA.PSAR starts in the file
SYNC = b"\0" + b"\xff" * 10 + b"\0"


def sfo(entries):
    """A PARAM.SFO of string entries [(key, value)]."""
    keys = b""
    data = b""
    index = b""
    for key, value in entries:
        v = value.encode("utf-8") + b"\0"
        room = (len(v) + 3) & ~3
        index += struct.pack("<HHIII", len(keys), 0x0204, len(v), room, len(data))
        keys += key.encode() + b"\0"
        data += v.ljust(room, b"\0")
    keys = keys.ljust((len(keys) + 3) & ~3, b"\0")
    head = 20 + len(index)
    return struct.pack("<4sIIII", b"\0PSF", 0x0101, head, head + len(keys), len(entries)) + index + keys + data


def as_stored(block, keep_codes):
    """The block's sectors as Sony's images hold them: sync and MSF header zero, and the error codes too."""
    out = bytearray(block)
    for k in range(0, len(out), SECTOR):
        if out[k:k + 12] != SYNC:
            continue  # not a data sector (padding past the image's end)
        mode = out[k + 15]
        out[k:k + 15] = bytes(15)
        if keep_codes or mode != 2:
            continue
        if out[k + 18] & 0x20:  # Form 2: the EDC after 2324 data bytes
            out[k + 2348:k + 2352] = bytes(4)
        else:  # Form 1: the EDC and 276 bytes of P/Q parity after 2048 data bytes
            out[k + 2072:k + 2352] = bytes(280)
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image", help="the raw dump: 2352-byte sectors from LBA 0")
    ap.add_argument("out", help="the EBOOT.PBP to write, in a folder of its own under build/ or disc/")
    ap.add_argument("--stored", action="store_true", help="store every block whole (no deflate)")
    ap.add_argument("--keep-codes", action="store_true", help="leave the sectors' error codes as the dump has them")
    ap.add_argument("--disc-id", default="SCUS94228")
    ap.add_argument("--title", default="Spyro the Dragon (made from a dump by tests/pbp/make_pbp.py)")
    a = ap.parse_args()

    size = os.path.getsize(a.image)
    if size % SECTOR or size < 17 * SECTOR:
        sys.exit("%s: %d bytes isn't a whole number of 2352-byte sectors" % (a.image, size))
    nblocks = (size + BLOCK - 1) // BLOCK
    if nblocks > (DATA_AT - TABLE_AT) // 32:
        sys.exit("%s: too large for one disc's block table" % a.image)
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)

    param = sfo([("DISC_ID", a.disc_id), ("TITLE", a.title)])
    if 0x28 + len(param) > PSAR_AT:
        sys.exit("the title is too long")
    head = bytearray(PSAR_AT)
    struct.pack_into("<4sI8I", head, 0, b"\0PBP", 0x00010000, 0x28, *([0x28 + len(param)] * 6), PSAR_AT)
    head[0x28:0x28 + len(param)] = param

    psar = bytearray(DATA_AT)
    psar[0:12] = b"PSISOIMG0000"
    game_id = ("_%s_%s" % (a.disc_id[:4], a.disc_id[4:])).encode()
    psar[0x400:0x400 + len(game_id)] = game_id
    struct.pack_into("<I", psar, 0xBFC, DATA_AT)
    title = a.title.encode("utf-8")[:127]
    psar[0x122C:0x122C + len(title)] = title

    stored = packed = 0
    with open(a.image, "rb") as src, open(a.out, "wb") as dst:
        dst.write(head)
        dst.write(psar)  # the table is rewritten below
        off = 0
        for b in range(nblocks):
            block = src.read(BLOCK).ljust(BLOCK, b"\0")
            block = as_stored(block, a.keep_codes)
            data = block
            if not a.stored:
                z = zlib.compressobj(9, zlib.DEFLATED, -15)
                comp = z.compress(block) + z.flush()
                if len(comp) < BLOCK:
                    data = comp
            if len(data) == BLOCK:
                stored += 1
            else:
                packed += 1
            struct.pack_into("<IHH", psar, TABLE_AT + 32 * b, off, len(data), 0)
            dst.write(data)
            off += len(data)
            if b % 2000 == 0:
                sys.stderr.write("\r%d%%" % (b * 100 // nblocks))
        dst.seek(PSAR_AT)
        dst.write(psar)
    sys.stderr.write("\r%s: %d blocks (%d deflate, %d stored), %.1f MB from %.1f MB\n"
                     % (a.out, nblocks, packed, stored, (PSAR_AT + DATA_AT + off) / 1048576.0, size / 1048576.0))


if __name__ == "__main__":
    main()
