# Heap-growth trials — what is *not* leaking

*State of play: 2026-08-01, four trials run. The leak is real and still
unattributed; what follows is the elimination, the reference numbers it produced,
and the seven instrument defects it exposed.*

## The question, and why a fifth long session would not answer it

Two multi-hour human sessions measured guest-heap growth:

| Session | Figure | Problem |
|---|---|---|
| 2026-07-27, 2.28 h | **+7.2 MB/h**, dead linear | mixed activity |
| 2026-07-31, 2.00 h | **+7–11 MB/h** of sustained play | mixed activity |

Both are averages over "whatever was being done at the time", and neither log
records what that was. A third such session produces a third average. The 2 h of
2026-07-31 also contained a ~2.3 MB sawtooth recurring about every five minutes,
which the arithmetic of the day put at ~0.97 MB retained per cycle — a figure
that survived exactly as long as it took to measure the cycle on its own (see
trial 6).

So: **one activity per run, bracketed by markers, with the guest-work counter
running.**

## Method

```sh
DYLD_LIBRARY_PATH=/opt/homebrew/lib \
  THEOC_LONGRUN=15 THEOC_FRAME_MS=50 ./port/build/theoc 2>| session-trial-N.log
python3 tools/plot_health.py session-trial-N.log --table
```

- **`THEOC_LONGRUN=15`** — 60 s is too coarse for a ten-minute trial; 15 s
  resolves a teardown and the rebuild that follows it as separate samples.
- **`Alt+M`** stamps a numbered `[mark]` and forces the next `[health]` out
  immediately, so an interval boundary lands on the event. A trial is the
  segment between two marks. Mark *after* the transition has settled, so the
  transition's own cost falls in the previous segment.
- **Read `moves` and `max Δ`, not the fitted slope.** A single step at a segment
  boundary drags a least-squares fit hard: trial 2 reported +0.884 MB/h of
  "province idle" that was 38 identical samples plus one step.
- **`MB/1k frames` is the cross-trial figure.** The engine is frame-tied, so a
  per-hour rate only compares runs at the same frame cap.

Everything below is on the same save, at `THEOC_FRAME_MS=50` (≈19.5 fps).

## Results

| # | Activity | Result |
|---|---|---|
| 1 | Idle on realm/map, 10 min | **null** — live pinned at 27.04 MB, 0 moves |
| 2 | Idle in province, 10 min | **null** — pinned at 30.18 MB across 38 consecutive samples |
| 3 | Windowed ↔ fullscreen ×45 | **null** — 0 moves on both screens, both directions |
| 6 | Realm ↔ province ×20 | **warm-up, not a leak** — saturates at 30.72 MB |

### 1 & 2 — idling allocates nothing, on either screen

Live is not merely slow-growing, it is *pinned*: trial 2 held 30.18 MB across 38
consecutive 15 s samples. Two intervals in the same run moved ±0.1–0.2 MB/h, so
the field can move — this is a real zero, not a dead readout.

The useful contrast is the guest-work column: **0.016M blk/frame on the map
against 0.208–0.234M in province**, ~13× the work per frame for the same zero
allocation. The simulation ticking is not what allocates.

### 3 — the window mode is free, and the province view has a price

45 toggles. Six over ~6 min on the map (pinned at 28.10 MB) and five over ~5 min
in province (pinned at 30.75 MB) moved nothing, in either direction. The +1.08 MB
step that ended trial 2 was therefore *not* the toggle as such.

What the trial did catch is the province view being **torn down and rebuilt**
inside a fast-toggling burst:

```
16:22:00   live 31.52   +0.40
16:22:15   live 29.19   -2.33     <- province view torn down
16:22:26   live 31.80   +2.61     <- rebuilt
```

The 2026-07-31 session's twelve sawtooth drops averaged **−2.32 MB**. Same
number: that session's sawtooth is the province view being rebuilt.

### 6 — the sawtooth saturates

20 cycles, 40 transitions, +6.28 MB net over the marked segment. The shape is
what settles it — live at the end of each map visit:

```
24.44 28.89 29.48 29.60 30.21 30.24 30.25 30.37 30.51 30.51
30.52 30.64 30.65 30.66 30.67 30.68 30.69 30.70 30.71 30.72 30.72
```

**+4.45 MB of the 6.28 is the first province visit.** The remaining +1.83 MB
decays across the other 19 cycles, and the last six average **+8.3 KB each**.
It settles at 30.72 MB. The ±~2.5 MB sawtooth is present on every cycle and is
fully reclaimed. Two minutes idle afterwards: flat — nothing released late,
nothing gained.

So **"~0.97 MB retained per cycle" was an artefact** of a 60 s interval
containing the cycle *and* everything else being done at the time. Retired.

## Reference numbers

Costs, for reading any future log:

| Event | Guest live |
|---|---|
| Save load (boot → map) | **+21.9 MB** (2.56 → 24.44) |
| First province entry | **+4.45 MB** |
| Subsequent province build / teardown | **+2.5 / −2.5 MB**, reclaimed |
| Province cycle retention, once warm | **~8 KB/cycle** |
| Steady state, this save | live **30.72 MB**, frontier 34.43 MB |

Guest work per frame, same save: map **0.016M**, province **0.21–0.86M**.

**ESP identifies the screen.** `0x6fffee5c` is the map, `0x6fffc240` is province;
the 2026-07-31 session showed six recurring values, and they are game states, not
stack drift. The `[health]` line's `esp` field is therefore a free record of
which screen each sample was taken on — used above to split trial 6 into visits.

## What is eliminated, and what is next

Idling allocates nothing (1, 2). The window mode allocates nothing (3). The
screen cycle saturates (6). The +7–11 MB/h of sustained play still has to come
from somewhere.

Remaining, in the worklist (`../../task_fifo.md`, Playability #1): **battles**
(the largest thing those sessions were doing, and the leading candidate by
elimination), the **reload path**, **UI panels**, and the **two-frame-cap
discriminator** that would say whether the leak is per sim tick or per wall-clock
hour. Host RSS is separately unattributed: +99 MB over the 2026-07-31 session.

Also unexplained, and cheap to notice again: **`blk/frame` in province read 0.21
in trial 2 and 0.83–0.86 in trial 3** — 4× for nominally the same activity.

## The instrument defects these trials found

Seven, in four sessions. Recorded because the pattern is the point: an
instrument's *first real use* is when its defects surface, and five of these
produce plausible wrong numbers rather than obvious failures.

1. **`exec_blocks()` was armed only by `THEOC_FPS`** — and the *watchdog's*
   entire guest-running/stuck-host-side verdict reads it. A watchdog-only run
   would have called every stall host-side. Found by reading the arming path,
   not by being burned. See [diagnostics.md](diagnostics.md), "A counter nobody
   armed".
2. **`TrapLayer::report()` printed to stdout** while the documented capture
   recipe is `2>session.log` — so the 2026-07-31 session exited normally and
   still has no end-of-run allocator state.
3. **`[video] Alt+Enter` and `[click]` printed to stdout**, same class. The
   trial-2 step could not be attributed to a fullscreen toggle from the log; the
   operator had to remember it.
4. **Host RSS delta underflowed** — `size_t` subtraction, so any dip below the
   first sample printed `+17592186044408.9 MB`.
5. **Since-start rates are meaningless on a short run.** The one-time ~27 MB load
   dominates and never washes out, so a ten-minute trial reported
   `+6498 MB/h -> 0.0 h headroom` while the live set was flat. Headroom is now
   withheld as `n/a (warm-up)` below 0.5 h.
6. **Segments were cut on `up N.NNh`, a 36 s quantum**, so up to two samples
   *before* a marker fell inside its segment. This put the +21.9 MB save load
   inside trial 6 and reported **+30.65 MB instead of +6.28**. Now cut on the
   frame counter, which is exact.
7. **A double-tapped marker produced zero-duration segments** — three forced
   samples in the same second, printing a row of zeroes that reads exactly like
   a measured null. Segments under 30 s are dropped.

The general lesson, which is [diagnostics.md](diagnostics.md)'s lesson again from
the other side: an instrument that reads *exactly zero* deserves suspicion, and
so does one that reads a confident, plausible number the first time it is
pointed at something real.
