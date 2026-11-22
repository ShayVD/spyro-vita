#!/usr/bin/env python3
"""Check a bounded presentation capture (vita/runtime/presenttrace) against its launch's log and report the CPU
swap-return cadence: nearest-rank p50 / p95 / p99 / max of the picture interval and pictures per second.

  vita/tools/present_trace.py present-trace.csv --log RUN/game.log [--min-frame N] [--max-frame N]
                              [--prefix vpk-presenttrace] [--output report.json]

Refuses a CSV without its `# end` footer, one whose export line is not in this log (an old CSV cannot pass for a new
capture: the pid, nonce and capture number must match), and one whose timestamps, intervals or picture numbers do not
add up. An interval counts when both of its pictures are in the game-frame range. This is the CPU's return from the
swap, not scan-out, GPU time or input latency.
From kh1-vita scripts/analyze_present_trace.py @ 75afd241. Python 3, stdlib.
"""
import argparse
import csv
import hashlib
import io
import json
import math
import re
from pathlib import Path


def analyze(raw, log, low=0, high=2**32 - 1, prefix="vpk-presenttrace"):
    lines = raw.decode().splitlines()
    if not lines:
        raise ValueError("empty trace")
    head = re.fullmatch(r"# pid=([0-9a-f]{8}) nonce=(\d+) capture=(\d+) samples=(\d+) capacity_stop=([01]) "
                        r"clock=sceKernelGetProcessTimeWide units=us event=cpu-swap-return", lines[0])
    if not head:
        raise ValueError("unrecognized trace header")
    pid, nonce, capture, count, cap = head.groups()
    if lines[-1] != f"# end samples={count}":
        raise ValueError("missing or mismatched completion footer")
    marker = (f"{prefix}: export complete {count} samples; capacity_stop={cap}; "
              f"pid={pid} nonce={nonce} capture={capture}")
    if not any(l.endswith(marker) for l in log.splitlines()):
        raise ValueError("trace has no matching completed export in this launch log")
    rows = [{k: int(v) for k, v in row.items()} for row in csv.DictReader(io.StringIO("\n".join(lines[1:-1])))]
    if len(rows) != int(count):
        raise ValueError("sample count mismatch")
    for i, row in enumerate(rows):
        if i == 0:
            if row["interval_us"] != 0:
                raise ValueError("first record must have no previous interval")
        else:
            prev = rows[i - 1]
            delta = row["swap_return_us"] - prev["swap_return_us"]
            if (delta <= 0 or delta != row["interval_us"] or row["picture"] <= prev["picture"]
                    or row["observed_game_frame"] < prev["observed_game_frame"]):
                raise ValueError("non-monotonic or inconsistent sample sequence")
    # both interval endpoints must belong to the selected game-frame range
    selected = [b for a, b in zip(rows, rows[1:])
                if low <= a["observed_game_frame"] <= high and low <= b["observed_game_frame"] <= high]
    values = sorted(r["interval_us"] for r in selected)

    def percentile(p):
        return values[max(0, math.ceil(len(values) * p) - 1)] / 1000 if values else None
    wall = sum(r["interval_us"] for r in selected)
    return {"pid": pid, "nonce": nonce, "capture": int(capture), "capture_samples": len(rows),
            "capacity_stop": bool(int(cap)), "selected_intervals": len(values), "selected_game_frames": [low, high],
            "interval_ms": {"p50": percentile(.50), "p95": percentile(.95), "p99": percentile(.99),
                            "max": max(values) / 1000 if values else None},
            "picture_fps": len(values) * 1e6 / wall if wall else None, "wall_us": wall,
            "sha256": hashlib.sha256(raw).hexdigest(),
            "percentile_method": "nearest rank, each selected picture interval counted once",
            "measurement": "CPU return from swap for new game pictures; includes scheduling/queue waits; "
                           "not scanout, GPU pass duration or input latency"}


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("trace", type=Path)
    p.add_argument("--log", type=Path, required=True)
    p.add_argument("--min-frame", type=int, default=0)
    p.add_argument("--max-frame", type=int, default=2**32 - 1)
    p.add_argument("--prefix", default="vpk-presenttrace")
    p.add_argument("--output", type=Path)
    a = p.parse_args()
    try:
        report = analyze(a.trace.read_bytes(), a.log.read_text(errors="replace"), a.min_frame, a.max_frame, a.prefix)
    except (ValueError, KeyError, TypeError, UnicodeError) as exc:
        p.error(str(exc))
    if a.output:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
