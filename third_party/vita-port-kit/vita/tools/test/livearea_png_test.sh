#!/usr/bin/env bash
# livearea_png.py on synthetic art: RGB and RGBA PNGs written with every row filter are converted to 8-bit indexed
# at the right sizes and pass `check`; a good VPK passes `vpk --livearea`; a wrong size, an RGB PNG in a VPK and a
# VPK without its eboot.bin are refused. Needs python3 only.
#   vita/tools/test/livearea_png_test.sh
set -euo pipefail
tools=$(cd "$(dirname "$0")/.." && pwd)
w=$(mktemp -d "${TMPDIR:-/tmp}/livearea_png_test.XXXXXX")
trap 'rc=$?; [ "${KEEP:-0}" = 1 ] || rm -rf "$w"; exit $rc' EXIT
fails=0
check() { if eval "$2"; then echo "  ok   $1"; else echo "  FAIL $1"; fails=$((fails + 1)); fi; }
la() { python3 -I "$tools/livearea_png.py" "$@"; }

# Synthetic art: gradients with more than 256 colours (so median cut and dither run), rows written with filters 0-4.
python3 -I - "$w/art" <<'PY'
import os, struct, sys, zlib
out = sys.argv[1]; os.makedirs(out)
def png(path, w, h, bpp, pix):
    def chunk(t, d): return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    rows, prev = [], bytearray(w * bpp)
    for y in range(h):
        line = bytearray(b for x in range(w) for b in pix(x, y)); f = y % 5; enc = bytearray()
        for i, v in enumerate(line):
            a = line[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
            p = [0, a, b, (a + b) >> 1,
                 (lambda pa, pb, pc: a if pa <= pb and pa <= pc else b if pb <= pc else c)(abs(b - c), abs(a - c), abs(a + b - 2 * c))][f]
            enc.append((v - p) & 255)
        rows.append(bytes([f]) + bytes(enc)); prev = line
    ctype = {3: 2, 4: 6}[bpp]
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))
png(f"{out}/icon0.png", 128, 128, 4, lambda x, y: (x * 2, y * 2, (x + y) & 255, 255 - x))
png(f"{out}/bg.png", 840, 500, 3, lambda x, y: (x * 255 // 839, y * 255 // 499, (x ^ y) & 255))
png(f"{out}/startup.png", 280, 158, 3, lambda x, y: (40, x * 255 // 279, y))
png(f"{out}/wrong.png", 100, 100, 3, lambda x, y: (x, y, 0))
PY

la from "$w/art" "$w/out"
check "from: three 8-bit indexed PNGs at the Vita's sizes" \
  "la check '$w/out/icon0.png' 128x128 '$w/out/bg.png' 840x500 '$w/out/startup.png' 280x158"
check "from: the same output on a second run" "la from '$w/art' '$w/out2' && cmp -s '$w/out/bg.png' '$w/out2/bg.png'"
check "check: refuses an RGB PNG" "! la check '$w/art/bg.png' 840x500 2> /dev/null"
check "check: refuses a wrong size" "! la check '$w/out/icon0.png' 840x500 2> /dev/null"
check "from: refuses art at the wrong size" "mkdir -p '$w/bad' && cp '$w/art/icon0.png' '$w/art/startup.png' '$w/bad/' && cp '$w/art/wrong.png' '$w/bad/bg.png' && ! la from '$w/bad' '$w/badout' 2> /dev/null"

# VPKs
mkdir -p "$w/vpk/sce_sys/livearea/contents"
echo eboot > "$w/vpk/eboot.bin"; echo sfo > "$w/vpk/sce_sys/param.sfo"
cp "$w/out/icon0.png" "$w/vpk/sce_sys/"; cp "$w/out/bg.png" "$w/out/startup.png" "$w/vpk/sce_sys/livearea/contents/"
echo '<livearea/>' > "$w/vpk/sce_sys/livearea/contents/template.xml"
(cd "$w/vpk" && python3 -I -m zipfile -c "$w/good.vpk" eboot.bin sce_sys)
check "vpk: a good VPK passes with --livearea" "la vpk '$w/good.vpk' --livearea"
cp "$w/art/bg.png" "$w/vpk/sce_sys/livearea/contents/bg.png"
(cd "$w/vpk" && python3 -I -m zipfile -c "$w/rgb.vpk" eboot.bin sce_sys)
check "vpk: refuses an RGB bg.png" "! la vpk '$w/rgb.vpk' 2> /dev/null"
(cd "$w/vpk" && python3 -I -m zipfile -c "$w/noeboot.vpk" sce_sys)
check "vpk: refuses a VPK without eboot.bin" "! la vpk '$w/noeboot.vpk' 2> /dev/null"
check "usage: exit 2 with no arguments" "rc=0; la > /dev/null 2>&1 || rc=\$?; [ \$rc = 2 ]"
[ $fails = 0 ] && echo "livearea_png_test: ok" || { echo "livearea_png_test: $fails failed"; exit 1; }
