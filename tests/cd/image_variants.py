#!/usr/bin/env python3
"""The disc image backend (src/platform/common/cdimage.c, decisions D60)
against the dumps players have and the ones it must refuse.

From the user's real dump (a 2352-byte .bin and its .cue) it makes, in WORK
(inside the repo, so tools/tc's container sees it; gitignored under build/):

  good:    the .cue; the .bin alone; the .bin as a CloneCD .img; a .cue
           naming the .bin in another case and folder; a 2448-byte-sector
           copy; a copy with a 150-sector pregap of silence; a copy that
           starts at LBA 10; an .iso and a broken .cue beside a good .cue
           (the good .cue wins, deterministically); a .cue whose track 1
           isn't MODE2 beside the .bin (the .bin is used).
           Each runs tests/cd/test_cd against the extracted files (the
           image's table is manifest.json byte for byte; every file of a
           good dump, through psx_cd_read_raw) and the game for FRAMES
           vblanks, whose last shot must equal the extracted files'.
  refused: an empty folder; a 2048-byte .iso; .chd, .pbp, .ecm headers;
           another region's disc; another SCUS_942.28; a bad WAD.WAD (with
           --disc-marker); a cut-short image; a file list that can't be
           read; random bytes; a .cue naming a missing .bin; a .cue whose
           track 1 isn't MODE2 (and no image beside it); a damaged
           manifest.json. Each must exit 1
           with its title on stderr; its panel is written as disc-error.png.

The full-size copies (~0.7 GB each) are made one at a time and deleted; the
refusals that need a whole image are sparse files holding only the
sectors the check reads. The .bin itself is hard-linked, never copied.

Usage: python3 -I tests/cd/image_variants.py IMAGE.cue FILES_DIR WORK OUT
  IMAGE.cue  the user's dump (disc/image/...cue)
  FILES_DIR  the extracted files of the same dump (disc/files)
  WORK       scratch folder inside the repo (build/discvar)
  OUT        logs and pictures (logs/runN-variants)
"""
import json
import os
import re
import shutil
import subprocess
import sys

RAW = 2352
FRAMES = 1200
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SPYRO = "build/linux32/src/platform/linux32/spyro"
TEST_CD = "build/linux32/tests/cd/test_cd"


def tc(args, log):
    with open(log, "w") as f:
        return subprocess.call([os.path.join(ROOT, "tools/tc")] + args, cwd=ROOT,
                               stdout=f, stderr=subprocess.STDOUT)


def rel(path):
    return os.path.relpath(path, ROOT)


def fresh(path):
    shutil.rmtree(path, ignore_errors=True)
    os.makedirs(path)
    return path


def link(src, dst):
    os.link(os.path.realpath(src), dst)


def copy_sectors(src, dst, sector=RAW, pad=b"", lead=0, skip=0):
    """dst: lead silent sectors, then src's sectors from `skip` on, each
    followed by `pad` (2448-byte sectors carry 96 subchannel bytes)."""
    with open(src, "rb") as f, open(dst, "wb") as out:
        out.write(bytes(sector + len(pad)) * lead)
        f.seek(skip * sector)
        while True:
            block = f.read(sector * 4096)
            if not block:
                break
            if pad:
                block = b"".join(block[i:i + sector] + pad for i in range(0, len(block), sector))
            out.write(block)


def small_sectors(manifest):
    """LBAs of everything but the bodies of the big files: the system area,
    directories, SYSTEM.CNF, SCUS_942.28 and every file's first sector."""
    keep = set(range(0, 40))
    for m in manifest:
        n = (m["size"] + 2047) // 2048
        keep.update(range(m["lba"], m["lba"] + (n if m["size"] < (1 << 20) else 1)))
    # directory records of S0/ and SOURCE/ sit between the files; take every
    # gap between files too (they are small)
    spans = sorted((m["lba"], m["lba"] + (m["size"] + 2047) // 2048) for m in manifest)
    for (a, b), (c, _) in zip(spans, spans[1:]):
        keep.update(range(b, c))
    return keep


def sparse_copy(src, dst, keep, length, patch=None):
    """A file of `length` bytes holding only src's sectors in `keep` (the
    rest a hole, read as zeros); patch(lba, sector) may change one."""
    with open(src, "rb") as f, open(dst, "wb") as out:
        for lba in sorted(keep):
            if (lba + 1) * RAW > length:
                continue
            f.seek(lba * RAW)
            sec = bytearray(f.read(RAW))
            if patch:
                patch(lba, sec)
            out.seek(lba * RAW)
            out.write(sec)
        out.truncate(length)


def write(path, data):
    with open(path, "wb") as f:
        f.write(data)


def main(argv):
    if len(argv) != 5:
        sys.exit(__doc__)
    cue, files, work, out = (os.path.abspath(a) for a in argv[1:])
    text = open(cue, encoding="latin-1").read()
    binname = re.search(r'FILE\s+"([^"]+)"', text).group(1)
    image = os.path.join(os.path.dirname(cue), binname)
    size = os.path.getsize(os.path.realpath(image))
    manifest = json.load(open(os.path.join(files, "manifest.json")))
    by_path = {m["path"]: m for m in manifest}
    os.makedirs(work, exist_ok=True)
    os.makedirs(out, exist_ok=True)
    results = []

    # the extracted files' picture at FRAMES-1, the reference
    ref = fresh(os.path.join(out, "ref-files"))
    tc([SPYRO, "--disc", rel(files), "--frames", str(FRAMES), "--shot", str(FRAMES - 1),
        "--shot-dir", rel(ref)], os.path.join(ref, "stdout.txt"))
    ref_shot = open(os.path.join(ref, f"shot-{FRAMES - 1}.png"), "rb").read()

    def good(name, make, expect_log):
        d = fresh(os.path.join(work, name))
        o = fresh(os.path.join(out, name))
        make(d)
        r1 = tc([TEST_CD, rel(files), rel(d)], os.path.join(o, "test_cd.txt"))
        t = open(os.path.join(o, "test_cd.txt")).read()
        ok = (r1 == 0 and "table identical" in t and "image: 16/16 files match" in t
              and "SKIP" not in t)
        r2 = tc([SPYRO, "--disc", rel(d), "--frames", str(FRAMES), "--shot", str(FRAMES - 1),
                 "--shot-dir", rel(o)], os.path.join(o, "stdout.txt"))
        s = open(os.path.join(o, "stdout.txt")).read()
        shot = os.path.join(o, f"shot-{FRAMES - 1}.png")
        same = os.path.exists(shot) and open(shot, "rb").read() == ref_shot
        picked = re.search(r"libcd: disc image (.*)", s)
        ok = ok and r2 == 0 and same and picked and expect_log in picked.group(1)
        results.append((name, "ok" if ok else "FAIL",
                        (picked.group(1) if picked else "no image") +
                        f"; test_cd exit {r1}, game exit {r2}, shot {'same' if same else 'DIFFERS'}"))
        shutil.rmtree(d)

    def refused(name, make, title, extra=()):
        d = fresh(os.path.join(work, name))
        o = fresh(os.path.join(out, name))
        make(d)
        r = tc([SPYRO, "--disc", rel(d), "--frames", "10", "--shot-dir", rel(o)] + list(extra),
               os.path.join(o, "stdout.txt"))
        s = open(os.path.join(o, "stdout.txt")).read()
        m = re.search(r"\n\n([^\n]+)\n([^\n]*(?:\n[^\n]+)*)\n\n", s)
        got_title, got_text = (m.group(1), m.group(2)) if m else ("", "")
        ok = r == 1 and got_title == title and os.path.exists(os.path.join(o, "disc-error.png"))
        results.append((name, "ok" if ok else "FAIL",
                        f"exit {r}: {got_title}: {got_text}".replace("\n", " / ")))
        shutil.rmtree(d)

    # ---- good ----
    good("cue", lambda d: (link(image, os.path.join(d, binname)), shutil.copy(cue, d)), "(from ")
    good("bare-bin", lambda d: link(image, os.path.join(d, "spyro.bin")), "spyro.bin, 2352")
    good("clonecd-img", lambda d: link(image, os.path.join(d, "spyro.img")), "spyro.img, 2352")

    def cue_case(d):
        link(image, os.path.join(d, binname))
        write(os.path.join(d, "spyro.cue"),
              f'FILE "C:\\DUMPS\\{binname.upper()}" BINARY\r\n  TRACK 01 MODE2/2352\r\n'
              "    INDEX 01 00:00:00\r\n".encode())
    good("cue-case-folder", cue_case, "(from spyro.cue)")

    def several(d):
        link(image, os.path.join(d, binname))
        shutil.copy(cue, os.path.join(d, "b.cue"))
        write(os.path.join(d, "a.cue"), b'FILE "missing.bin" BINARY\n  TRACK 01 MODE2/2352\n')
        write(os.path.join(d, "a.iso"), bytes(17 * 2048))
    good("several", several, "(from b.cue)")

    def cue_mode1_bin(d):  # a .cue it can't use is passed over for the .bin beside it
        link(image, os.path.join(d, "spyro.bin"))
        write(os.path.join(d, "spyro.cue"), b'FILE "spyro.bin" BINARY\n  TRACK 01 MODE1/2048\n')
    good("cue-mode1-bin", cue_mode1_bin, "spyro.bin, 2352")

    good("sector-2448",
         lambda d: copy_sectors(image, os.path.join(d, "spyro.bin"), pad=bytes(96)),
         "2448-byte sectors, first LBA 0")
    good("pregap-150",
         lambda d: copy_sectors(image, os.path.join(d, "spyro.bin"), lead=150),
         "2352-byte sectors, first LBA -150")
    good("starts-lba-10",
         lambda d: copy_sectors(image, os.path.join(d, "spyro.bin"), skip=10),
         "2352-byte sectors, first LBA 10")

    # ---- refused ----
    refused("none", lambda d: None, "NO DISC IMAGE FOUND")

    def iso(d):
        with open(os.path.realpath(image), "rb") as f, open(os.path.join(d, "spyro.iso"), "wb") as o:
            for lba in range(400):
                f.seek(lba * RAW + 24)
                o.write(f.read(2048))
    refused("iso", iso, "AN ISO HAS NO MUSIC")
    refused("chd", lambda d: write(os.path.join(d, "spyro.chd"), b"MComprHD" + bytes(200)),
            "CONVERT THE CHD FIRST")
    refused("pbp", lambda d: write(os.path.join(d, "EBOOT.PBP"), b"\0PBP" + bytes(200)),
            "A PBP CAN'T BE USED")
    refused("ecm", lambda d: write(os.path.join(d, "spyro.bin.ecm"), b"ECM\0" + bytes(200)),
            "UNPACK THE ECM FIRST")
    refused("random", lambda d: write(os.path.join(d, "spyro.bin"), os.urandom(1 << 20)),
            "CAN'T READ THE IMAGE")
    refused("cue-missing-bin",
            lambda d: write(os.path.join(d, "spyro.cue"),
                            b'FILE "spyro.bin" BINARY\n  TRACK 01 MODE2/2352\n'),
            "THE .BIN IS MISSING")

    def cue_mode1(d):  # the image itself isn't a candidate (.dat), so the .cue's reason shows
        write(os.path.join(d, "spyro.dat"), b"")
        write(os.path.join(d, "spyro.cue"), b'FILE "spyro.dat" BINARY\n  TRACK 01 MODE1/2048\n')
    refused("cue-mode1", cue_mode1, "CAN'T READ THE CUE FILE")
    refused("manifest-damaged",
            lambda d: write(os.path.join(d, "manifest.json"), b"[{\"path\": 3}]"),
            "MANIFEST.JSON IS DAMAGED")

    keep = small_sectors(manifest)
    exe, cnf = by_path["SCUS_942.28"], by_path["SYSTEM.CNF"]

    def pal(lba, sec):  # SCUS_942.28 renamed and booted as a PAL serial
        if lba == cnf["lba"] or lba < 40:
            sec[24:24 + 2048] = sec[24:24 + 2048].replace(b"SCUS_942.28", b"SCES_009.00")
    refused("wrong-region",
            lambda d: sparse_copy(image, os.path.join(d, "spyro.bin"), keep, size, pal),
            "WRONG DISC")

    def exe_flip(lba, sec):
        if lba == exe["lba"] + 100:
            sec[24 + 7] ^= 0xff
    refused("wrong-exe",
            lambda d: sparse_copy(image, os.path.join(d, "spyro.bin"), keep, size, exe_flip),
            "WRONG VERSION OF THE GAME")
    refused("bad-wad",
            lambda d: sparse_copy(image, os.path.join(d, "spyro.bin"), keep, size),
            "BAD DUMP", ["--disc-marker", rel(os.path.join(work, "bad-wad-marker.txt"))])
    refused("cut-short",
            lambda d: sparse_copy(image, os.path.join(d, "spyro.bin"), keep, size // 2 // RAW * RAW),
            "THE IMAGE IS CUT SHORT")
    refused("no-file-list",
            lambda d: sparse_copy(image, os.path.join(d, "spyro.bin"), set(range(0, 17)), size),
            "THE IMAGE IS DAMAGED")

    width = max(len(r[0]) for r in results)
    lines = [f"{n:<{width}}  {s:<4}  {d}" for n, s, d in results]
    report = "\n".join(lines) + "\n"
    open(os.path.join(out, "summary.txt"), "w").write(report)
    print(report, end="")
    bad = sum(1 for r in results if r[1] != "ok")
    print(f"{len(results) - bad}/{len(results)} cases ok")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
