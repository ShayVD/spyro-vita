#!/usr/bin/env python3
"""Compare a run that loaded a save state with a reference run that didn't
(decisions D51). Both ran the linux32 host with --state-hash, --wav and a
--log; the reference saved the state the other one loaded.

  savestate_compare.py REF_DIR RUN_DIR

- hashes: every main-loop-top row RUN_DIR wrote after its last load is
  compared, chunk by chunk, with REF_DIR's row at the same vblank;
- audio: RUN_DIR's audio.wav after the load against REF_DIR's from the
  state's vblank on (735 frames, 2940 bytes, per vblank);
- shots: shot-V.png files that both runs wrote, byte for byte.
"""
import os
import re
import sys

FRAME = 4
PER_VBLANK = 735


def hashes(path):
    rows = []
    with open(path) as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")
            rows.append((int(parts[0]), dict(p.split("=", 1) for p in parts[1:])))
    return rows


def loads(log):
    out = []
    with open(log) as f:
        for line in f:
            m = re.search(r"savestate: vblank counter (\d+) -> (\d+)", line)
            if m:
                out.append((int(m.group(1)), int(m.group(2))))
    return out


def wav_data(path):
    with open(path, "rb") as f:
        b = f.read()
    i = b.find(b"data")
    return b[i + 8:]


def main():
    ref, run = sys.argv[1], sys.argv[2]
    ld = loads(os.path.join(run, "spyro.log"))
    if not ld:
        print("no load in", run)
        return 1
    was, to = ld[-1]
    print(f"load: vblank {was} -> {to}")
    ok = True

    rref = {v: h for v, h in hashes(os.path.join(ref, "hash.tsv"))}
    rows = hashes(os.path.join(run, "hash.tsv"))
    # rows after the last load: from the last time the vblank went back
    start = max(i for i in range(len(rows)) if i == 0 or rows[i][0] < rows[i - 1][0])
    after = rows[start:]
    same = diff = missing = 0
    first = None
    for v, h in after:
        r = rref.get(v)
        if r is None:
            missing += 1
            continue
        bad = [k for k in h if r.get(k) != h[k]]
        if bad:
            diff += 1
            if first is None:
                first = (v, bad)
        else:
            same += 1
    print(f"hashes: {same} loop tops identical, {diff} differ, {missing} not in the reference "
          f"(vblanks {after[0][0]}-{after[-1][0]})")
    if first:
        print(f"  first difference at vblank {first[0]}: {' '.join(first[1])}")
        ok = False

    a, b = wav_data(os.path.join(ref, "audio.wav")), wav_data(os.path.join(run, "audio.wav"))
    ra, rb = a[to * PER_VBLANK * FRAME:], b[was * PER_VBLANK * FRAME:]
    n = min(len(ra), len(rb))
    if n == 0:
        print("audio: nothing to compare")
    elif ra[:n] == rb[:n]:
        print(f"audio: {n // FRAME} frames ({n // FRAME / 44100:.1f} s) after the load identical")
    else:
        k = next(i for i in range(n) if ra[i] != rb[i])
        print(f"audio: differs {k // FRAME} frames after the load (of {n // FRAME})")
        ok = False

    shots = sorted(f for f in os.listdir(run) if re.fullmatch(r"shot-\d+\.png", f))
    s_same = s_diff = 0
    for f in shots:
        p = os.path.join(ref, f)
        if not os.path.exists(p):
            continue
        if open(p, "rb").read() == open(os.path.join(run, f), "rb").read():
            s_same += 1
        else:
            s_diff += 1
    print(f"shots: {s_same} identical, {s_diff} differ")
    ok = ok and s_diff == 0
    print("RESULT:", "identical" if ok else "DIFFERENT")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
