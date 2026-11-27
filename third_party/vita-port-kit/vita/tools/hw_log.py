#!/usr/bin/env python3
"""Tables from a hardware log's per-window lines, so nobody rewrites a scratch script per run.
  hw_log.py pics LOG [--frames A-B] [--format FILE] [--scene-re R] [--pics-re R] [--shown-re R] [--json]
      pictures per second per scene (room, level, map): windows, mean, min, max
  hw_log.py summary LOG [LOG ...] [--vs LOG ...] [--frames A-B] [--drop-loads] [--timing-tol N] [--format FILE] [...]
                   [--json]
      one row for a frame range (a console A/B run): pictures/s, the worst window, each thread's mean ms a window
      (the 'game / gs worker / GPU ms' columns of a console results table), each format metric's mean and max a
      window, and the run's timing (the frame of each scene change); the window time comes from the log.
      Several logs: one row per run, the runs grouped by timing (same scenes in the same order, each change within
      --timing-tol frames, default one window), and a warning when they span timings. --vs LOG ... is the B side
      (the first logs are A): each timing's A and B means, and a warning when the A/B spans timings (kb
      realtime-pad-route-splits-into-timings: compare runs of one pad route only within one timing)
  hw_log.py windows LOG [--frames A-B] [--start-re R] [--end-re R] [--window-ms 1000] [--format FILE] [...] [--json]
      one row per window: scene, pictures/s, the bound label, each thread's and each core's load (% of the window);
      --start-re / --end-re keep only the windows from a start line to an end line (a fight, a cutscene)
  hw_log.py sym ADDR ... --elf ELF --image-base HEX [--elf-base 0x81000000] [--addr2line arm-vita-eabi-addr2line]
      a logged address to function and line: ADDR - image base + the ELF's base, through addr2line

A window is one watchdog threads line (targets/vita/runtime/watchdog vpk_threads_line: 'NAME N ms (core C, mask M, prio P,
K preempted)', comma-separated; a NAME may have spaces, 'gs worker'), one pictures line and one bound line
(targets/vita/runtime/bound '[LABEL]'), whichever come; a kind seen twice starts the next window, and a shown-count line (below) always starts one: the lines after
it, up to the next, are its window (print a window's header before its other lines; kh1-vita #252).

Pictures come from one of two line shapes:
  - --shown-re (default 'watchdog T ms: frame F, ... K shown', kh1-vita's watchdog line): groups 'ms' (a clock),
    'frame' and 'shown' (a running count of pictures shown). Pictures/s is the shown count's step over the clock's
    step since the last such line; the first line only sets the start. 'frame' is the window's frame for --frames.
  - --pics-re (default the bound line's 'N ms/picture'): group 'fps' is pictures/s, else group 1 or 'ms' is ms per
    picture.
--frames A-B keeps the windows whose frame is in [A, B] (windows with no frame are dropped when it is given).
  - a format file's "window_re": a line that closes its window (the lines before it, back to the last one, are
    that window's). Named groups: 'frame' (the window's frame or vblank, for --frames), 'ms' (the window's clock),
    'pictures' (pictures shown in the window; pictures/s is pictures over ms); every other named group is a metric
    of the window ('late' in 'N vblanks late'). spyro-vita's 'vita vblank N: 300 vblanks in T ms (...), P pictures,
    L vblanks late' is this shape.
--frames A-B keeps the windows whose frame is in [A, B] (windows with no frame are dropped when it is given).
--format FILE is a JSON object with any of "scene_re", "pics_re", "shown_re", "window_re", "metrics" (a game repo
keeps its own as tools/vita/hw_log.json, so nobody passes the regexes by hand); flags given on the command line win.
"metrics" is {name: regex}: the regex's 'value' group (else group 1) is a number of a window; with a 'frame' group
it goes to the window of that frame (spyro-vita's 'vita time N: ... busy B%'), without one to the last window.
The scene is the last --scene-re match before the window (default: 'room=', 'scene=', 'level=' or 'map=' and the
word after it; a 'scene' group, else group 1); '-' before any. A scene regex with an 'at' group (the frame the line
was logged at, spyro-vita's '[  2108]') places each change at that frame: a window's scene is then the last change at
or before its frame, and a window with a scene line inside it is a load (--drop-loads leaves loads out, as the
game's own per-level readers do). The run's timing is each change of scene with its frame (a scene line without an
'at' group is placed at the frame of the window it falls in).
Exit 0; 2 when the log cannot be read or has no window (in the frame range); 1 when sym's addr2line fails.
Counters (vpk-perfcount lines) are perf_report.py's; the present trace's intervals are present_trace.py's.
"""
import argparse
import json
import re
import subprocess
import sys

SCENE_RE = r"\b(?:room|scene|level|map)[=:]\s*(\S+)"
PICS_RE = r"([\d.]+) ms/picture"
SHOWN_RE = r"watchdog (?P<ms>\d+) ms: frame (?P<frame>\d+),.*?(?P<shown>\d+) shown"
# a name after the line's head ('threads: ', 'threads ') or a ', ' separator; names may hold spaces
THREAD = re.compile(r"(?:threads:? |, )([\w.-]+(?: [\w.-]+)*?) (\d+) ms \(core (-?\d+)")
LABEL = re.compile(r"\[([A-Za-z-]+)\]")


def number(v):
    return float(v) if "." in v else int(v)


def parse(path, scene_re=SCENE_RE, pics_re=PICS_RE, shown_re=SHOWN_RE, window_re=None, metrics=None, changes=None):
    """the log's windows; changes, when a list, gets the run's timing: (frame, scene) for each change of scene"""
    scene_p, pics_p = re.compile(scene_re), re.compile(pics_re)
    shown_p = re.compile(shown_re) if shown_re else None
    window_p = re.compile(window_re) if window_re else None
    metric_ps = [(k, re.compile(v)) for k, v in (metrics or {}).items()]
    wins, cur, scene, last = [], {}, "-", None
    hits, by_frame, pending = [], {}, {}  # scene lines (at, line, scene); windows by frame; metrics before theirs

    def put(kind, value, n):
        nonlocal cur
        if kind in cur:
            wins.append(cur)
            cur = {}
        if not cur:
            cur.update(scene=scene, line=n)
        cur[kind] = value

    with open(path, errors="replace") as f:
        for n, line in enumerate(f, 1):
            m = scene_p.search(line)
            if m:
                g = m.groupdict()
                scene = g["scene"] if "scene" in g else m.group(1)
                hits.append((int(g["at"]) if g.get("at") else None, n, scene))
            for k, p in metric_ps:
                m = p.search(line)
                if m:
                    g = m.groupdict()
                    v = number(g["value"] if "value" in g else m.group(1))
                    if g.get("frame") is not None:
                        f = int(g["frame"])
                        (by_frame[f]["metrics"] if f in by_frame else pending.setdefault(f, {}))[k] = v
                    elif wins:
                        wins[-1].setdefault("metrics", {})[k] = v
            m = window_p.search(line) if window_p else None
            if m:  # the window line closes its window
                g = {k: v for k, v in m.groupdict().items() if v is not None}
                if not cur:
                    cur.update(line=n)
                cur["scene"] = scene
                cur["metrics"] = {k: number(v) for k, v in g.items() if k not in ("frame", "ms")}
                if "ms" in g:
                    cur["ms"] = number(g["ms"])
                if "pictures" in g and cur.get("ms"):
                    cur["pics"] = 1000.0 * number(g["pictures"]) / cur["ms"]
                if "frame" in g:
                    cur["frame"] = int(g["frame"])
                    by_frame[cur["frame"]] = cur
                    cur["metrics"].update(pending.pop(cur["frame"], {}))
                wins.append(cur)
                cur = {}
                continue
            m = shown_p.search(line) if shown_p else None
            if m:
                t, k = int(m.group("ms")), int(m.group("shown"))
                g = m.groupdict()
                if cur:  # the shown line heads its window: the lines after it are that window's
                    wins.append(cur)
                    cur = {}
                if last and t > last[0]:
                    put("pics", 1000.0 * (k - last[1]) / (t - last[0]), n)
                    cur["ms"] = t - last[0]
                    if g.get("frame") is not None:
                        cur["frame"] = int(g["frame"])
                last = (t, k)
                continue
            m = pics_p.search(line)
            if m:
                g = m.groupdict()
                if g.get("fps") is not None:
                    put("pics", float(g["fps"]), n)
                else:
                    ms = float(g["ms"] if g.get("ms") is not None else m.group(1))
                    put("pics", 1000.0 / ms if ms else 0.0, n)
            ts = THREAD.findall(line) if "ms (core" in line else []
            if ts:
                put("threads", {t: (int(ms), int(c)) for t, ms, c in ts}, n)
            m = LABEL.search(line)
            if m and (m.group(1).endswith("-bound") or m.group(1) in ("on-target", "unknown")):
                put("label", m.group(1), n)
    if cur:
        wins.append(cur)
    place(wins, hits, changes)
    return wins


def place(wins, hits, changes):
    """scene lines with an 'at' frame name their windows' scenes and mark the loads; the run's timing into changes"""
    fs = [w["frame"] for w in wins if "frame" in w]
    step = window_step(wins)
    timed = [(at, s) for at, _, s in hits if at is not None]
    if timed and fs:
        prev = None
        for w in wins:
            if "frame" not in w:
                continue
            f = w["frame"]
            lo = prev if prev is not None else f - (step or 0)
            before = [s for at, s in timed if at <= f]
            if before:
                w["scene"] = before[-1]
            w["load"] = any(lo < at <= f for at, _ in timed)
            prev = f
    if changes is None:
        return
    last = None
    for at, n, s in hits:
        if at is None:  # the frame of the window this line falls in: the first with a frame from this line on
            at = next((w["frame"] for w in wins if "frame" in w and w["line"] >= n), None)
            if at is None:
                at = next((w["frame"] for w in reversed(wins) if "frame" in w), None)
        if s != last:
            changes.append((at, s))
            last = s


def window_step(wins):
    """a window's length in frames: the median step between windows' frames (None with fewer than two)"""
    fs = [w["frame"] for w in wins if "frame" in w]
    d = sorted(b - a for a, b in zip(fs, fs[1:]) if b > a)
    return d[len(d) // 2] if d else None


def pics(wins):
    out = {}
    for w in wins:
        if "pics" in w:
            out.setdefault(w["scene"], []).append(w["pics"])
    return [{"scene": s, "windows": len(v), "mean": sum(v) / len(v), "min": min(v), "max": max(v)}
            for s, v in out.items()]


def frames(wins, rng):
    """the windows whose frame is in rng = (lo, hi); None keeps all"""
    if not rng:
        return wins
    return [w for w in wins if "frame" in w and rng[0] <= w["frame"] <= rng[1]]


def summary(wins):
    """pictures/s over the range (shown over time when the log has a clock, else the windows' mean), the worst
    window, and each thread's mean ms a window"""
    ps = [w for w in wins if "pics" in w]
    timed = [w for w in ps if w.get("ms")]
    if timed:
        rate = sum(w["pics"] * w["ms"] for w in timed) / sum(w["ms"] for w in timed)
    else:
        rate = sum(w["pics"] for w in ps) / len(ps) if ps else None
    th = {}
    for w in wins:
        for t, (ms, _) in w.get("threads", {}).items():
            th.setdefault(t, []).append(ms)
    ms = {}
    for w in wins:
        for k, v in w.get("metrics", {}).items():
            ms.setdefault(k, []).append(v)
    fr = [w["frame"] for w in wins if "frame" in w]
    return {"windows": len(wins), "frames": [min(fr), max(fr)] if fr else None, "pics": rate,
            "worst": min((w["pics"] for w in ps), default=None),
            "threads": {t: sum(v) / len(v) for t, v in th.items()},
            "metrics": {k: {"mean": sum(v) / len(v), "max": max(v)} for k, v in ms.items()}}


def same_timing(a, b, tol):
    """two runs' timings (lists of (frame, scene)) are one timing: the same scenes in order, each change within tol"""
    if [s for _, s in a] != [s for _, s in b]:
        return False
    return all(x is None or y is None or abs(x - y) <= tol for (x, _), (y, _) in zip(a, b))


def group_timings(runs, tol):
    """runs: [(name, timing)]; the groups, each [names], in first-seen order (a run joins the first group whose
    first run it matches)"""
    groups = []
    for name, t in runs:
        for g in groups:
            if same_timing(g["timing"], t, tol):
                g["runs"].append(name)
                break
        else:
            groups.append({"timing": t, "runs": [name]})
    return groups


def timing_text(t):
    return ", ".join(f"{s} {'-' if f is None else f}" for f, s in t) or "-"


def labels(paths):
    """short names for runs: the file names, or their folders' when the file names repeat (logs/hw-ap23t1/spyro.log)"""
    import os
    for f in (os.path.basename, lambda p: os.path.basename(os.path.dirname(os.path.abspath(p))), str):
        names = [f(p) for p in paths]
        if len(set(names)) == len(names):
            return names
    return [str(p) for p in paths]


def mean(xs):
    xs = [x for x in xs if x is not None]
    return sum(xs) / len(xs) if xs else None


def frame_range(v):
    m = re.fullmatch(r"(\d+)-(\d+)", v.replace(",", ""))
    if not m or int(m.group(1)) > int(m.group(2)):
        raise argparse.ArgumentTypeError("--frames wants A-B with A <= B")
    return int(m.group(1)), int(m.group(2))


def span(wins, start_re=None, end_re=None, path=None):
    """the windows from the first start line to the next end line (line numbers from the log)"""
    if not start_re:
        return wins
    lo, hi = None, None
    with open(path, errors="replace") as f:
        for n, line in enumerate(f, 1):
            if lo is None and re.search(start_re, line):
                lo = n
            elif lo is not None and end_re and re.search(end_re, line):
                hi = n
                break
    if lo is None:
        return []
    return [w for w in wins if w["line"] >= lo and (hi is None or w["line"] <= hi)]


def loads(w, window_ms):
    """each thread's and each core's % of the window: the window's own clock step when the log has one"""
    window_ms = w.get("ms") or window_ms
    th = w.get("threads", {})
    cores = {}
    for ms, c in th.values():
        cores[c] = cores.get(c, 0) + ms
    pct = lambda ms: 100.0 * ms / window_ms
    return {t: pct(ms) for t, (ms, _) in th.items()}, {c: pct(ms) for c, ms in sorted(cores.items())}


def sym(addrs, elf, image_base, elf_base=0x81000000, addr2line="arm-vita-eabi-addr2line"):
    xs = [(int(a, 16) & ~1) - image_base + elf_base for a in addrs]
    p = subprocess.run([addr2line, "-f", "-C", "-p", "-e", elf] + ["0x%x" % x for x in xs], capture_output=True,
                       text=True)
    if p.returncode:
        raise RuntimeError(p.stderr.strip() or f"{addr2line} exit {p.returncode}")
    return [(a, "0x%x" % x, s) for a, x, s in zip(addrs, xs, p.stdout.splitlines())]


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("pics", "summary", "windows"):
        s = sub.add_parser(name)
        s.add_argument("log", nargs="+" if name == "summary" else None)
        s.add_argument("--format", help="JSON with scene_re / pics_re / shown_re / window_re / metrics")
        s.add_argument("--scene-re")
        s.add_argument("--pics-re")
        s.add_argument("--shown-re", help="'' turns the shown-count line off")
        s.add_argument("--frames", type=frame_range, metavar="A-B")
        s.add_argument("--json", action="store_true")
        s.add_argument("--drop-loads", action="store_true", help="leave out windows with a scene line inside")
        if name == "summary":
            s.add_argument("--vs", nargs="+", default=[], metavar="LOG", help="the B side of an A/B")
            s.add_argument("--timing-tol", type=int, help="frames a scene change may move within one timing")
        if name == "windows":
            s.add_argument("--start-re")
            s.add_argument("--end-re")
            s.add_argument("--window-ms", type=float, default=1000.0)
    s = sub.add_parser("sym")
    s.add_argument("addrs", nargs="+")
    s.add_argument("--elf", required=True)
    s.add_argument("--image-base", required=True, type=lambda v: int(v, 16))
    s.add_argument("--elf-base", default=0x81000000, type=lambda v: int(v, 16))
    s.add_argument("--addr2line", default="arm-vita-eabi-addr2line")
    a = ap.parse_args(argv)

    if a.cmd == "sym":
        try:
            rows = sym(a.addrs, a.elf, a.image_base, a.elf_base, a.addr2line)
        except (OSError, RuntimeError) as e:
            print(f"hw_log: {e}", file=sys.stderr)
            return 1
        for addr, elf_addr, where in rows:
            print(f"{addr}  {elf_addr}  {where}")
        return 0
    try:
        fmt = {}
        if a.format:
            with open(a.format) as f:
                fmt = json.load(f)
            if not isinstance(fmt, dict) or not isinstance(fmt.get("metrics", {}), dict):
                raise ValueError(f"{a.format}: not a JSON object (with \"metrics\" an object of regexes)")
        pick = lambda flag, key, dflt: flag if flag is not None else fmt.get(key, dflt)
        opts = dict(scene_re=pick(a.scene_re, "scene_re", SCENE_RE), pics_re=pick(a.pics_re, "pics_re", PICS_RE),
                    shown_re=pick(a.shown_re, "shown_re", SHOWN_RE), window_re=fmt.get("window_re"),
                    metrics=fmt.get("metrics"))
        logs = a.log if a.cmd == "summary" else [a.log]
        vs = a.vs if a.cmd == "summary" else []
        runs = []
        for path in logs + vs:
            ch = []
            wins = parse(path, changes=ch, **opts)
            step = window_step(wins)
            wins = frames(wins, a.frames)
            if a.drop_loads:
                wins = [w for w in wins if not w.get("load")]
            if a.cmd == "windows":
                wins = span(wins, a.start_re, a.end_re, path)
            runs.append({"path": path, "wins": wins, "timing": ch, "step": step})
    except (OSError, ValueError, re.error) as e:
        print(f"hw_log: {e}", file=sys.stderr)
        return 2
    for r in runs:
        if not r["wins"]:
            print(f"hw_log: no windows in {r['path']}" + (" in frames %d-%d" % a.frames if a.frames else ""),
                  file=sys.stderr)
            return 2
    num = lambda v: "-" if v is None else f"{v:.1f}"
    if a.cmd == "summary" and len(runs) == 1:
        r = summary(runs[0]["wins"])
        r["timing"] = runs[0]["timing"]
        if a.json:
            print(json.dumps(r, indent=1))
            return 0
        fr = "-" if not r["frames"] else f"{r['frames'][0]}-{r['frames'][1]}"
        print(f"frames {fr}, {r['windows']} windows: {num(r['pics'])} pictures/s, worst window {num(r['worst'])}")
        for t, ms in r["threads"].items():
            print(f"  {t:16} {ms:7.0f} ms a window")
        for k, v in r["metrics"].items():
            print(f"  {k:16} {v['mean']:7.2f} a window (max {v['max']:g})")
        if r["timing"]:
            print(f"  timing: {timing_text(r['timing'])}")
        return 0
    if a.cmd == "summary":
        return summary_runs(a, runs, num)
    wins = runs[0]["wins"]
    if a.cmd == "pics":
        rows = pics(wins)
        if a.json:
            print(json.dumps(rows, indent=1))
        else:
            print(f"{'scene':24} {'windows':>7} {'mean':>7} {'min':>7} {'max':>7}")
            for r in rows:
                print(f"{r['scene']:24} {r['windows']:7} {r['mean']:7.1f} {r['min']:7.1f} {r['max']:7.1f}")
        return 0
    rows = []
    for i, w in enumerate(wins):
        th, co = loads(w, a.window_ms)
        rows.append({"window": i, "line": w["line"], "frame": w.get("frame"), "scene": w["scene"], "pics": w.get("pics"),
                     "label": w.get("label"), "threads": th, "cores": co, "metrics": w.get("metrics", {}),
                     "load": w.get("load", False)})
    if a.json:
        print(json.dumps(rows, indent=1))
        return 0
    names = sorted({t for r in rows for t in r["threads"]})
    cores = sorted({c for r in rows for c in r["cores"]})
    mets = list(dict.fromkeys(k for r in rows for k in r["metrics"]))
    head = ["win", "line", "frame", "scene", "pics/s", "bound"] + names + [f"core{c}" for c in cores] + mets
    print("  ".join(head))
    for r in rows:
        cells = [str(r["window"]), str(r["line"]), "-" if r["frame"] is None else str(r["frame"]),
                 r["scene"] + (" (load)" if r["load"] else ""), "-" if r["pics"] is None else f"{r['pics']:.1f}",
                 r["label"] or "-"]
        cells += ["-" if t not in r["threads"] else f"{r['threads'][t]:.0f}%" for t in names]
        cells += ["-" if c not in r["cores"] else f"{r['cores'][c]:.0f}%" for c in cores]
        cells += ["-" if k not in r["metrics"] else f"{r['metrics'][k]:g}" for k in mets]
        print("  ".join(cells))
    return 0


def summary_runs(a, runs, num):
    """several runs: a row each, grouped by timing; with --vs, each timing's A and B means"""
    names = labels([r["path"] for r in runs])
    n_a = len(a.log)
    for i, (r, name) in enumerate(zip(runs, names)):
        r.update(summary(r["wins"]), name=name, side=("A" if i < n_a else "B") if a.vs else None)
    tol = a.timing_tol if a.timing_tol is not None else max((r["step"] or 0 for r in runs), default=0)
    groups = group_timings([(r["name"], r["timing"]) for r in runs], tol)
    for k, g in enumerate(groups, 1):
        for r in runs:
            if r["name"] in g["runs"]:
                r["group"] = k
    threads = list(dict.fromkeys(t for r in runs for t in r["threads"]))
    mets = list(dict.fromkeys(m for r in runs for m in r["metrics"]))
    cols = [("pics/s", lambda r: r["pics"]), ("worst", lambda r: r["worst"])]
    cols += [(m, lambda r, m=m: r["metrics"][m]["mean"] if m in r["metrics"] else None) for m in mets]
    cols += [(t + " ms", lambda r, t=t: r["threads"].get(t)) for t in threads]
    warnings, abs_ = [], []
    if len(groups) > 1 and not a.vs:
        warnings.append(f"the runs span {len(groups)} timings: compare runs only within one "
                        "(kb realtime-pad-route-splits-into-timings)")
    if a.vs:
        for k, g in enumerate(groups, 1):
            side = {s: [r for r in runs if r["group"] == k and r["side"] == s] for s in "AB"}
            if side["A"] and side["B"]:
                abs_.append({"timing": k, "A": [r["name"] for r in side["A"]], "B": [r["name"] for r in side["B"]],
                             "means": {c: [mean(f(r) for r in side[s]) for s in "AB"] for c, f in cols}})
            else:
                only = "A" if side["A"] else "B"
                warnings.append(f"timing {k} holds only {only} ({' '.join(r['name'] for r in side[only])}): "
                                "no A/B there")
        if not abs_:
            warnings.append("the A/B spans timings: no timing holds both sides, so A against B mixes timings "
                            "(kb realtime-pad-route-splits-into-timings); rerun until one timing holds both")
        elif len(groups) > 1:
            warnings.append("the A/B spans timings: compare A and B within a timing (above), never across")
    if a.json:
        keep = ("name", "path", "side", "group", "windows", "frames", "pics", "worst", "threads", "metrics", "timing")
        print(json.dumps({"runs": [{k: r[k] for k in keep} for r in runs],
                          "timings": [{"timing": g["timing"], "runs": g["runs"]} for g in groups],
                          "ab": abs_, "warnings": warnings}, indent=1))
        return 0
    w = max(len("run"), *(len(r["name"]) for r in runs))
    head = f"{'run':{w}} " + ("side " if a.vs else "") + f"{'timing':>6} {'frames':>13} {'win':>4} "
    print(head + " ".join(f"{c:>9}" for c, _ in cols))
    for r in runs:
        fr = "-" if not r["frames"] else f"{r['frames'][0]}-{r['frames'][1]}"
        line = f"{r['name']:{w}} " + (f"{r['side']:4} " if a.vs else "")
        line += f"{r['group']:>6} {fr:>13} {r['windows']:>4} "
        print(line + " ".join(f"{'-' if f(r) is None else format(f(r), '.2f'):>9}" for _, f in cols))
    for k, g in enumerate(groups, 1):
        print(f"timing {k} ({' '.join(g['runs'])}): {timing_text(g['timing'])}")
    for ab in abs_:
        print(f"timing {ab['timing']}: A ({' '.join(ab['A'])}) against B ({' '.join(ab['B'])})")
        for c, (x, y) in ab["means"].items():
            d = "-" if x is None or y is None else f"{y - x:+.2f}"
            v = lambda x: "-" if x is None else f"{x:.2f}"
            print(f"  {c:16} A {v(x):>8}  B {v(y):>8}  B-A {d}")
    for m in warnings:
        print(f"warning: {m}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
