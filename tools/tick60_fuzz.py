#!/usr/bin/env python3
"""Differential fuzz of the 60 Hz logic against 30 Hz (decisions D160, issue #148).

The same random pad script plays from the same Cliff Town save state at 30 Hz and
at 60 Hz (--tick60); Spyro's state sequence from each run's --trace is compared.
What the 60 Hz rules must match is the original's cadence: a state change at most
once a 30 Hz tick, so a jump re-entered faster than every 4 vblanks (a tick in the
jump, a tick on the ground) is the "machine gun" of issue #148, and more jump
entries than the 30 Hz run shows a hop the original didn't make.

  tools/tick60_fuzz.py base DIR                one run per mode that saves a state at Cliff Town's play
  tools/tick60_fuzz.py gen DIR N [SEED] [SET]  N random scripts into DIR/scripts
  tools/tick60_fuzz.py run DIR [N]             both modes per script, from the state (about a minute each)
  tools/tick60_fuzz.py report DIR              the comparison, worst first

SET is "moves" (the default: runs, charges, jumps, glides), "hits" or "super". They
cover what Spyro's once-a-tick transitions must not lose (D160's review before the merge):
  hits   a one-shot hit poked into g_Spyro.m_DamageFlags on a random vblank (either
         half of a 30 Hz tick), then cross tapped through the hurt state and the pad
         lockout after it. Every hit must be taken at both rates, and no jump may
         start inside the lockout.
  super  a supercharge (the surface's flag poked every vblank, square held), then
         cross held: the jump out of it comes after the same hold at both rates. In
         Tree Tops, whose Spyro has the animation: TICK60_FUZZ_LEVEL=43
         TICK60_FUZZ_PLAY_AT=6869 on every command of that DIR.

Run from the repo root; the game binary is build/linux32/src/platform/linux32/spyro
(TICK60_FUZZ_BIN: another) under tools/tc. DIR is gitignored (logs/...)."""
import os, random, subprocess, sys

# another build takes its own DIR: a state loads only into the binary that made it
BIN = os.environ.get("TICK60_FUZZ_BIN", "build/linux32/src/platform/linux32/spyro")
LEVEL = int(os.environ.get("TICK60_FUZZ_LEVEL", "22"))
# the first play vblank: Cliff Town through the tour (newgame_host.txt, --tour 22 --tour-stay 120),
# or Artisans straight from the new game (TICK60_FUZZ_LEVEL=10: 6347, no tour)
PLAY_AT = int(os.environ.get("TICK60_FUZZ_PLAY_AT", "6869" if LEVEL == 22 else "6347"))
SCRIPT_LEN = int(os.environ.get("TICK60_FUZZ_LEN", "600"))  # vblanks of random input after the anchor
JUMP_STATES = {5, 20, 24}  # jump, charge jump, supercharge jump (44 is the supercharge itself)
HURT_STATES = {7, 14, 22, 25, 27, 28}  # HandleSpyroDamage's hurt states (pete.c)
# g_Spyro's fields the hits and super sets poke (the US layout: g_Spyro is 0x80078a58)
SPYRO = 0x80078A58
DAMAGE_FLAGS = SPYRO + 0x2C
HIGHEST_POINT = SPYRO + 0x158  # m_highestFlightPoint: the supercharge's speed comes from the drop below it
INVULNERABLE = SPYRO + 0x160
HEALTH = SPYRO + 0x164
HIT = 1               # a plain hit: state 14 (the hits that throw him, 0x80, need the level's animation for it)
SUPERCHARGE = 0x2000  # the supercharge surface's flag (special_surfaces.c)
LOCKOUT = 24          # EndHurt's pad lockout: 12 ticks, in vblanks
SUPER_STATE, SUPER_JUMP = 44, 24
CROSS = 0x4000        # the host log's "pad: buttons" mask


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


def poke(t, addr, value):
    return f"@gs=0,level={LEVEL} +{t} poke:{addr:08x}={value & 0xFFFFFFFF:08x}"


def gen_hits(d, n, seed):
    """A hit every 200 vblanks or so, on a random vblank (a tick's first or its second), Spyro
    running or standing; then cross tapped for 80 vblanks: through the hurt state (24) and the
    lockout after it (24), where no tap may jump, and past them, where one does."""
    rng = random.Random(seed)
    for i in range(n):
        lines = [f"# tick60 fuzz hits {i} seed {seed}"]
        for t in range(150 + rng.randint(20, 60), 150 + SCRIPT_LEN - 100, 200):
            direction = rng.choice(DIRS + ["none"])
            lines.append(f"@gs=0,level={LEVEL} +{t - 20} {direction}")
            t += rng.randint(0, 40)
            # full health and no invulnerability left, so each poke is a hit by itself
            lines += [poke(t, HEALTH, 3), poke(t, INVULNERABLE, 0), poke(t, DAMAGE_FLAGS, HIT)]
            period, held = rng.randint(2, 5), rng.randint(1, 2)
            for k in range(rng.randint(1, 6), 80, max(period, held + 1)):
                lines.append(f"@gs=0,level={LEVEL} +{t + k} {direction}+cross".replace("none+", ""))
                lines.append(f"@gs=0,level={LEVEL} +{t + k + held} {direction}")
        lines.append(f"@gs=0,level={LEVEL} +{150 + SCRIPT_LEN} none")
        with open(f"{d}/scripts/hits{i:03d}.txt", "w") as f:
            f.write("\n".join(lines) + "\n")


def gen_super(d, n, seed):
    """A supercharge without its ramp: square held, the surface's flag poked every vblank, and
    the height its speed is measured from poked 4500 above Spyro (base30's last trace row), so
    the charge passes 0x3000 and becomes state 44. Then cross, held from a random vblank for a
    random time: the jump (24) comes after ten vblanks of hold, or at the release before that.
    Needs a level whose Spyro has the supercharge animation and room ahead of its start:
    TICK60_FUZZ_LEVEL=43 TICK60_FUZZ_PLAY_AT=6869, Tree Tops."""
    with open(f"{d}/base30/trace.tsv") as f:
        z = int(f.read().splitlines()[-1].split("\t")[6])
    rng = random.Random(seed)
    for i in range(n):
        lines = [f"# tick60 fuzz super {i} seed {seed}"]
        direction = "up"  # Tree Tops' start: the one way with room for the charge (a wall after ~30 vblanks the other ways)
        press = 150 + rng.randint(14, 44)
        release = press + rng.randint(3, 30)
        lines.append(f"@gs=0,level={LEVEL} +150 {direction}+square")
        for t in range(150, release + 60):
            lines.append(poke(t, DAMAGE_FLAGS, SUPERCHARGE))
            if t == 160:
                lines.append(poke(t, HIGHEST_POINT, z + 4500))
            if t == press:
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+square+cross")
            if t == release:
                lines.append(f"@gs=0,level={LEVEL} +{t} {direction}+square")
        lines.append(f"@gs=0,level={LEVEL} +{release + 60} none")
        with open(f"{d}/scripts/super{i:03d}.txt", "w") as f:
            f.write("\n".join(lines) + "\n")


def gen(d, n, seed=1, kind="moves"):
    os.makedirs(f"{d}/scripts", exist_ok=True)
    if kind != "moves":
        {"hits": gen_hits, "super": gen_super}[kind](d, n, seed)
        print(n, kind, "scripts in", f"{d}/scripts")
        return
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


def pad_log(path):
    """The run's log: the vblanks DAMAGE_FLAGS was poked with a hit, and [(vblank, mask)] of the pad"""
    pokes, buttons = [], []
    with open(path) as f:
        for line in f:
            if "pad: poke" in line:
                w = line.split()
                if int(w[w.index("poke") + 1], 16) == DAMAGE_FLAGS and int(w[w.index("=") + 1], 16) == HIT:
                    pokes.append(int(w[-1]))
            elif "pad: buttons" in line:
                buttons.append((int(line[1:line.index("]")]), int(line.split()[-1], 16)))
    return pokes, buttons


def report_hits(d, names):
    """Per script and rate: the poked hits, those not taken (no hurt state entered within 4
    vblanks: the transitions never saw the flags), and the jumps that began inside the pad
    lockout after a hurt state (within LOCKOUT vblanks of leaving it: a press the lockout
    should have blanked). Both must be 0 at both rates."""
    print("script    hits   not taken 30/60   jumps inside the lockout 30/60 (vblank, vblanks after the hurt state)")
    total = [0, 0, 0, 0]
    for name in names:
        cells, row = [], []
        for k, m in enumerate(("30", "60")):
            try:
                seq = states(f"{d}/{name}-{m}/trace.tsv")
                pokes, _ = pad_log(f"{d}/{name}-{m}/spyro.log")
            except FileNotFoundError:
                break
            r = run_lengths(seq)
            hurt = [v for (v, st, n), prev in zip(r[1:], r) if st in HURT_STATES and prev[1] not in HURT_STATES]
            missed = [p for p in pokes if not any(p <= v <= p + 4 for v in hurt)]
            ends = [v for (v, st, n), prev in zip(r[1:], r) if prev[1] in HURT_STATES and st not in HURT_STATES]
            early = [(v, v - e) for (v, st, n) in r for e in ends if st == 5 and 0 < v - e <= LOCKOUT]
            cells.append((len(pokes), missed, early))
            total[k] += len(missed)
            total[2 + k] += len(early)
        if len(cells) == 2:
            print(f"{name}   {cells[0][0]:3d}     {len(cells[0][1]):3d} / {len(cells[1][1]):3d}"
                  f"          {len(cells[0][2]):3d} / {len(cells[1][2]):3d}   "
                  f"{[m for m in cells[1][1]][:4] or ''} {cells[1][2][:4] or ''}")
    print(f"hits not taken: 30 Hz {total[0]}, 60 Hz {total[1]}; jumps inside the lockout: 30 Hz {total[2]}, 60 Hz {total[3]}")


def report_super(d, names):
    """Per script and rate: the vblanks from cross going down, in the supercharge (state 44),
    to its jump (state 24), beside the vblanks cross was held. Ten vblanks of hold jump; a
    shorter hold jumps at the release. The two rates must agree to a tick (2 vblanks)."""
    print("script     cross held   press to jump 30/60 (vblanks; -: no supercharge at the press, or no jump)")
    worst = 0
    for name in names:
        cells = []
        for m in ("30", "60"):
            try:
                seq = states(f"{d}/{name}-{m}/trace.tsv")
                _, buttons = pad_log(f"{d}/{name}-{m}/spyro.log")
            except FileNotFoundError:
                break
            press = next((v for v, mask in buttons if mask & CROSS), None)
            release = next((v for v, mask in buttons if press is not None and v > press and not mask & CROSS), None)
            st = dict(seq)
            jump = None
            if press is not None and st.get(press) == SUPER_STATE:
                jump = next((v - press for v, x in seq if v > press and x == SUPER_JUMP), None)
            cells.append((release - press if release else None, jump))
        if len(cells) == 2:
            a, b = cells[0][1], cells[1][1]
            if a is not None and b is not None:
                worst = max(worst, abs(a - b))
            print(f"{name}     {cells[0][0]}          {'-' if a is None else a} / {'-' if b is None else b}")
    print(f"largest difference between the rates: {worst} vblanks")


def report(d):
    names = sorted(s[:-4] for s in os.listdir(f"{d}/scripts"))
    hits = [n for n in names if n.startswith("hits")]
    sup = [n for n in names if n.startswith("super")]
    if hits:
        report_hits(d, hits)
    if sup:
        report_super(d, sup)
    if any(n.startswith("fuzz") for n in names):
        report_moves(d)


def report_moves(d):
    """Per script: jump entries from the ground at each rate, the fastest such re-entry,
    the 60 Hz states that lasted an odd number of vblanks under 4 (the original's
    states last whole ticks: at 60 Hz a 1- or 3-vblank state is a change the 30 Hz
    rules never make; the machine gun of issue #148 was states of 1 and 2 vblanks
    alternating), and the run-length histograms of both rates."""
    rows, hist = [], {"30": {}, "60": {}}
    for s in sorted(os.listdir(f"{d}/scripts")):
        name = s[:-4]
        if not name.startswith("fuzz"):
            continue
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
        gen(d, int(sys.argv[3]), int(sys.argv[4]) if len(sys.argv) > 4 else 1,
            sys.argv[5] if len(sys.argv) > 5 else "moves")
    elif cmd == "run":
        run(d, int(sys.argv[3]) if len(sys.argv) > 3 else None)
    elif cmd == "report":
        report(d)
    else:
        sys.exit(__doc__)
