#!/usr/bin/env python3
"""Extract the files from your own Spyro the Dragon disc image, for the port
(ux0:data/spyro/ on the Vita). The port runs the USA release, SCUS-94228,
and the European one, SCES-01438 (decisions D154); the Japanese release
(SCPS-10083) is recognised too, and the port says it isn't supported yet
(decisions D132).

Reads a raw dump: a .cue sheet (the first track's file is used), or a
.bin/.img with 2352-byte sectors (the usual bin/cue, Redump, CloneCD .img)
or 2448-byte sectors (with subchannel data). A dump that starts before or
after LBA 0 (a pregap) is found by the sector headers. Compressed or
converted images (.chd, .pbp, .ecm) and 2048-byte .iso files are refused
with a note on how to get a raw dump: an .iso has lost the XA music.

Form 1 files are written as plain 2048-byte-per-sector data. XA / Form 2 files
(streamed audio or video) are written as 2336-byte sectors (subheader + data),
the same layout dumpsxiso uses. A manifest.json records each file's LBA, size
and type. The release is the one whose executable SYSTEM.CNF boots (REGIONS
below), and every file is then checked against the SHA-1 of a good dump of
it: the executable or WAD.WAD wrong is an error (another version of the
game, or a bad dump); other files only warn.

Usage: python3 -I tools/extract_disc.py [--no-verify] <image.cue|.bin> <out_dir>
Needs Python 3.6 or newer, nothing else.
"""
import hashlib
import json
import os
import re
import struct
import sys

RAW = 2352
SYNC = b"\x00\xff\xff\xff\xff\xff\xff\xff\xff\xff\xff\x00"

XA_FORM2 = 0x1000
XA_INTERLEAVED = 0x2000
XA_CDDA = 0x4000

# SHA-1 of each file from a good dump of each release, as this script writes
# it (XA files as 2336-byte sectors), with its serial and executable; the
# port's table is src/platform/common/region.c (D132). SCUS_942.28 is the
# executable the decomp rebuilds byte for byte (tools/build_matching.sh). The
# PAL and JP hashes are of one dump each, not yet checked against Redump
# (docs/regions-plan.md §10, decision 6).
REGIONS = {
    "USA": {
        "serial": "SCUS-94228",
        "exe": "SCUS_942.28",
        "supported": True,
        "files": {
            "SYSTEM.CNF": "913fe0d270ad295117e6a91eb667284845df8918",
            "WAD.WAD": "ab68001377ea93fb309a5c2a35e772c9d1f57e0b",
            "SCUS_942.28": "84e3728ab94720d0873e2514adf4aade4935e0c5",
            "PETEXA0.STR": "045a1fedd5d4a224c95a65f3802488bf9c952c60",
            "PETEXA1.STR": "4fad48fbc15616998efb9379e976f5891d7c97e4",
            "PETEXA2.STR": "19ed8ddac4a83182d1744139b23a5bd284e5cba1",
            "PETEXA3.STR": "e51baa4cefc3f85e454943d964a3690b9f34be52",
            "PETEXA4.STR": "6c6b387bcd7b61226281d2adf48adbab547fa089",
            "PETEXA5.STR": "aa4a2c6b2eac867751d30a761ca9ab97877c3ce4",
            "S0/CRASH.EXE": "82d1047a5bf6b6563ea0107706e02b9e01dbcc2a",
            "S0/S000000D.NSD": "fb9788f83e39c71b61064ace91e41c2c5d79b58b",
            "S0/S000000D.NSF": "4203d38457b960452ba99e7f23955d40f3ba04e9",
            "S0/S000003C.NSD": "aa2d89c6e8b630df93120a332af2132858b604d8",
            "S0/S000003C.NSF": "c5105ab984dbcee6a787eb2a178f2764c37c36c1",
            "S0/WARP.BIN": "b5895f4fe129f9afdba70cfd1fdef794032be99f",
            "SOURCE/SOURCE.TRD": "498601c3f0b08dd66b53b16c2be9ac10d171721f",
        },
    },
    "Europe": {
        "serial": "SCES-01438",
        "exe": "SCES_014.38",
        "supported": True,
        "files": {
            "SYSTEM.CNF": "413c21c006d9fb38562650f14cf6fa37e7c33d06",
            "WAD.WAD": "f1fc05d7cf197ba9cfc693738b44020d8798f99d",
            "SCES_014.38": "fdd04c044cd1892ae28c43e9e619dd41a848058a",
            "MUSIC1.STR": "193272992b656801080299b2e7204fc3aaacd4d6",
            "MUSIC2.STR": "5abb0f4f153ebc71fa0b940bdd6868865650e1b1",
            "MUSIC3.STR": "df9e18a9795e17521816b7339cf6987525f05cba",
            "MUSIC4.STR": "b3380d844fdf21bfa2354e4cd24c6cdb2a3970bf",
            "MUSIC5.STR": "06b576a42b4ed411e5ffe7acf1ccabf869c20689",
            "MUSIC6.STR": "11e64cab0b72999f08c908b65093bd1196b565dc",
            "S0/CRASH.EXE": "d965322d31403c6ae1307235286d4bf15c2aa385",
            "S0/S000000D.NSD": "cb4291cb519855c6f69bf40722559cd337fec077",
            "S0/S000000D.NSF": "73d832166ac03ee8f2f3475a26fc3620d59eb7e6",
            "S0/S000003C.NSD": "ce6a5fbabe3cc9040cfa2a36ce5e9b4f903c8619",
            "S0/S000003C.NSF": "fb81d721d27dbe5ecde854366d6c6c12e76e59ea",
            "S0/WARP.BIN": "068bb105369931465c41735fd16dbe0ba535a2c7",
            "SOURCE/SOURCE.TRD": "498601c3f0b08dd66b53b16c2be9ac10d171721f",
        },
    },
    "Japan": {
        "serial": "SCPS-10083",
        "exe": "SCPS_100.83",
        "supported": False,
        "files": {
            "SYSTEM.CNF": "d2a1c4dcdb824e4ebb064387d8c227278ad81e65",
            "WAD.WAD": "812f63ff07366cf80f152ffd5bdd2e03e0582abb",
            "SCPS_100.83": "3818ddb9520e2fe0114de48f5392972c33690399",
            "MUSIC1.STR": "193272992b656801080299b2e7204fc3aaacd4d6",
            "MUSIC2.STR": "5abb0f4f153ebc71fa0b940bdd6868865650e1b1",
            "MUSIC3.STR": "df9e18a9795e17521816b7339cf6987525f05cba",
            "MUSIC4.STR": "b3380d844fdf21bfa2354e4cd24c6cdb2a3970bf",
            "MUSIC5.STR": "06b576a42b4ed411e5ffe7acf1ccabf869c20689",
            "MUSIC6.STR": "11e64cab0b72999f08c908b65093bd1196b565dc",
            "SOURCE/SOURCE.TRD": "498601c3f0b08dd66b53b16c2be9ac10d171721f",
        },
    },
}


class DumpError(Exception):
    pass


class Image:
    """A raw image: sector size (2352 or 2448) and the LBA of its first sector."""

    def __init__(self, path):
        self.f = open(path, "rb")
        head = self.f.read(2448 * 300)
        self.sector, self.base = None, 0
        # The first data sector: at the start, or after a pregap of silence.
        for size in (RAW, 2448):
            for k in range(len(head) // size - 1):
                at = k * size
                if head[at:at + 12] == SYNC and head[at + size:at + size + 12] == SYNC:
                    self.sector = size
                    m, s, fr = (bcd(b) for b in head[at + 12:at + 15])
                    self.base = (m * 60 + s) * 75 + fr - 150 - k
                    break
            if self.sector:
                break
        if self.sector is None:
            raise DumpError(f"{path}: not a raw 2352-byte-sector CD image ({what_is(path, head)})")

    def raw(self, lba):
        self.f.seek((lba - self.base) * self.sector)
        sector = self.f.read(RAW)
        if len(sector) != RAW or sector[:12] != SYNC:
            raise DumpError(f"bad sector at LBA {lba}: the dump is cut short or damaged")
        return sector


def bcd(b):
    return (b >> 4) * 10 + (b & 15)


def what_is(path, head):
    if head[:8] == b"MComprHD":
        return "a CHD; convert it first: chdman extractcd -i game.chd -o game.cue"
    if head[:4] == b"\0PBP":
        return "a PSP EBOOT.PBP; use a bin/cue dump of the disc instead"
    if head[:4] == b"ECM\0":
        return "an ECM file; unpack it with unecm first"
    with open(path, "rb") as f:
        f.seek(16 * 2048 + 1)
        if f.read(5) == b"CD001":
            return ("a 2048-byte .iso, which has lost the XA music; dump the disc as "
                    "bin/cue (MODE2/2352) instead")
    return "unknown format; dump the disc as bin/cue (MODE2/2352)"


def resolve(path):
    """A .cue: the image file of its first track, relative to the .cue."""
    if not path.lower().endswith(".cue"):
        return path
    text = open(path, "rb").read().decode("latin-1")
    m = re.search(r'^\s*FILE\s+(?:"([^"]+)"|(\S+))', text, re.M | re.I)
    if not m:
        raise DumpError(f"{path}: no FILE line")
    if not re.search(r"TRACK\s+0*1\s+MODE2/(2352|2448)", text, re.I):
        raise DumpError(f"{path}: track 1 isn't MODE2/2352, as a PS1 data track should be")
    name = m.group(1) or m.group(2)
    img = os.path.join(os.path.dirname(os.path.abspath(path)), name)
    if not os.path.exists(img):
        raise DumpError(f"{path} names {name}, which isn't next to it")
    return img


def read_form1(img, lba, count=1):
    return b"".join(img.raw(lba + i)[24:24 + 2048] for i in range(count))


def parse_dir(img, lba, size):
    data = read_form1(img, lba, (size + 2047) // 2048)[:size]
    entries, pos = [], 0
    while pos < len(data):
        rec_len = data[pos]
        if rec_len == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        rec = data[pos:pos + rec_len]
        name_len = rec[32]
        name = rec[33:33 + name_len]
        sys_use = rec[33 + name_len + (1 - name_len % 2):]
        xa_attr = 0
        if len(sys_use) >= 14 and sys_use[6:8] == b"XA":
            xa_attr = struct.unpack(">H", sys_use[4:6])[0]
        if name not in (b"\x00", b"\x01"):
            entries.append({
                "name": name.decode("ascii").split(";")[0],
                "lba": struct.unpack("<I", rec[2:6])[0],
                "size": struct.unpack("<I", rec[10:14])[0],
                "is_dir": bool(rec[25] & 0x02),
                "xa_attr": xa_attr,
            })
        pos += rec_len
    return entries


def sector_is_form2(img, lba):
    return bool(img.raw(lba)[18] & 0x20)


def extract(image, out_dir):
    manifest = []
    img = Image(resolve(image))
    pvd = read_form1(img, 16)
    if pvd[1:6] != b"CD001":
        raise DumpError("no ISO9660 primary volume descriptor at LBA 16")
    root = pvd[156:156 + 34]
    stack = [("", struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0])]
    while stack:
        rel, lba, size = stack.pop()
        os.makedirs(os.path.join(out_dir, rel), exist_ok=True)
        for e in parse_dir(img, lba, size):
            path = os.path.join(rel, e["name"])
            if e["is_dir"]:
                stack.append((path, e["lba"], e["size"]))
                continue
            if e["xa_attr"] & XA_CDDA:
                raise DumpError(f"{path}: CD-DA files are not supported")
            count = (e["size"] + 2047) // 2048
            xa = bool(e["xa_attr"] & (XA_FORM2 | XA_INTERLEAVED)) or (
                count > 0 and sector_is_form2(img, e["lba"]))
            with open(os.path.join(out_dir, path), "wb") as out:
                if xa:
                    for i in range(count):
                        out.write(img.raw(e["lba"] + i)[16:RAW])
                else:
                    out.write(read_form1(img, e["lba"], count)[:e["size"]])
            manifest.append({"path": path, "lba": e["lba"], "size": e["size"],
                             "type": "xa" if xa else "data"})
    manifest.sort(key=lambda m: m["lba"])
    with open(os.path.join(out_dir, "manifest.json"), "w") as m:
        json.dump(manifest, m, indent=2)
    return manifest


def region_of(manifest, out_dir):
    """The release (a REGIONS key): the one whose executable SYSTEM.CNF
    boots, else the first whose executable is on the disc, else None."""
    have = {m["path"].replace(os.sep, "/").upper() for m in manifest}
    boot = None
    if "SYSTEM.CNF" in have:
        text = open(os.path.join(out_dir, "SYSTEM.CNF"), "rb").read().decode("latin-1")
        m = re.search(r"cdrom:[\\/]*([^;\s]+)", text)
        if m:
            boot = re.split(r"[\\/]", m.group(1))[-1].upper()
    for name, r in REGIONS.items():
        if boot == r["exe"]:
            return name, boot
    for name, r in REGIONS.items():
        if r["exe"] in have:
            return name, boot
    return None, boot


def verify(manifest, out_dir, region):
    """Errors (wrong game or a bad dump) and warnings, against REGIONS."""
    errors, warnings = [], []
    known = REGIONS[region]["files"]
    required = (REGIONS[region]["exe"], "WAD.WAD")
    have = {m["path"].replace(os.sep, "/"): m["path"] for m in manifest}
    for name in required:
        if name not in have:
            errors.append(f"{name} is missing: this isn't Spyro the Dragon ({region}, "
                          f"{REGIONS[region]['serial']})")
    for name, want in sorted(known.items()):
        if name not in have:
            if name not in required:
                warnings.append(f"{name} is missing")
            continue
        h = hashlib.sha1()
        with open(os.path.join(out_dir, have[name]), "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                h.update(block)
        if h.hexdigest() != want:
            msg = f"{name}: SHA-1 {h.hexdigest()}, a good dump has {want}"
            (errors if name in required else warnings).append(msg)
    return errors, warnings


def main(argv):
    args = [a for a in argv[1:] if a != "--no-verify"]
    if len(args) != 2:
        sys.exit(__doc__)
    try:
        manifest = extract(args[0], args[1])
    except DumpError as e:
        sys.exit(f"error: {e}")
    for m in manifest:
        print(f'{m["lba"]:>7}  {m["type"]:<4}  {m["size"]:>11}  {m["path"]}')
    if "--no-verify" in argv:
        return 0
    region, boot = region_of(manifest, args[1])
    if region is None:
        print(f"error: this isn't Spyro the Dragon: it boots {boot or 'nothing'}. The port needs "
              "the USA release, SCUS-94228.", file=sys.stderr)
        return 1
    r = REGIONS[region]
    errors, warnings = verify(manifest, args[1], region)
    for w in warnings:
        print("warning:", w, file=sys.stderr)
    for e in errors:
        print("error:", e, file=sys.stderr)
    if errors:
        print(f"This looks like Spyro the Dragon ({region}, {r['serial']}), but not a good "
              "dump of the release the port knows. The port needs the USA release, SCUS-94228, "
              "or the European one, SCES-01438. "
              "--no-verify keeps going anyway; the port may not run.", file=sys.stderr)
        return 1
    print(f"All {len(r['files']) - len(warnings)} known files match a good dump of "
          f"{r['serial']} ({region}).")
    if not r["supported"]:
        print("Spyro Vita recognises this release but can't run it yet: it needs the USA "
              "release, SCUS-94228, or the European one, SCES-01438, for now.")
        return 0
    print(f"Copy everything in {args[1]} (manifest.json too) to ux0:data/spyro/.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
