# Heap-growth trials — log

Kept for historical records. Not all that useful after v1.

*State of play: done, game is provably stable, nothing threatens OOM.*

## Multi-hour sessions

Two multi-hour sessions measured guest-heap growth:

| Session | Figure | Problem |
|---|---|---|
| 2.28 h | **+7.2 MB/h**, dead linear | mixed activity |
| 2.00 h | **+7–11 MB/h** of sustained play | mixed activity |

Both are averages over general gameplay, with various actions and activities,
that included multiple saves, loads, cinematics, battles, and empire management.
The 2nd 2h session also contained a ~2.3 MB sawtooth recurring about every five minutes,
which the arithmetic of the day put at ~0.97 MB retained per cycle — a figure
that survived exactly as long as it took to measure the cycle on its own (see
trial 6).

## Method

```sh
DYLD_LIBRARY_PATH=/opt/homebrew/lib \
  THEOC_LONGRUN=15 THEOC_FRAME_MS=50 ./port/build/theoc 2>| session-trial-N.log
python3 tools/plot_health.py session-trial-N.log --table
```

- `THEOC_LONGRUN=15` — 60 s is too coarse for a ten-minute trial; 15 s
  resolves a teardown and the rebuild that follows it as separate samples.
- `Alt+M` stamps a numbered `[mark]` and forces the next `[health]` out
  immediately, so an interval boundary lands on the event. A trial is the
  segment between two marks. Mark *after* the transition has settled, so the
  transition's own cost falls in the previous segment.
- Read `moves` and `max Δ`, not the fitted slope. A single step at a segment
  boundary drags a least-squares fit hard: trial 2 reported +0.884 MB/h of
  "province idle" that was 38 identical samples plus one step.
- `MB/1k frames` is the cross-trial figure. The engine is frame-tied, so a
  per-hour rate only compares runs at the same frame cap.

Everything below is on the same save, at `THEOC_FRAME_MS=50` (≈19.5 fps).

## Results

| # | Activity | Result |
|---|---|---|
| 1 | Idle on realm/map, 10 min | **null** — live pinned at 27.04 MB, 0 moves |
| 2 | Idle in province, 10 min | **null** — pinned at 30.18 MB across 38 consecutive samples |
| 3 | Windowed ↔ fullscreen ×45 | **null** — 0 moves on both screens, both directions |
| 6 | Realm ↔ province ×20 | **no leaks** — saturates at 30.72 MB |
| 7 | 9 battles in 3 phases, 30 min | **no leaks** — saturates at ~43.6 MB |

### 1 & 2 — idling allocates nothing, on either screen

Live is *stable*: trial 2 held 30.18 MB across 38
consecutive 15 s samples. Two intervals in the same run moved ±0.1–0.2 MB/h, so
the field can move — this is valid.

The useful contrast is the guest-work column: 0.016M blk/frame on the map
against 0.208–0.234M in province, ~13× the work per frame for the same zero
allocation. The simulation ticking is not what allocates.

### 3 — switching between fullscreen & windowed

45 toggles. Six over ~6 min on the map (pinned at 28.10 MB) and five over ~5 min
in province (pinned at 30.75 MB) moved nothing, in either direction. The +1.08
MB step that ended trial 2 was therefore *not* the toggle as such.

What the trial did catch is the province view being torn down and rebuilt
inside a fast-toggling burst:

```
16:22:00   live 31.52   +0.40
16:22:15   live 29.19   -2.33     <- province view torn down
16:22:26   live 31.80   +2.61     <- rebuilt
```

The twelve sawtooth drops averaged −2.32 MB. Same
number: that session's sawtooth is the province view being rebuilt.

### 6 — the sawtooth saturates

20 cycles, 40 transitions, +6.28 MB net over the marked segment. The shape is
what settles it — live at the end of each map visit:

```
24.44 28.89 29.48 29.60 30.21 30.24 30.25 30.37 30.51 30.51
30.52 30.64 30.65 30.66 30.67 30.68 30.69 30.70 30.71 30.72 30.72
```

+4.45 MB of the 6.28 is the first province visit. The remaining +1.83 MB
decays across the other 19 cycles, and the last six average **+8.3 KB each**. It
settles at 30.72 MB. The ±~2.5 MB sawtooth is present on every cycle and is
fully reclaimed. Two minutes idle afterwards: flat — nothing released late,
nothing gained.

So "~0.97 MB retained per cycle" was an artefact of a 60 s interval
containing the cycle *and* everything else being done at the time. Retired.

### 7 — battles: only a warm-up

The leading candidate by elimination, running nine battles in three
phases, designed by the author to separate three things at once:

| Phase | Battles | Reloads |
|---|---|---|
| 1 | 3 real, fought | quit to menu, load the same save after each |
| 2 | 3 auto-resolved | same |
| 3 | 3 synthetic, units spawned for both sides via the console | none |

124 samples over 0.51 h / 22,880 frames, `THEOC_LONGRUN=15 THEOC_FRAME_MS=50`,
both streams captured. Phase boundaries were double-tapped `Alt+M`; the marks
were placed just after each battle, so a reload never sat on a boundary.

Reloading the same save converges. Live floor of each load-to-load cycle:

```
30.97  39.79  42.03  43.01  43.61  43.64
       +8.82  +2.24  +0.98  +0.60  +0.03
```

The world is torn down and rebuilt from the same file each time, so anything the
second load did not return would show here as a step. By the sixth load it costs
30 KB. Phase 3, with no reloads at all, does the same: floors of 46.62,
47.32, 47.80 — steps +2.98, +0.70, +0.48. Both series decay.

Per phase, map baseline to map baseline:

| Phase | Live | MB/1k frames |
|---|---|---|
| 1 — real, excl. warm-up | 30.97 → 42.62 | **1.208** |
| 2 — auto-resolve | 42.62 → 43.64 | **0.226** |
| 3 — synthetic | 43.64 → 47.80 | **0.784** |

So a watched battle costs ~5× an auto-resolved one per frame: the allocation
tracks the battle *view* running, not the resolution. Both still retain more
than a plain province cycle (~340 KB per auto-resolved battle against ~8 KB).

The session ended flat — 8 consecutive samples at 47.80 MB, 0 moves, and the
frontier unmoved at 53.39 MB for the last six minutes. Nothing was released
late. Exit was clean: `0 unimplemented`, no watchdog stall, and 23.1 MB live /
53.4 MB frontier / 1529 free blocks — the first session ever to have an
end-of-run trap report in its log, because that had been going to stdout (defect
2).

Two side results.

Guest work peaked at 9.694M blocks/frame at 2.4 fps,
against 0.016M on the map and 0.21–0.86M in province — 11–45× province work per
frame, with 71–75k audio underrun frames tracking it.
During a battle the frame rate dropped heavily, which was a genuine saturation, not a host stall. And the
"stress test" phase was *lighter* on the engine than the real battles (blk/frame
0.4–1.2 against 2.7–9.7), so spawning a pile of units does not reproduce
whatever a real battle does.

## Reference numbers

Costs, for reading any future log:

| Event | Guest live |
|---|---|
| Save load (boot → map) | +21.9 MB (2.56 → 24.44) |
| First province entry | +4.45 MB |
| Subsequent province build / teardown | +2.5 / −2.5 MB, reclaimed |
| Province cycle retention, once warm | ~8 KB/cycle |
| Steady state, this save | live 30.72 MB, frontier 34.43 MB |

Guest work per frame, same save: map 0.016M, province 0.21–0.86M.

ESP identifies the screen. `0x6fffee5c` is the map, `0x6fffc240` is
province; the long session showed six recurring values, and they are game
states, not stack drift. The `[health]` line's `esp` field is therefore a free
record of which screen each sample was taken on — used above to split trial 6
into visits.

## Where this landed

Idling allocates nothing (1, 2). The window mode allocates nothing (3). The
screen cycle saturates (6). Battles and save reloads saturate (7). Every
activity anyone has been able to isolate settles, and each one settles on the
same shape: a few large steps, then decay to nothing.

What that leaves is a plateau that depends on what you have done, not a
leak: 30.72 MB for realm↔province cycling, ~43.6 MB once battles are in the mix.
Both are comfortable against a 128 MB arena, and the frontier — which is what
actually runs into the end of it — was flat at 53.39 MB for the last six minutes
of the battle session.

The question the port needed
answered was "does a session survive long enough to play", and every measurement
says yes with wide margin.

Host RSS remains separately unattributed — +99 MB over the long session,
+60 MB over trial 7's 30 minutes. It is host-side, it is not the guest arena,
and nothing has been done about it — it's yet to cause any issues.

Also unexplained, and cheap to notice again: **`blk/frame` in province read 0.21
in trial 2 and 0.83–0.86 in trial 3** — 4× for nominally the same activity.

## The instrument defects these trials found

Notes on issues found while running these tests.

1. `exec_blocks()` was armed only by `THEOC_FPS` — and the *watchdog's*
   entire guest-running/stuck-host-side verdict reads it. A watchdog-only run
   would have called every stall host-side. Found by reading the arming path,
   not by being burned. See [diagnostics.md](diagnostics.md), "A counter nobody
   armed".
2. `TrapLayer::report()` printed to stdout while the documented capture
   recipe is `2>session.log` — so the long session exited normally and
   still has no end-of-run allocator state.
3. `[video] Alt+Enter` and `[click]` printed to stdout, same class. The
   trial-2 step could not be attributed to a fullscreen toggle from the log; the
   operator had to remember it.
4. Host RSS delta underflowed — `size_t` subtraction, so any dip below the
   first sample printed `+17592186044408.9 MB`.
5. Since-start rates are meaningless on a short run. The one-time ~27 MB load
   dominates and never washes out, so a ten-minute trial reported
   `+6498 MB/h -> 0.0 h headroom` while the live set was flat. Headroom is now
   withheld as `n/a (warm-up)` below 0.5 h.
6. Segments were cut on `up N.NNh`, a 36 s quantum, so up to two samples
   *before* a marker fell inside its segment. This put the +21.9 MB save load
   inside trial 6 and reported +30.65 MB instead of +6.28. Now cut on the
   frame counter, which is exact.
7. A double-tapped marker produced zero-duration segments — three forced
   samples in the same second, printing a row of zeroes that reads exactly like
   a measured null. Segments under 30 s are dropped.
8. …and the drop was silent — the fix for #7 was a bare
   `continue`, so a segment too short to carry a rate simply produced no row.
   To the operator that is indistinguishable from a segment that measured
   nothing, and the two call for opposite responses: *re-run it longer* versus
   *believe the null*. `segments()` now returns its drops and the table lists
   them with the sample count, the duration and the reason. The floor is three
   samples and 30 s — at `THEOC_LONGRUN=15` that means a segment shorter than
   about 45 s does not exist as far as the table is concerned, which is worth
   knowing before bracketing anything brief.

9. Reloads were inferred instead of read  — `reloads()` looked
   for a ≥5 MB fall in the live set. A quit-to-menu frees the world and the load
   allocates it straight back, both inside one 15 s sample, so the two cancel and
   nothing shows. On trial 7 that reported six real save reloads as none, and
   the session was written up as one continuous game until the operator said
   otherwise. The game prints `*** Scenario Load ***` on every load and the log
   already contained seven of them — the instrument was inferring a fact that was
   sitting in its own input. Now parsed directly. The lesson is the sharpest
   one in this file: prefer the event the system announces over a signature you
   derive from a sampled series, because the derivation fails silently and
   quietly rewrites what the session was.

The stream defects (#2, #3) were finally fixed as a class rather than one site
at a time — all 115 host `printf` sites in `port/src` now go to stderr, and only
guest output stays on stdout. See [diagnostics.md](diagnostics.md), "Which
stream: stdout is the guest's, stderr is ours".

## Open threads

Listed here only so that grepping the docs for "Open threads" finds them; the
substance is in [Where this landed](#where-this-landed) and is not repeated.

- **Which allocation site produces the ~18 KB/cycle.** Deliberately not chased —
  it is bounded, deterministic and host-independent (macOS and Linux agree to two
  decimals across 20 cycles, live *and* frontier), so it is the guest allocating
  rather than the port leaking. Needs an allocation-site histogram that does not
  exist. Reopen on evidence, not suspicion: see the reopen conditions above.
- **Host RSS**, separately unattributed and not guest-arena memory.
- **`blk/frame` in province reading 4× apart** between trials 2 and 3 for
  nominally the same activity.
