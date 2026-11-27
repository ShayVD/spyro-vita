# same answers

A renderer or vertex-cache change is often meant to change **some** polygons (it finds more of them, or treats
more of them natively) and leave every other polygon's answers exactly as they were, or to do the same maths
for less work. Pictures compare too coarsely to show that. The same-answers check does: on the deterministic host
build, the walk hashes each polygon's inputs and outputs (FNV-1a) into a stream. Base and fix run the same routes,
and `tools/fnvcmp.py` pairs the polygons by index and counts which changed.

The method is spyro-vita's D168 (a cheaper vertex cache: 91 M looked-up vertices over three routes, every
checkpoint the same). The #230 and #231 gates used it for fixes that find more (up to 9 route pairs each; on #230's
8 routes, 0 to 23 polygons newly found a route and 0 found before changed). Each gate wrote its own throwaway scripts for it. These three pieces replace them.
kb: `same-answers-hash-per-polygon`.

| Piece | What |
|---|---|
| [`include/encore/same_answers.h`](../include/encore/same_answers.h) | the hook the walk calls per polygon; compiled out unless `ENC_SAME_ANSWERS` is defined, silent unless the variable of that name is set |
| [`tools/fnvcmp.py`](../../tools/fnvcmp.py) | compares two streams: the first checkpoint that differs, then each polygon unchanged, newly handled or changed |
| [`tools/same_answers_pairs.py`](../../tools/same_answers_pairs.py) | runs base and fix over a list of routes in parallel, each under a timeout, behind the port's lock, and prints one table |

## The hook

In the walk, once per polygon, after its answers are known:

```c
#include "encore/same_answers.h"   /* one .c file defines ENC_SAME_ANSWERS_IMPLEMENTATION before it */

EncSaPoly p;
enc_sa_begin(&p);
enc_sa_u32(&p, n);                              /* inputs: the vertex count, the keys looked up, packet words */
for (i = 0; i < n; i++) {
    enc_sa_u32(&p, key[i]);
    enc_sa_f32(&p, out[i].dx);                  /* outputs: every value the change could move */
    enc_sa_f32(&p, out[i].dy);
    enc_sa_f32(&p, out[i].w);
}
enc_sa_end(&p, found ? ENC_SA_HANDLED : 0);     /* flags bits 1-7 are the port's (which thread, which path) */
```

- **What to fold in:** everything the change could move, and enough of the inputs that a polygon cannot match a
  different one by accident. spyro-vita's vertex cache folded the vertex count, each vertex's reference word, and,
  for a found polygon, its candidate count, multi flag, and every candidate's offsets and depth.
- **`ENC_SA_HANDLED`** marks a polygon the change's subject handled (found in the cache, drawn natively). That is
  the line fnvcmp judges by: a polygon handled in the base must come out the same.
- **Floats go through `enc_sa_f32`, never by their bits.** On an i386 host build without SSE (`-mfpmath=387`), an
  expression can stay at x87 long double precision until it is stored, and inlining can drop the rounding a float
  parameter made. `enc_sa_f32` stores through a `volatile float` there, so what is hashed is the rounded float
  (spyro-vita D168: an offset came out 1/256 pixel apart once `record()` was inlined).
- **One thread writes.** A port that walks on two threads (spyro-vita's presenter on core 1, D167) runs the second
  inline for these runs, so the polygon order is the same every run.
- **Off twice.** Without `ENC_SAME_ANSWERS` defined, every call is an empty macro, so no code and no header remain.
  Define it only in the host build used for these runs, never in a release. With it defined, the stream is written
  only when `ENC_SAME_ANSWERS=<file>` is set. Guard any work done only to feed the hash with `if (enc_sa_on())`.
- **Variables:** `ENC_SAME_ANSWERS` (the output file), `ENC_SAME_ANSWERS_EVERY` (polygons between checkpoints,
  default 1,000,000), `ENC_SAME_ANSWERS_POLYS=0` (checkpoints only).
- **Cost:** 8 bytes a polygon with per-polygon records (spyro-vita's longest route: 28 M polygons, 225 MB). The
  checkpoints only take 16 bytes per million. The stream is flushed at every checkpoint, so a run that is killed
  still leaves everything up to its last one, and fnvcmp reports the run as cut short.

The format is in the header's first comment: a 16-byte header, then 8-byte `P` records, a 16-byte `C` checkpoint
every N polygons (the running hash of every polygon so far, and the count), and an `E` record at the end.

## Base and fix

1. Build both from **clean objects**, each with `ENC_SAME_ANSWERS` defined and the hook in the same place. The
   base is the integration head with the hook added. Before believing "all the same", show that the two binaries
   differ (`playbooks/port-verification`: a rebuild that saw stale file times once built the same binary twice).
2. Pick routes that reach the polygons the change is about (spyro-vita's #231: 9 routes, tours and level routes,
   30 and 60 fps, with and without the in-between frame). Give each run its own save folder.
3. Run them:

   ```bash
   # routes.txt: NAME [KEY=VALUE ...] ARGS...  ({out} is the run's folder, {route} the route's name)
   S    --pad pads/tour.txt --pad-after pads/route_s.txt --frames 16500 --log {out}/game.log --save {out}/save
   S60  --pad pads/stone60.pad --frames 10000 --tick60 --interp --log {out}/game.log --save {out}/save
   ```

   ```bash
   python3 third_party/encore/tools/same_answers_pairs.py --base "build/base/game" --fix "build/fix/game" \
     --common "--disc disc/files --vcache --widescreen" --routes routes.txt --out logs/sa-231 \
     --jobs 4 --timeout 14000 --wrap tools/tc --lock "$MAIN/runtime/autopilot/test.lock"
   ```

   `--wrap tools/tc` runs each run in the port's container, passing the variables on its command line. Running the
   whole runner inside one container is the other way (`tools/tc python3 .../same_answers_pairs.py ...`), and is
   cheaper when containers are slow to start. `--lock` takes the kit's lock folder (`mkdir`, with the holder's PID
   inside, as `runlock` and `ap_gate` do) for the whole batch. Exit 5 means it stayed busy, as with `ap_gate`.
4. Read the table (also `OUT/summary.txt`, with each route's fnvcmp report in `OUT/<route>/fnvcmp.txt`). Here,
   #230's S60 pair as a row of it (its first divergent checkpoint and run time were not recorded):

   ```
   | route | base | fix | polygons | unchanged | newly handled | changed | unhandled, changed | first checkpoint differs | seconds | verdict |
   | S60 | ok | ok | 12,969,897 | 12,969,871 | 23 | 0 | 3 | - | - | PASS |
   ```

## fnvcmp

```bash
python3 tools/fnvcmp.py BASE FIX [--strict] [--examples N] [--json]
```

Each polygon, paired by index:

| Class | Means | Fails? |
|---|---|---|
| unchanged | the same hash and flags | no |
| newly handled | not handled in the base, handled in the fix | no: what a fix that finds more is for |
| changed | handled in the base, and the fix's hash or flags differ (including no longer handled) | **yes** |
| unhandled, changed | handled in neither, the hash differs | only with `--strict` |

Exit 0 only when no polygon handled before changed. It also fails (exit 1) when the two runs walked different
numbers of polygons (the route diverged, so polygons cannot be paired), when a stream has no end record (the run
was cut short), or, with checkpoints only, when any checkpoint differs. A different checkpoint interval or a file
that is not a stream is exit 2. It also prints the first checkpoint and the first polygon that differ, the classes
broken down by flags, and the first few polygons of each class. Two synthetic 28 M-polygon streams compared in
about a second when the same and in under 3 s with 867 scattered differences (a Mac, the files in the page cache). Pass `--strict` for a change that is only
meant to be cheaper (D168): there, even an unhandled polygon must not move, and every checkpoint should be the same.

## What it does not show

Pictures (`shots_cmp.py`), the host checks' files (`cmp`), timing. It complements those checks and replaces none of
them. The polygons a change handled newly still need a look in pictures: fnvcmp only counts them.

**Test:** `tools/test/same_answers_test.sh` (a synthetic 1,050-polygon walk, `tools/test/same_answers_walk.c`, no
game): the hook compiled out and unset, fnvcmp on the same run, a fix that only finds more, a mutant that moves a
handled polygon, a cut-short run and the checkpoint-only form, the runner's table, a timeout, `--wrap` and the lock.
Where the compiler can (`-m32 -mfpmath=387`), the x87 build too.

**From:** spyro-vita D168 and its #230/#231 gates (`fnv_insert2.py`, `fnvcmp.py`, `fnvrun.sh`, `inner.sh`, never
committed there).
