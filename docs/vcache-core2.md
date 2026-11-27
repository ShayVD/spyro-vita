# The vertex cache's kept projections on core 2

The design for #272, asked for by the owner before the implementation (#282, 2026-11-26). The race test that goes
with it is `tests/vcache/test_vcache_helper.c`. Nothing here changes game code yet.

## Summary

- **What:** a helper thread on core 2 works out the vertex cache's kept projections while the game thread records them. The game thread on core 0 then only records: it finds the entry, logs the place and keeps the inputs (`lz_add`).
- **How:** one 16-bit claim word per entry, plus a per-entry `cov` (the last record the helper worked out). The game thread publishes the store in batches with one release store. The helper takes whole entries in store order with a relaxed CAS and hands each back with one release store. An owner (the game thread, or the walk after the handover) takes an entry back with an acquire CAS. An entry's projections are still worked out in the order they came, by one thread at a time, with the same code (`rtp_apply_in`). So the answers are the same.
- **Fallback:** when the helper is behind, the game thread works the projection out itself, exactly as today.
- **Switch:** `vc_helper=0` keeps today's code paths.
- **Expected:** about 1.5–3 ms a vblank off the game thread in Artisans' heaviest 60 Hz stretch. It needs 0.9 ms (D264: 15.4 ms of 14.5).
- **Race test:** under ctest, 60 + 30 iterations of six frames each, about 4.3 M projections in all. Every projection is worked out exactly once, and every entry and log place matches a single thread's answer. Three mutants fail as they should, and ThreadSanitizer is clean on a native run.

## Where the time is

The numbers are from hardware session 9 (D264, `runtime/hw-ded47d3/RESULTS.md`, logs `logs/hw-ap23*`). All runs use the owner's settings at DRAW DISTANCE MAX 400, with virtual vblanks.

- **Projections:** at 11,400 the game thread works out 5,128 of 5,624 projections a vblank at once (91%, D261). In `ap23a1` at 7,201–7,500 it is 3,901 of 4,318, of which 632 are exact repeats whose maths `rp_skip` skips. It keeps 418, and the walk works out 362 of them.
- **Why so few are kept:** the walk on core 1 is 63% busy there and 85% at 11,400, so the idle rule (D188: keep only while the last frame's walk has ended) keeps little.
- **Cost on the game thread:** the wobble fix costs about 4.8 ms a vblank there (9.83 against 5.02 ms with `persp=0 subpixel=0`, D264). Per window, `ap23a1` minus `ap23x1` is 1.24–1.37 µs a projection.
- **Per part,** comparing windows whose KB of primitives are the same in both runs:
  - An at-once projection costs 1.3–1.4 µs. Spyro: (0.77 − 0.43) ms / 242 = 1.40 µs at 7,500, and 1.36 at 8,100. Mobys: (2.57 − 1.32) / 975 = 1.28 at 8,100.
  - The world's projections average 1.0 µs, with a sixth to a third of them repeats. Solved with 1.35 µs for the others, a skipped repeat costs about 0.35 µs: the find and the log alone.
- **The cost of a kept projection can't be separated from these windows.** The kept counts barely vary (300–415), and a least-squares fit gives a negative cost. From the code, it is a repeat's find and log plus `lz_add`'s ~45 instructions and one or two store lines: 0.5–0.7 µs. `vc_helper=2` measures it (see "The switch").
- **`vc_lazy_logic=2` didn't keep more** (`ap23x2`: 125,184 kept per 300 vblanks against 125,290). Mode 2 only makes every frame lazy (`lz_next`). The keep rule in `rtp_point` stays the walk-idle one, so D264's "changes nothing" says nothing about keeping everything.
- **Core 2's free time:** the render thread has about 6.5 ms a vblank free (D261; report 259-1: 61% busy at 11,400). Session 9's render line says "busy 16 ms per vblank (96%)", but that counts `run()` with the present's swap, which waits for the display (`gpu_thread.c` `s_rt_busy_us`). The helper runs below the render thread, so it only gets the real idle time. That time is the main unknown (see "Risks").

## What changes

Today, in a lazy logic frame, a projection is kept only while the last frame's walk has ended. Otherwise the game thread works it out at once. The walk works the kept ones out as it looks polygons up (`lz_need` → `lz_flush`), and a few are never needed.

With the helper:

- **Recording:** the game thread keeps every projection while the helper keeps up.
- **The helper:** on core 2, it walks the store in record order and works out each entry's kept projections, in place in the table.
- **At the handover:** most entries are already done. The walk's `lz_need` takes them back with one acquire, and works out itself only what the helper hasn't reached.
- **Unchanged:** the walk-idle rule still keeps what it keeps today, so `vc_helper=1` never keeps less.

## Which entries the helper takes

- **Tables:** only the logic draw's (`VC_LOGIC`), and only frames recorded lazily (`tb->lazy`: `vc_lazy_logic` ≥ 1 and no in-between frames, as `lz_next` decides).
  - The presenter's table is out of scope: `vc_lazy=0` on the Vita (D197), and it lives on core 1.
  - At 30 Hz with in-between frames the logic frames work everything out at once, as today. Core 2 also draws twice the pictures there.
- **Projections:** only kept ones.
  - Overflow entries are worked out at once, never kept (#221).
  - Places past the log are not kept either (`lz_add` takes `q < VC_LOG` only, #237). So the helper never writes `Xlog`, `nxlfar` or `xlc_line`.
- **Order:** the store's record order (`lz->rec[0..pub)`). Each record names its entry: `LzRec.pad` becomes `ent`, so `LzRec` stays 16 bytes.
- **Per claim:** the helper takes an entry's whole run of kept records, from the newest it may take down to `cov[e]`, and works them out oldest first.
- **Tables it has left:** a table the game thread is about to record into again (see "The table's reuse").
- **Frame order:** the helper serves the oldest frame with published records it hasn't passed. It finishes the handed-over frame's tail, which the walk is reading, before the frame being recorded.

## The claim

### The words

Per table, in the `Lazy` store, allocated only with the helper on:

| Word | Type | Written by | Meaning |
|---|---|---|---|
| `cl[e]` | `uint16_t` | game, helper, walk (atomic) | the claim state, below |
| `cov[e]` | `uint16_t` | the entry's holder (relaxed) | records up to `cov − 1` are worked out (0: none) |
| `pub` | `unsigned` | game (release) | records `0..pub−1` are published |
| `final` | `unsigned` | game (release) | the frame ended; `pub` is final |
| `hdone` | `unsigned` | helper (relaxed) | the helper's next record (the game's backlog) |
| `stop`, `inside` | `unsigned` | game / helper (seq_cst) | the reuse handshake |

`cl` and `cov` are 16.5 KB each per table. They are cleared with `memset` as the table starts recording (`lz_next`), after the helper has left it. Nothing is added to `Entry`, which stays 40 bytes (#159).

### The states

| `cl[e]` | Value | Meaning | Who may move it |
|---|---|---|---|
| FRESH | 0 | at most one kept record, never offered again | helper → H (taking that one record); owner → OWN |
| Q\|idx | 0x8000 \| idx | kept records offered up to record `idx` | helper → H once `idx < pub`; owner → OWN; game → Q\|newer |
| OWN | 1 | the owner side works the entry out itself | game → Q\|idx (after a new keep) |
| H | 2 | the helper holds it | helper → DONE |
| DONE | 3 | the helper worked it out up to `cov` | owner → OWN (acquire); helper → H (records after `cov`) |

The owner side is one thread at a time: the game thread while the frame records, the walk after the handover. The handover (D71's post, a semaphore) orders them.

### The game thread

**Keeping** (`lz_add`, then the claim word):

1. It writes the record, as today: `next` is the entry's `head` if `T_PEND` is set, else 0, and then `head` moves on.
2. If `T_PEND` was clear, this is the entry's first kept record since it was last worked out. The state is FRESH (nothing kept yet) or OWN, and nobody else writes the entry in either. It sets `T_PEND` (relaxed store). If the state is OWN, it stores Q\|n (relaxed). FRESH stays FRESH: its one record is this one.
3. If `T_PEND` was set:
   - FRESH or Q: relaxed CAS to Q\|n.
   - OWN: store Q\|n.
   - H or DONE: leave it. The helper took the earlier records, and this one sits after `cov` for the helper's next pass or for an owner.
   - **It never waits.**
4. Every `VH_PUB` keeps (32), it does a release store `pub = n`. If the helper sleeps and the backlog is at least `VH_WAKE` (32), it wakes the helper.
5. At the frame's end (`done_hook`, before the tables swap), it does a release store `pub = n`, then `final = 1`, and wakes the helper.

**Deciding** (`rtp_point`): keep when the store has room and any of these holds:

- `n − hdone < VH_BACKLOG` (512 records, about 0.6 ms of the helper's work);
- today's rule keeps it (the walk is idle, or the `vc_lazy_logic=3` quota);
- the entry's state is H (so an at-once record never waits for the helper).

Otherwise it works the projection out at once, as today: `lz_need` (below), then the maths. `hdone` is read relaxed, every `VH_PUB` records, so the line the helper writes isn't pulled in on every record.

**A hand-over's source** (`hk_src`, `derive`, the own points) and every other read on the recording table go through `lz_need`, as #193 already requires.

### The owner side: `lz_need`

- **The fast path stays as it is:** `T_PEND` clear means nothing is pending. The slow path becomes `lz_collect`:

```
lz_collect(tb, k, e):
  loop:
    c = load(cl[e], relaxed)
    FRESH or Q|idx: if CAS(cl[e], c -> OWN, relaxed): lz_flush(e); return   # not taken yet: today's path
    OWN:            lz_flush(e); return
    H:              spin (yield) until it changes; count the wait           # the helper is on it
    DONE:           if CAS(cl[e], DONE -> OWN, acquire): lz_flush(e); return  # the rest after cov, if any
```

- **What `lz_flush` changes:**
  - It walks the list from `head` only while a record's index + 1 > `cov[e]`. It no longer runs to `next == 0`: past `cov` the links are the helper's, and today's in-place reversal may have turned them round.
  - It sets `cov[e] = head[e]` and clears `T_PEND`, as today.
  - It still reverses its own records in place, since nobody else reads them while the owner holds the entry.
- **The helper never clears `T_PEND`.** An entry it has finished stays on the slow path until an owner takes it back with the acquire. Otherwise the walk's fast path could read candidates the helper wrote with no acquire, or read them half-written. Mutant 2 of the race test is exactly that.
- **When an owner waits:** only on H, and only from the walk's lookups or a hand-over's read. The game thread's keeps and at-once records never wait. The wait is one entry's work (a few µs), unless the render thread preempts the helper while it holds that one entry. The counters record every wait (see "The switch").
- **The walk's cheaper path (optional):** once the helper has passed every record of a final table, it stores `hfin = 1` (release) and never returns. After one acquire load of `hfin`, the walk takes DONE entries back with plain relaxed stores instead of an acquire CAS each. That saves a `dmb` per entry on core 1 in the common case.

### The helper

```
loop (the oldest frame with work; the reuse handshake around it):
  p = load(pub, acquire); fin = load(final, acquire) first
  for h in hdone .. p-1:
    if load(stop, relaxed): leave
    e = rec[h].ent; c = load(cl[e], relaxed); top = none
    FRESH:                         top = h               # its one kept record
    Q|idx and idx < p:             top = idx             # everything offered, all published
    DONE and h + 1 > cov[e]:       top = h               # kept after its last pass
    if top and CAS(cl[e], c -> H, relaxed):
      walk next from top while index + 1 > cov[e] into a local array (no writes to the list)
      work them out oldest first: rtp_apply_in in place, rp_skip with mask T_MULTI (not T_PEND)
      cov[e] = top + 1 (relaxed); store(cl[e], DONE, release)
    store(hdone, h + 1, relaxed)
  finished when fin and hdone == p
```

- **Why the claim CAS can be relaxed:** everything the helper reads (the records up to `top`, the register rows, the entry and its `cov`) was written before a release store of `pub` it has acquired. This holds:
  - for the game thread's own writes;
  - for the helper's earlier writes, which are its own;
  - for an owner's earlier writes, which come before that owner's later Q store and the game's next `pub`.
- **The index check:** Q\|idx is taken only when `idx < p`. A Q the game stored after the last `pub` it read is passed by, and met again at record `idx`.
- **No `head` read:** the helper never reads `head[e]`. The game may append to an entry the helper holds, and such records sit after `top`.

### Why each projection is worked out exactly once, in order

- **Ownership:** an entry's records are worked out by whoever holds it: the helper in H, the owner in OWN.
- **Disjoint runs:** the helper works `(cov, top]`; an owner works `(cov, head]`. Each sets `cov` to its end before handing the entry on. `cov` only grows, and a record is above `cov` until someone works it out.
- **Order:**
  - The helper's runs end at `top`. An owner's starts above the `cov` the helper left (after DONE), or at the old `cov` when it takes the entry before the helper (FRESH/Q).
  - The helper then finds OWN and passes by. It never takes OWN, and it takes DONE only for records after `cov`.
  - Records come in index order and every run is worked oldest first, so the entry sees its projections in record order.
- **The same code:** each projection is worked out with `rtp_apply_in`, as at once or in `lz_flush`. Its inputs are the record and its register row, both immutable once published. Its effect depends only on the entry's state before it, so every entry ends the same as with one thread.
- **The far and lfar arrays** are the one exception (see "The far index").

### The table's reuse

The tables alternate (D71). Frame N + 2 records into the table frame N used. Its walk was joined at frame N + 1's `DrawSync`, but the helper may still be in its tail.

- **The game thread** (`done_hook`, before resetting the next table): `store(stop, 1, seq_cst)`, then spin while `load(inside, seq_cst)`. Then it resets the table (generation, counts, `memset` of `cl` and `cov`), clears `stop`, and does a release store of the frame number into `fseq`.
- **The helper,** before working a table: `store(inside, 1, seq_cst)`. If `load(stop, seq_cst)` is set, or `fseq` isn't its frame, it leaves. It checks `stop` (relaxed) before each entry, and stores `inside = 0` (release) when it leaves.
- **Why it's safe:** this is Dekker's pair, with a `dmb` on each side. At least one side sees the other, so the table is never reset under the helper. Each wait is at most one entry's work, and usually zero: the helper left that table a frame ago.

## Memory order on ARMv7 and x86

Checked by compiling the primitives with the Vita's compiler (`arm-vita-eabi-gcc` 15.2, `-O2`, ARMv7-A) and `clang --target=i686-linux-gnu -O2`. The host build's x86 is TSO.

| Step | Thread | C11 | ARMv7 (Cortex-A9) | x86 (i686) |
|---|---|---|---|---|
| offer (Q\|n, FRESH stays) | game | relaxed store | `strh` | `movw` |
| re-offer on an offered entry | game | relaxed CAS | `ldrexh`/`strexh` loop, no `dmb` | `lock cmpxchgw` |
| publish `pub` (every 32 keeps, frame end) | game | release store | `dmb ish; str` | `mov` |
| read `pub`, `final` | helper | acquire load | `ldr; dmb ish` | `mov` |
| claim (→ H) | helper | relaxed CAS | `ldrexh`/`strexh` loop | `lock cmpxchgw` |
| hand back (→ DONE) | helper | release store | `dmb ish; strh` | `movw` |
| take before the helper (FRESH/Q → OWN) | game, walk | relaxed CAS | `ldrexh`/`strexh` loop | `lock cmpxchgw` |
| take back (DONE → OWN) | game, walk | acquire CAS | loop, then `dmb ish` | `lock cmpxchgw` |
| `tag` loads (probes, fast path, prefetch) | all | relaxed load | `ldr` | `mov` |
| `tag` stores (by the holder) | all | relaxed store | `str` | `mov` |
| `log[q]` flags (by the holder); `log` reads on the walk | all | relaxed load/store | `ldrh`/`strh` | `movw` |
| `cov`, `hdone` | all | relaxed | `ldrh`/`strh`, `ldr`/`str` | `mov` |
| far / lfar index | all | relaxed fetch_add | `ldrex`/`strex` loop | `lock xadd` |
| reuse handshake | game, helper | seq_cst store + load | `dmb; str; dmb` … `dmb; ldr; dmb` | `xchg` … `mov` |

- **What it costs the game thread per record:**
  - nothing on a first keep;
  - one relaxed CAS (an exclusive pair, no barrier) when it keeps again under an offered entry;
  - one `dmb` per 32 keeps;
  - one acquire CAS when it takes back an entry the helper finished.
- **No standalone fences:** `relaxed load + atomic_thread_fence(acquire)` would work too, but ThreadSanitizer doesn't model fences. The acquire CAS costs the same `dmb` and is checkable.
- **x86 (the host's i686 build):** every acquire and release is a plain `mov`, and the CASes and fetch_adds are `lock` instructions. The host build runs the same protocol, and its FNV gates see the same answers.

## Shared state the helper writes

Writes in place make the helper's work cheap: no second copy of the entry. These are the places where other threads can see them:

- **The entry's `tag`:** the game thread's probes (`find_in`, `look`, `ov_find`) and the walk's prefetch in `psx_vcache_poly` read the tags of entries the helper is writing, for the key and generation only. Those generation bits never change while an entry is current. Loads in the probes and stores in the commit (`set_state`, `add_cand`, `rtp_apply_in`, `lz_flush`) become relaxed atomics (`VC_TAG_LD`/`VC_TAG_ST`). The instructions are the same on both targets; only the compiler's freedom to merge stores goes.
- **Candidates, `RpE`, `Rp`:** only the holder writes them. Readers take the entry back first.
- **`log[q]`:** the game thread writes the entry index before the record is published. Only the entry's holder writes `LOG_LC`/`LOG_CL` (a relaxed load and store, not `|=`). The walk's scans (`log_index`, `ord_w`, `resolve`'s chain) read `log` while the helper may still be in the tail, so they use relaxed loads. A place's `lc` and flags are read only after its entry is taken back (#193's rule).
- **`lc[q]`, `far[i]`, `lfar[i]`:** written once by the holder or allocator, and read after taking the entry back.
- **Counters:** the helper gets its own `Sink` block, `s_acc[2]` (and `g_vcache_rec[2]`), published once a frame with atomic adds, like the walk's (#179).
- **The cost model's `lc_line`:** it moves into the `Sink`, so the helper keeps its own. Only the cost counters change, and they depend on timing already.
- **`s_rp_lz_hits`:** already an atomic add.

## The far index

- **Who allocates:** `add_cand` takes a far slot (`tb->nfar++`) when a candidate's place doesn't fit an int16 (#178), and `log_answer` takes an lfar slot (`tb->nlfar++`, #192). With the helper, the game thread (at once), the helper and the walk can all take slots in one table at once.
- **How:** both become `i = atomic_fetch_add(&n, 1, relaxed); if (i >= VC_FAR) full`. A slot's contents are written by the thread that took it, before the entry is handed on with a release, so relaxed is enough. The count may run past the cap, which reads as full until the table's reset sets it to 0.
- **What changes:** the indices, not the places. Candidates are compared and drawn by their positions (`cand_pos`), so the answers are the same. `Xlog`'s `nxlfar` isn't shared: kept projections never go past the log.
- **The one exception:** when a frame fills the array (1,024 far candidates, 1,024 lfar places), which candidate is turned away depends on the order. That is already true today on the Vita, where which projections are kept depends on the walk's timing. #272 adds a `far_full` counter. The host routes' FNV gate must show it at 0, and the race test keeps its frames under the cap.

## When the helper is behind

- **During the frame:**
  - Over `VH_BACKLOG`, the game thread works new projections out at once, exactly as today. `lz_need` takes the entry's offered ones first (FRESH/Q → OWN, `lz_flush`), then `rtp_apply_in`, with the repeat skip.
  - The store full (`VC_LOG` records or `LZ_PROJ` register rows): at once, as today. This is the one place an at-once record may wait on H.
- **At the handover:** the walk takes anything not yet taken itself (today's lazy path), and the helper then passes it by. Entries the helper holds: the walk waits.
- **The helper on another table:** its backlog on this one is the whole store, so the game thread works the excess out at once until the helper arrives.
- **No helper:** if the thread didn't start, or `vc_helper=0`, everything is as today.

The split finds its own balance, as D188's did: the helper takes what core 2 has time for, and the game thread takes the rest. Which projections are kept never changes an answer.

## The switch

- **The `vc_helper` key** (Vita `config.txt`, `main.c`'s key list, shown on the `threads:` line) and `VC_HELPER` (the host's environment, read in `psx_vcache_enable`'s host block like `VC_LAZY_LOGIC`):
  - **0:** no thread, no `cl`/`cov` arrays. `lz_collect` sees none and calls `lz_flush` as today, and the keep rule is today's. The default until the hardware A/B.
  - **1:** the helper.
  - **2 (diagnostic):** keep every projection while the store has room, with the helper idle, so the walk works them all out. This is what D188 meant by keeping everything, which `vc_lazy_logic=2` no longer does. It measures the game thread's cost of a kept projection, k, directly: the game thread's ms with everything kept against the default, over the same windows.
- **API** (`psx_vcache.h`): `psx_vcache_set_helper(int mode)` before the cache records, and `psx_vcache_helper_loop(const PsxVcacheHelperOps *ops)`, the thread's body. Its ops `sleep(us)` and `wake()` come from the platform.
- **The Vita:** `src/platform/vita/vchelp.c` creates `spyro_vchelp` on `SCE_KERNEL_CPU_MASK_USER_2`.
  - Priority 0xA8, behind the render thread (0x80) and the presenter (0x9A, `pres_core=2`), so it takes only their idle time. Stack 64 KB: the local run array is 16 KB.
  - It sleeps on a semaphore with a 1 ms timeout, and is woken with the render thread's sleeping-flag exchange (`gpu_thread.c` `s_rt_sleeping`).
  - Core 3 isn't usable on this unit (session 9's `cores:` line).
- **The host:** a pthread in the linux32 platform layer, run by qemu-i386 like the walk's `--walk-lazy`.
- **Tunables** (`vcache.c`): `VH_BACKLOG` 512, `VH_PUB` 32, `VH_WAKE` 32.
- **The counters** (a Vita line `vchelp N:` and perfcount keys `vc.help.*`):
  - projections and entries the helper worked out, and its busy ms a vblank;
  - projections the game thread kept, and those it worked out at once because the helper was behind;
  - entries owners took before the helper (`steal`);
  - owners' waits on H: count, total µs and worst µs (`vc.help.wait`, `vc.help.wait_us`, `vc.help.wait_max`);
  - the table handshake's waits;
  - `far_full`.

## Expected saving

At 11,400 (D261): 5,624 records a vblank, 5,128 of them at once. A sixth of those are exact repeats (13–18% in session 9's 7,200–8,700 windows), so P ≈ 820 repeats and N ≈ 4,300 others.

| Cost a projection | Value | From |
|---|---|---|
| a, at once (the renderer's hand-overs included) | 1.3–1.4 µs | Spyro, mobys, `ap23a1` against `ap23x1` |
| s, a repeat skipped at once | ≈ 0.35 µs | the world at 8,100 |
| k, kept (find, log, `lz_add`) | 0.5–0.7 µs | the code; `vc_helper=2` measures it |
| h, the helper's (maths and commit, its line misses) | 1.2–1.4 µs | a − s, plus misses on lines core 0 owns |

- **The game thread's saving, with the helper taking all:** N·(a − k) − P·(k − s) − overheads. Repeats are kept too, because their entry is pending by then; the helper still skips them in its run.
  - = 4,300 × (0.6 to 0.9) µs − 820 × (0.15 to 0.35) µs − (0.2 to 0.4) ms
  - **≈ 1.9–3.3 ms a vblank.**
- **The overheads:**
  - the claim word's line and CAS on the ~1,250 keeps a vblank under an entry already pending;
  - coherence misses on entry lines the helper wrote, when the game thread probes them again (~0.1–0.2 ms);
  - one `dmb` per 32 keeps (negligible);
  - taking back the entries the game thread reads again.
- **The helper's need:** N·h ≈ 5.2–6.0 ms a vblank, against core 2's ~6.5 ms free (D261). If core 2 gives only 4 ms, the helper takes about 3,000 and the saving falls to ~1.4–2.1 ms. The backlog rule makes that happen by itself.
- **The walk:** it no longer works out the ~500 kept a vblank (≈ −0.5 ms). It takes back helper-done entries instead: one claim-word read and an acquire CAS each, about 3,000 × 0.1 µs ≈ 0.3 ms, or less with `hfin`. Core 1 is 85% busy at 11,400, so this stays within its 15%.
- **In Artisans' heaviest stretch:** about 1.5–3 ms a vblank off the game thread. That takes 15.4 ms to about 12.5–13.9 ms, under the 14.5 ms budget that holds 2x at 1x. At 7,500 (`ap23a1`: N ≈ 3,270, P ≈ 630) it is ~1.5–2.5 ms.

### Risks

1. **Core 2's real idle time.** The render thread's swap waits look like busy time. If it is much below 6.5 ms, the saving scales down; the backlog rule keeps it safe. The helper's busy line measures it.
2. **Preemption while holding.** The render thread wakes several times a vblank and preempts the helper. If the walk or a hand-over then needs the one entry the helper holds, it waits until the render thread blocks. The game thread's keeps and at-once records never wait. The wait counters show it. If worst waits over ~100 µs show up, plan B is a private copy (see "Rejected").
3. **Lines moving between cores 0 and 2:** the entries, the claim words and `head`. This is modelled at ~0.1–0.3 ms. If it is much more, a lag (the helper staying a few hundred records behind `pub`) cuts the re-touches it races.
4. **k.** If keeping costs more than 0.7 µs, the saving shrinks: at k = 0.9 it is about 1 ms. `vc_helper=2` gives k before `vc_helper=1` is judged.

## Same answers

- **By construction:** each entry's projections are worked out in record order, by one thread at a time, by the same `rtp_apply_in`, from immutable inputs ("Why each projection is worked out exactly once").
- **Indices move:** only the far and lfar slots' indices, which nothing reads as an answer.
- **The gates for #272 (D168's method, kb `same-answers-hash-per-polygon`):**
  - host FNV per polygon `--strict` from clean objects on the 11 routes (S60, T, O60, D60, S, Si, P, Pi, Pf, Pfi, D);
  - develop against `VC_HELPER=0` after step 1;
  - `VC_HELPER=1` against `0` after step 2, each route twice (the split depends on timing), and with `VC_LAZY_LOGIC=0` and `1`;
  - `far_full` 0 on every route;
  - Vita3K's eight routes, shots against `base-20d3514` with 0 difference.
- **The race test** checks the same thing per entry and per log place, against a single thread.

## The race test

`tests/vcache/test_vcache_helper.c` is registered in `tests/vcache/CMakeLists.txt`. It is a model of the parts of `vcache.c` the protocol touches, with the same shapes:

- the tag (state, count, `T_MULTI`, `T_PEND`, generation);
- four candidates, with far places through the atomic far index;
- the order log with each place's own answer (`lc`, `LOG_LC`, lfar);
- the store (`LzRec` with `ent`, lists newest first, `head`) and the repeat record;
- `lz_collect`, `lz_flush` bounded by `cov`, `lz_add` with the claim word, the helper's loop, and the reuse handshake.

Its maths is integer, so x87 precision doesn't matter, and its commit follows `rtp_apply_in`, so an entry's answer depends on its projections' order.

- **Threads:**
  - **The game thread** records six frames per iteration into two alternating tables. It keeps or works out at once by the backlog rule, and probes its neighbours' tags. On 1 record in 32 it reads an entry the way a hand-over does, and checks it against the single thread's state after that entry's last place.
  - **The walk** takes each frame at the handover and looks up 87% of its entries, some twice, in a shuffled order, checking each against the single thread's final answer. Meanwhile the game thread records the next frame and the helper finishes the tail.
  - **The helper** runs as above.
- **Frames:** shaped like Artisans' at 60 Hz. There are 800–6,000 projections over sectors of 16–48 vertices, each sector under its own registers. 38% of vertices fall under a recent key again: half the same point (an exact repeat), half another point on that pixel. A few are behind the eye or far off.
- **Each iteration varies:** the backlog (0, 4, 64, 512, everything), the batch per publish (1, 7, 32, 128), the helper's delay before each projection (0, 40 or 400 spins, to widen the windows), and the helper on or off (`vc_helper=0`).
- **At each table's reuse:**
  - the entries nobody looked up are worked out;
  - every entry's FNV (state, count, flags, candidates by value) and every log place's answer must equal the single thread's (every projection at once, in order);
  - every place must have been worked out exactly once.
- **Mutants** (`VC_RACE_MUTANT`), each of which must fail:
  1. an owner works out an entry the helper holds;
  2. the helper clears `T_PEND` as `lz_flush` does;
  3. an owner's flush ignores `cov`.
- **ThreadSanitizer:** a `test_vcache_helper_tsan` target where the toolchain links `-fsanitize=thread`. Not i686: TSan needs a 64-bit target, so the linux32 build skips it. The native macOS run:
  - `clang -std=gnu11 -O1 -g -fsanitize=thread tests/vcache/test_vcache_helper.c -lpthread`;
  - Apple silicon, a weakly ordered CPU like the A9;
  - 0 reports on the protocol; mutants 1 and 2 are reported as data races.

## Rejected

- **The helper commits on a private copy of the entry and publishes it with one CAS; the owner copies it in** (or takes the entry from the helper at any time and redoes it).
  - For: no owner ever waits, even when the helper is preempted.
  - Against: the commit needs a second, staged form for far places, log answers and the repeat record. The helper's snapshot of the entry races with an owner that takes it mid-copy, unless every `Entry` access becomes an atomic.
  - Plan B if the wait counters show stalls.
- **The helper works out only the maths into a per-record array, and owners commit** (no claim at all).
  - For: the simplest. The helper writes nothing the others read.
  - Against: it moves the commit, plus a line from core 2 per record, onto the walk: ~1.5 ms more on core 1, which is 85% busy at 11,400.
- **The helper on core 2 above the render thread** (no preemption while it holds an entry).
  - Against: the render thread's commands would wait for up to the helper's whole share of the vblank.
- **Waiting on H in the game thread's keeps and at-once records.**
  - Against: these are the paths most likely to meet an entry the helper has just taken. Appending after `cov`, and keeping instead of working out at once, cost nothing.

## Steps for #272

1. **Refactor with no change of behaviour** (helper code compiled, off):
   - Changes:
     - `VC_TAG_LD`/`VC_TAG_ST` and relaxed `log` accesses at the sites above;
     - the far and lfar indices by fetch_add;
     - `lc_line` in the `Sink`;
     - `LzRec.pad` → `ent`;
     - `lz_flush` bounded by `cov`;
     - `lz_need`'s slow path → `lz_collect` (the arrays absent: `lz_flush`);
     - `rp_skip`'s mask parameter;
     - the `far_full` counter.
   - Gate: host FNV against develop on the 11 routes, Vita3K shots, ctest, the Vita build.
2. **The helper in `vcache.c`:** the claim word and `cov` arrays, the keep rule, publishing, `done_hook`'s final publish and handshake, the helper loop and its `Sink`, `psx_vcache_set_helper`, `VC_HELPER`, the host pthread.
   - Port `test_vcache_helper` to a second test that drives the real `vcache.c` through `ngte_rtps_w`, `psx_vcache_frame_take`/`use` and `psx_vcache_poly` with a helper pthread.
   - Gate: FNV `VC_HELPER=1` against `0` (twice each, both lazy modes), the race tests, ctest.
3. **The Vita thread, key and counters** (`vchelp.c`, `main.c`, `perfcount.c`).
   - Gate: Vita3K's eight routes, shots and fixed counters against base, and `vc_helper=1 2` for coverage.
4. **Hardware A/B, with the owner's yes:** Artisans `owner60`, virtual vblanks, the owner's settings at MAX 400. Run `vc_helper=0, 1, 0, 1` and one `vc_helper=2`, then the Toasty route in real time, `0` and `1`.
   - Win: the game thread's ms at 6,900–9,000 and in the 11,400 stretch lower by more than the two base runs' spread; 2x where 1x held; the render thread's pictures and late vblanks unchanged; the walk's join no worse; worst wait under 100 µs.
   - Then the owner decides the default.
