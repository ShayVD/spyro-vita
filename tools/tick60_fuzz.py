#!/usr/bin/env python3
"""Differential fuzz of the 60 Hz logic against 30 Hz (decisions D160, issue #148).

The same random pad script plays from the same Cliff Town save state at 30 Hz and
at 60 Hz (--tick60); Spyro's state sequence from each run's --trace is compared.
What the 60 Hz rules must match is the original's cadence: a state change at most
once a 30 Hz tick, so a jump re-entered faster than every 4 vblanks (a tick in the
jump, a tick on the ground) is the "machine gun" of issue #148, and more jump
entries than the 30 Hz run shows a hop the original didn't make.

  tools/tick60_fuzz.py base DIR          one run per mode that saves a state at Cliff Town's play
  tools/tick60_fuzz.py gen DIR N [SEED]  N random scripts into DIR/scripts
  tools/tick60_fuzz.py run DIR [N]       both modes per script, from the state (about a minute each)
  tools/tick60_fuzz.py report DIR        the comparison, worst first

Run from the repo root; the game binary is build/linux32/src/platform/linux32/spyro
under tools/tc. DIR is gitignored (logs/...)."""
import os, random, subprocess, sys

BIN = "build/linux32/src/platform/linux32/spyro"
LEVEL = int(os.environ.get("TICK60_FUZZ_LEVEL", "22"))
# the first play vblank: Cliff Town through the tour (newgame_host.txt, --tour 22 --tour-stay 120),
# or Artisans straight from the new game (TICK60_FUZZ_LEVEL=10: 6347, no tour)
PLAY_AT = int(os.environ.get("TICK60_FUZZ_PLAY_AT", "6869" if LEVEL == 22 else "6347"))
SCRIPT_LEN = int(os.environ.get("TICK60_FUZZ_LEN", "600"))  # vblanks of random input after the anchor
JUMP_STATES = {5, 20, 44}  # jump, charge jump, supercharge jump


def sh(args, log):
    with open(log, "w") as out:
        return subprocess.run(["tools/tc"] + args, stdout=out, stderr=subprocess.STDOUT).returncode


def base(d):
    for mode in ("30", "60"):
        run = f"{d}/base{mode}"
        os.makedirs(f"{run}/save", exist_ok=True)
        args = [BIN, "--disc", "disc/files", "--pad", "tools/oracle/pads/newgame_host.txt",
                "--frames", str(PLAY_AT + 11)]
        if LEVEL != 10:
            args += ["--tour", str(LEVEL), "--tour-stay", "120"]
        args += [
                "--save", f"{run}/save", "--state", f"{PLAY_AT}:save:1",
                "--trace", f"{run}/trace.tsv", "--log", f"{run}/spyro.log", "--shot-dir", run]
        if mode == "60":
            args.append("--tick60")
        print(mode, "exit", sh(args, f"{run}/stdout.txt"))


DIRS = ["up", "down", "left", "right", "up+left", "up+right", "down+left", "down+right"]


def gen(d, n, seed=1):
    os.makedirs(f"{d}/scripts", exist_ok=True)
    rng = random.Random(seed)
    for i in range(n):
        lines = [f"# tick60 fuzz {i} seed {seed}"]
        t = 150
        direction = rng.choice(DIRS)
        while t < 150 + SCRIPT_LEN:
            if rng.random() < 0.3:
                direction = rng.choice(DIRS)
            kind = rng.choice(["run", "run", "charge", "charge", "charge+hold", "charge+tap", "jump+hold", "jump+tap",
                               "glide", "flame", "look", "stop"])
            length = rng.randint(6, 40)
            if kind == "run":
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}")
            elif kind == "charge":
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+square")
            elif kind == "charge+hold":
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+square+cross")
            elif kind == "charge+tap":
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+square")
                for k in range(0, length, rng.randint(8, 24)):
                    lines.append(f"@gs=0,level={LEVEL} +{t + k} {direction}+square+cross")
                    lines.append(f"@gs=0,level={LEVEL} +{t + k + 3} {direction}+square")
            elif kind == "jump+hold":
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+cross")
            elif kind == "jump+tap":
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}")
                for k in range(0, length, rng.randint(8, 30)):
                    lines.append(f"@gs=0,level={LEVEL} +{t + k} {direction}+cross")
                    lines.append(f"@gs=0,level={LEVEL} +{t + k + 3} {direction}")
            elif kind == "glide":  # a jump, then cross again near the apex
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+cross")
                lines.append(f"@gs=0,level={LEVEL} +{t + 3} {direction}")
                lines.append(f"@gs=0,level={LEVEL} +{t + rng.randint(14, 24)} {direction}+cross")
                length = max(length, 40)
            elif kind == "flame":
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+circle")
            elif kind == "look":
                lines.append(f"@gs=0,level={LEVEL} +{t} triangle")
            else:
                lines.append(f"@gs=0,level={LEVEL} +{t} none")
            t += length
        lines.append(f"@gs=0,level={LEVEL} +{t} none")
        with open(f"{d}/scripts/fuzz{i:03d}.txt", "w") as f:
            f.write("\n".join(lines) + "\n")
    print(n, "scripts in", f"{d}/scripts")


def run(d, n=None):
    scripts = sorted(os.listdir(f"{d}/scripts"))
    if n is not None:
        scripts = scripts[:n]
    for s in scripts:
        name = s[:-4]
        for mode in ("30", "60"):
            out = f"{d}/{name}-{mode}"
            if os.path.exists(f"{out}/trace.tsv"):
                continue
            os.makedirs(out, exist_ok=True)
            args = [BIN, "--disc", "disc/files", "--pad", f"{d}/scripts/{s}",
                    "--save", f"{d}/base{mode}/save", "--state", "1:load:1",
                    "--frames", str(PLAY_AT + 150 + SCRIPT_LEN + 60),
                    "--trace", f"{out}/trace.tsv", "--log", f"{out}/spyro.log", "--shot-dir", out]
            if mode == "60":
                args.append("--tick60")
            print(name, mode, "exit", sh(args, f"{out}/stdout.txt"), flush=True)


def states(path):
    rows = []
    with open(path) as f:
        for line in f:
            c = line.split("\t")
            if c[0] == "vblank":
                continue
            rows.append((int(c[0]), int(c[1]), int(c[2]), int(c[7])))
    return [(v, st) for v, gs, lv, st in rows if gs == 0 and lv == LEVEL and v >= PLAY_AT]


def run_lengths(seq):
    """[(vblank, state, vblanks in it)] from (vblank, state) rows"""
    r = []
    for v, st in seq:
        if r and r[-1][1] == st:
            r[-1][2] += 1
        else:
            r.append([v, st, 1])
    return r


def report(d):
    """Per script: jump entries from the ground at each rate, the fastest such re-entry,
    the 60 Hz states that lasted an odd number of vblanks under 4 (the original's
    states last whole ticks: at 60 Hz a 1- or 3-vblank state is a change the 30 Hz
    rules never make; the machine gun of issue #148 was states of 1 and 2 vblanks
    alternating), and the run-length histograms of both rates."""
    rows, hist = [], {"30": {}, "60": {}}
    for s in sorted(os.listdir(f"{d}/scripts")):
        name = s[:-4]
        try:
            a, b = states(f"{d}/{name}-30/trace.tsv"), states(f"{d}/{name}-60/trace.tsv")
        except FileNotFoundError:
            continue
        ra, rb = run_lengths(a), run_lengths(b)
        for m, r in (("30", ra), ("60", rb)):
            for v, st, n in r[1:-1]:
                k = min(n, 8)
                hist[m][k] = hist[m].get(k, 0) + 1
        odd = [(v, prev[1], st, n) for prev, (v, st, n) in zip(rb, rb[1:-1]) if n % 2 == 1 and n < 4]
        ea = [v for (v, st, n), prev in zip(ra[1:], ra) if st in JUMP_STATES and prev[1] not in JUMP_STATES]
        eb = [v for (v, st, n), prev in zip(rb[1:], rb) if st in JUMP_STATES and prev[1] not in JUMP_STATES]
        ga = min((ea[i + 1] - ea[i] for i in range(len(ea) - 1)), default=0)
        gb = min((eb[i + 1] - eb[i] for i in range(len(eb) - 1)), default=0)
        rows.append((len(odd) * 10 + abs(len(eb) - len(ea)), name, len(ea), len(eb), ga, gb, odd[:4]))
    rows.sort(reverse=True)
    print("script    jumps from the ground 30/60   fastest re-entry 30/60   60 Hz states of 1 or 3 vblanks (vblank, from, state, length)")
    for score, name, na, nb, ga, gb, odd in rows:
        print(f"{name}        {na:3d} / {nb:3d}                 {ga:3d} / {gb:3d}            {odd}")
    print("state-run lengths (vblanks, 8 = 8 or more):")
    for m in ("30", "60"):
        print(f"  {m} Hz:", " ".join(f"{k}:{hist[m][k]}" for k in sorted(hist[m])))


if __name__ == "__main__":
    cmd, d = sys.argv[1], sys.argv[2]
    if cmd == "base":
        base(d)
    elif cmd == "gen":
        gen(d, int(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 1)
    elif cmd == "run":
        run(d, int(sys.argv[3]) if len(sys.argv) > 3 else None)
    elif cmd == "report":
        report(d)
    else:
        sys.exit(__doc__)
