# Frame timing: the present-coupled timer and frame-tied simulation

The game's real-time behaviour depends on host-side timing that our HLE has to
synthesise, and getting it wrong produces symptoms that *look* like
performance problems but are not. Province view was the case that exposed it.

Two distinct bugs, discovered in sequence:

1. **Present-coupled heartbeat** → province stuck at ~12fps (looked like a slow
   renderer; was actually a stalled clock).
2. **Frame-tied simulation** → once (1) was fixed, province ran at ~40fps but the
   whole sim ran ~1.5× too fast.

Both are inherent to a 2000-era Linux game that assumes a real POSIX interval
timer and a roughly fixed frame rate.

---

## Background: how the game paces itself

`theocracy.real` + `libmvos.so` pace themselves with two independent clocks:

### Heartbeat

`setitimer(ITIMER_REAL, 33ms)` arms a **30Hz** timer.

`SIGALRM` → `_TimerFunction` (`libmvos+0x922e0`).

On real Linux the kernel delivers this signal 30×/s regardless of what the process is doing — even
mid-`usleep` (delivery interrupts the sleep with `EINTR`).

`_TimerFunction` is a one-line wrapper around `cTimerSystem_Linux::Proc`, which dispatches to the
**single** `cVTimer` registered at `+0x1c` — `_ActivateTimer` is an
exclusive one-slot registration, not a list. In game that timer is
`cIntuition`, whose `TimerProc` advances the **cursor click animation** and
refreshes the **cursor sprite**; during animation playback `cFLCAnimPlayer`
or `cAnimSkeleton` takes the slot instead. Not one of them reads a clock,
and none of them touch the simulation. See "What the heartbeat actually
drives" below.

#### The frame limiter

The game's main loop renders a frame, then calls
  `cSyncSystem::Sleep(us)` (`libmvos+0x95b50`, a thin `usleep` wrapper) to pad out
  to its target frame period. The sleep duration is computed in the game binary
  (caller `theocracy.real:0x81da59b`).

Our HLE has no real signal delivery and no host thread running the guest —
Unicorn is single-threaded and *nested* `uc_emu_start` crashes it. So the
heartbeat can only be serviced when the guest voluntarily yields to a trap. The
original design serviced it only at `SwapBuffers`/present
(`maybe_redirect_timer`), via `redirect_guest` (splice `_TimerFunction` into the
current emulation instead of nesting). See [guest-libmvos.md](guest-libmvos.md)
for the green-run mechanism.

---

## Bug 1 — present-coupled heartbeat (province 12fps)

### Symptom
Province view ran at a rock-steady **11.9fps**; map view and menus were fine.
Optimising the renderer (native LFB16 blit, see below) removed real CPU work but
**did not change the frame rate at all** — the first sign it was not throughput.

### Diagnosis (the `THEOC_FPS` instrument)
A per-second frame instrument (guest-blocks/frame, blocks/sec, heartbeat rate,
`usleep`/`gettimeofday` rates) showed province steady-state:

```
[fps] 11.9 fps | guest 1.6M blk/s (0.13M/frame) | heartbeat 6/s mixer 6/s
      sleep 850ms/s in 12 usleep | gettimeofday 83/s | select 0/s
```

Reading this:
- Guest work is ~1.5M blk/s — *lower* than the idle menu. The emulator is
  ~95% idle, doing almost no computation. **Not throughput-bound.**
- **~850ms of every second is spent in `usleep`** — ~12 calls/frame-sec of ~68ms
  each. The wall-clock is going into *host sleep*, not work.
- **Heartbeat is 6Hz, not the armed 30Hz.**

The `usleep` caller was `cSyncSystem::Sleep`, driven from the game's frame
limiter (`theocracy.real:0x81da59b`), requesting a steady **~68ms** per frame.

### Root cause — a circular timing dependency
- The frame limiter sleeps waiting for the game clock to reach the next frame
  deadline. The game clock is advanced by the **heartbeat**.
- The heartbeat only fires **from present**, at most once per rendered frame.
- Present only happens after the limiter's sleep ends.

So: *sleep waits for a tick → tick needs a present → present needs the sleep to
end.* The loop settles at a slow equilibrium (~12fps / 6Hz).

On something like Debian Woody (~2002) the kernel
delivers `SIGALRM` at 30Hz independent of rendering, so the wait resolves in
~33ms and province runs normally.

Our artificial present-coupled timer is the whole bug.

### Fix — deliver the heartbeat during `usleep` (real Linux `EINTR` semantics)
On Linux, `SIGALRM` *interrupts* `usleep` to run the handler. We reproduce that:
in the `usleep` trap, if a timer tick is due, deliver it right there (same
`redirect_guest` splice present uses) instead of sleeping through it; and never
sleep past the next tick deadline, so the 30Hz cadence is preserved and the loop
re-evaluates promptly.

`port/src/traps.cpp`, `t["usleep"]`:
```
// tick due → deliver (EINTR)
// else sleep, bounded to time-until-next-tick, so the clock keeps 30Hz cadence
if (!legacy && maybe_redirect_timer(m, esp)) return 0;
```

Result: province **12fps → ~40fps**, heartbeat back to a solid **30Hz**.
`THEOC_LEGACY_SLEEP=1` reverts to the old blind sleep for A/B.

> Corollary: this fixes the game clock **everywhere**, not just province — any
> screen that was heartbeat-starved now ticks at the correct rate.

#### The slice loop is self-limiting

Per-slice sleep overshoots do not accumulate across a
frame. The loop charges *real elapsed* time against the remaining budget and
recomputes the next slice, so one slice that overshoots by 20 ms consumes the
rest of the frame instead of extending it. The frame floor is the budget plus
roughly one overshoot, not N of them.

Measured on a Windows VM under deliberate CPU contention: between one spinning
thread per core and two per core, the **single-shot sleep median degraded 4×**
(6.6 → 26.7 ms) while the **province frame moved 0.9 ms** (97.9 → 98.8). It also
means a host-timing budget expressed as `slices × overshoot` is the wrong model
— a mistake this project made when writing the acceptance criteria for that very
run. See [other-os-ports.md](other-os-ports.md), "The contention runs".

`THEOC_FPS=1` now reports both terms directly as `(N slices/frame, +M ms each)`;
[diagnostics.md](diagnostics.md), "Reading the sleep slices", has the per-host
reference figures and the warning that overshoot must be measured in the played
configuration or not at all.

---

## Bug 2 — frame-tied simulation

### Symptom
With Bug 1 fixed, province rendered smoothly at ~40fps, but the whole
simulation/animation ran too fast.

### Root cause
The engine steps physics/animation once per rendered frame (a common
2000-era assumption: "frame rate is fixed, so one step per frame"). Bug 1's fix
sped rendering far past the frame rate the sim was tuned for, so the sim ran
proportionally fast. This is inherent to the engine, not our emulation.

### The designed frame rate — read from the game binary
`cProvince::Do` (`theocracy.real:0x081da59b`, the province tick) contains the
frame limiter, and its target constant is explicit:

```c
Set__8cDayTimell(&target, 0, 0x14585);              // 0x14585 = 83333 µs
if (elapsed_since_last_frame < target)
    Sleep__11cSyncSystemUl(target - elapsed);       // sleep the remainder
```

**`0x14585` = 83,333 µs = exactly 1/12 s → province is designed for 12fps.**
(The `40000`/`30000` constants just below are unrelated: the async
asset-streaming budget — spend up to 40ms/frame preloading bitmaps — which is
also what causes the province-**entry** load spike.) 

On Woody it's **12fps render + async 30Hz
heartbeat**, the two decoupled by the kernel; our single-threaded emulator
cannot run them independently.

### Fix — cap the render rate to the designed cadence
Clamp the **minimum** present interval to the game's own limiter period (default
**83ms = 12fps**; `THEOC_FRAME_MS` overrides, 0 disables). Measured
*present-to-present* so it only slows frames that are already too fast; the
game's own `usleep` pacing counts toward the interval and is never
double-limited.

`port/src/traps.cpp`, in `HLE_SwapBuffers` after present:
```
static const int frame_ms = env THEOC_FRAME_MS ?: 83;
// 0 disables if (frame_ms > 0 && elapsed_since_last_present < frame_ms) usleep(remainder);
```

Result: province **12fps with correct sim speed** — faithful to the original.

### Bug 2, revisited

The frame limiter in `cProvince_Do` is `Sleep(83333µs − elapsed)`. It was never
broken. **Our `usleep` handler was truncating it.** Bug 1's fix had taught
`usleep` to deliver a due heartbeat by returning immediately (EINTR semantics),
and otherwise to sleep no further than the next tick deadline — so a requested
83 ms sleep returned after at most ~33 ms. `cSyncSystem::Sleep` does not loop,
so `cProvince_Do` returned early and re-ran, ~2.5× too often. That is the whole
of the faster-than-intended simulation rate.

The global present clamp was a second limiter bolted on to hold
back the first one we had disabled.

#### Fix — honour the full sleep, and deliver ticks during it.

A real kernel
fires SIGALRM ~3 times inside an 83 ms `usleep` without shortening it. We could
not: a spliced tick has to *return through the trap*, so delivering one meant
abandoning the sleep. The way out is to make it return to the **usleep trap**
instead of to usleep's caller, and keep the remainder in host state:

```
usleep(83333) → sleep 33ms → tick due → splice _TimerFunction ─┐
                                                                │ returns to
              ← ─────────── usleep trap re-entered ─────────────┘ the trap
              → sleep 33ms → tick due → splice ──→ re-entered
              → sleep 17ms → remaining == 0 → return to the real caller
```

Full duration *and* a 30 Hz heartbeat, with **no guest patch**. The frame is
tight: `_TimerFunction` returns through `esp-4`, so its `signo` argument lands
on the same dword the re-entered trap reads as its return address — a `ret` pops
4 and a cdecl arg sits at `+4`, so the two slots cannot be separated without a
trampoline. That is safe only because the stock `_TimerFunction` never reads its
parameter (verified in the disassembly), so `timer_handler_ignores_signo()`
gates it and anything else falls back to the old truncating delivery.

Sound is deliberately **not** serviced mid-sleep: its entry argument (the
`cThread*`) occupies that same aliased slot and cannot survive the re-entry. It
is serviced once on entry instead, which its ~120 ms buffer absorbs.

**Measured, first run.** Province:

```
[fps] 12.4 fps | heartbeat 30/s mixer 10/s | sleep 605ms/s in 43 usleep | underrun=0/s
```

With **43 `usleep` calls for 12 frames**. The game issues
one `usleep` per frame; each is being split into ~3.5 slices by tick delivery,
and every re-entry counts as a fresh call — 12 × 3.5 ≈ 43. That is the splice
working, and `heartbeat 30/s` alongside `sleep 605ms/s` is the thing the old
design could not do: full-duration sleeps *and* a 30 Hz tick.

---

> If you're reading this you're more interested in the game than I am.
> This is genuinely technical and while it was immensely interesting for me,
> I can say that this is also immensely, mind-numbingly boring.

---

This is what makes the host's sleep primitive load-bearing. ~3.5 slices
per frame means the requested duration is uniform over `(0, 33.3 ms]` and the
tail slice before each tick is *routinely sub-millisecond*. A host that cannot
express that does not add jitter — it overshoots the deadline the slice
existed to stop at, and because the limiter is elapsed-based the error lands
on **game speed**. On POSIX `usleep` resolves this and there is nothing to do;
on Windows the default ~15.6 ms scheduler tick made the province frame 94 ms
against 83.3 ms until `theoc_sleep_us()` was built on a high-resolution
waitable timer. Measurements and the design in
[other-os-ports.md](other-os-ports.md), "The probe's answer".

**And it exposed a wrong assumption.** Realm reported `sleep 0ms/s in 0 usleep`
— `RealmGameLoop` **never calls the frame limiter at all**. It has no pacing of
its own, so "let realm run at its natural rate" was meaningless: uncapped it
free-ran to **~100fps** at 0.01–0.04M blocks/frame, and would climb further on
faster hardware. Realm was never *paced* at 12fps by anything but our clamp.

So the clamp stays, re-purposed: `THEOC_FRAME_MS` now defaults to 16ms
(~60fps) as a ceiling rather than 83ms as a pacer. It never fires in province,
which self-limits to 83ms through its own code, so it bounds the unlimited
screen without touching the limited one. `THEOC_FRAME_MS=83` restores the old
global clamp, `0` is genuinely uncapped, and `THEOC_LEGACY_SLEEP=1` reverts the
sleep handling entirely.

Entering province still spikes — one sample at
`underrun=15015/s`, `heap +6.04MB/s`, `heartbeat 10/s` — during the ~1s of
asset loading where the emulator is genuinely compute-bound and rarely yields.
Pre-existing and already documented; it recovers to 0 underruns immediately.

12fps in province is choppy but **authentic**. Making it smooth-and-correct was
the remaining half of the task. It is now a **won't-do** — see below.

## Why province stays at 12fps

The plan was to modernize: render province at ~30fps, step the simulation at 12Hz. Attempted four
approaches, all closed on evidence rather than on effort estimates.

Left purely for maintaining as a written record.

1. Run the sim at 30Hz with scaled per-tick deltas — **impossible.**

    There are no deltas. `cMan::Do` (`0x80a0c60`, **24.6 KB**) is the per-unit update, reached
    from `cProvince_Do` via 11× `cManList::Do` (`0x8147c40`), and **not one xref to
    `SetBySys__8cDayTime` falls inside its range** — nor inside `cManList::Do`.
    Province movement and animation never read wall-clock; they advance by fixed
    increments per call. (Contrast `SimulationUpdate`/`SimulationStep`, which call
    it three times each — the *realm* sim is time-based. Province is not.)

2. Skip the sim body on some frames — **worse than doing nothing.**

    Cheap to build: `cProvince_Do` already has a `LAB_081daefb` path that jumps past the AI
    block to the phase counters, and the two animation phase counters
    (`DAT_084c76e4` cycling 0–5, `DAT_084c76e6` toggling) are written *only* in
    `cProvince_Do` and read *only* by `FUN_08099c00`. So terrain phases would
    animate at 30Hz — while units froze between sim steps. Smooth background with
    12Hz units teleporting across it reads worse than honest 12fps.

3. Interpolate at render time — **out of scope by an order of magnitude.**

    Snapshot every unit's position per step and lerp during paint, i.e. build an
    interpolating renderer into an engine with no concept of one, from outside,
    against 24.6 KB of unit state we do not understand.

4. Scale the balance data instead of the code — **the real reason it dies.**

    Run at 30fps and multiply every rate-like quantity by 0.4 so things move at the
    correct real-world speed in smaller steps. `selap.txt` is internally consistent
    about units once you know the game — three time bases, three subsystems:

| Base Units | Examples | Owner |
|---|---|---|
| Real seconds | `INFO_SCROLL_PIXEL_PER_SEC=15` | the scrolling event-narration widget, separately clocked |
| Game calendar | `SCN_3T01_STRIKEDELAY_DAY=175`, `SCN_5T01_TIMEOUT_YEAR=10` | realm-screen world events |
| **Frames** | `Moon3_WoundFrame=10`, `Nature5_HealFrame=6` | **province — the targets** |

The remaining bare integers (`ARROW_SPEED=140`, `Moon1_ConcTime=10`,
`TRIBE0_CAST_TIME=900`, `PRIEST_GUARD_DELAY=24`, `Soul1_EndTime=23`) are almost
certainly frame counts too. So the blocker is **not** ambiguity. It is scale and
representation:

- **Scale.** These are hundreds of interdependent *balance* levers — spell
  durations, cast times, projectile speeds, guard delays, damage-over-time and
  healing periods. Multiplying them is not a mechanical edit, it is a rebalance,
  and verifying it means playing the game.
- **Representation.** The values are integers. `Nature5_HealFrame=6 × 0.4 = 2.4`
  does not exist in this format, and neither does a fractional accumulator in the
  engine to consume it. Every such value would have to be rounded — reintroducing
  exactly the drift the exercise was meant to remove — or the engine taught
  fractions, which is the "rewrite the game natively" project.

The engine was built with the specific frame rate in mind.
The cursor was given its **own 30Hz timer** (`setitimer` → the
single `cVTimer`, i.e. `cIntuition::TimerProc`; see "What the heartbeat actually
drives") *because the engine supports nothing but 12Hz*. Province at 12fps is not a limitation we inherited by
accident; it is the engine's intended sim rate and frame rate, and the separate
cursor timer is the evidence.

**What remains available** is the frame limiter constant itself
(`Set__8cDayTimell(&target, 0, 0x14585)` in `cProvince_Do`). Patching it changes
sim rate and frame rate together, which is what the engine's design allows and
all it allows. That is shipped as `THEOC_PROVINCE_MS`:

```
0x81da529:  68 85 45 01 00   push 0x14585   <- the 4 bytes at 0x81da52a
0x81da52e:  31 db            xor  ebx,ebx
0x81da530:  6a 00            push 0         ; high dword of the long long
0x81da532:  56               push esi       ; &target
0x81da533:  e8 2c 56 e7 ff   call Set__8cDayTimell@plt
```

The byte string `68 85 45 01 00` occurs **exactly once** in the whole 24 MB
image, so the site is unambiguous — the installer still reads the operand back
and refuses to write unless it is the expected `0x14585`, because a silent
mismatch would retune something else entirely. `33` → ~30fps at ~2.5× speed,
`166` → ~6fps at ~0.5×. Unset leaves the shipped 12fps alone.

Tested by hand at various settings; all report and behave
correctly, at the expected speeds. The judgement that came out of it:
`THEOC_PROVINCE_MS=50` — 20fps at 1.67× — is where it stops reading as a
2000-era game without yet reading as fast-forward. That is a subjective call
and is recorded as one, but it is the only figure here anybody actually played
to, and to be clear the sweet spot is *not* 30fps: the speed-up you
have to accept to get there is what spoils it, not the frame rate.

---

## Bug 3 — audio mixer coupled to frame rate (stutter)

### Symptom
After the frame cap, audio **stuttered**, worst at low fps, still faintly
audible at 30fps. `THEOC_FPS` `underrun=N/s` (callback samples pulled from an
empty queue) made it objective.

### Root cause
The soft-threaded mixer (`cSoundCard_Linux` Main, one-shot patched) is green-run
to produce one OSS fragment (~91ms of audio) per call — and was serviced **only
at present**, gated to one fragment / 90ms (~11/s). The drain is 44.1k int16/s ≈
one 91ms fragment every 90ms — so production == consumption with **zero
margin**, and any servicing jitter (worse as the present grid coarsens at low
fps) underruns.

Same shape as the heartbeat: a real-time obligation tied to the render loop.

### Fix — buffer-driven, serviced off present *and* usleep
- Drive the mixer by queue level (top up whenever it drains below a target),
  not a fixed clock — a light 15ms floor only prevents re-firing every yield.
- Service it from `usleep` as well as present, so throughput is decoupled from
  fps (the same extra yield points the heartbeat fix uses).

The queue depth *is* the audio latency, so the target is a direct
latency↔margin tradeoff: too high delays SFX (0.5s buffer = 0.5s lag — audible),
too low re-introduces underrun. Target is ~120ms (`THEOC_AUDIO_MS`), which
holds the queue at ~0.16–0.21s with 0 underruns/s even at the 12fps default
(the buffer absorbs the sparse yields — the mixer stays ~11/s, decoupled from
the frame rate). A brief blip remains during the province-load spike (~1s of
heavy asset loading where the emulator is genuinely compute-busy and rarely
yields) — transient, on screen entry, not the continuous stutter.

## What the heartbeat actually drives — and why "real threads" was closed

**The SIGALRM chain, end to end:**

```
setitimer(ITIMER_REAL, 33ms) → SIGALRM
  └─ _TimerFunction                    (0x922e0) — one line
       └─ cTimerSystem_Linux::Proc     (0x925f0) — if (this+0x1c) vt+0xc
            └─ the one registered cVTimer:
                 cIntuition::TimerProc (0x8d640) — click anim + cursor Refresh
                 cFLCAnimPlayer / cAnimSkeleton  — during animation playback
```

`cTimerSystem_Linux::_ActivateTimer` (`0x92590`) writes a **single** slot at
`+0x1c` and succeeds only when no timer is already armed, so exactly one
`cVTimer` owns SIGALRM at any moment. None of the three handlers reads a
clock. The heartbeat is the *cursor and animation* tick.

The simulation clocks itself, and already has catch-up. `SimulationUpdate`
(`theocracy.real:0x81f97e0`, see
[game-loop-and-simulation.md](../subsystems/game-loop-and-simulation.md)) runs
from `RealmGameLoop` once per frame and computes its own work:

```
ticks = elapsed(world+0x1410) / tickDuration(world+0x1408)
if (ticks > 10) ticks = 10          // anti "spiral of death" clamp
while (ticks--) SimulationStep(g_World)
```

That is a fixed timestep with bounded multi-tick catch-up, in the game, driven
by elapsed wall-clock. A stalled frame is absorbed there and nowhere else.

- Multi-tick catch-up — nothing to build. `maybe_redirect_timer` collapses
  backlog to one call, and what that costs is *cursor-animation frames*, not sim
  ticks. Cosmetic, self-correcting (`MouseRefresh` re-reads the live pointer
  position on the next tick), and already covered by a host fallback —
  `tick_pointer_click_anim`, run every present for exactly this case. The thing
  that genuinely needs catch-up has its own, and it is better than anything we
  would have added.
- Real host threads — infeasible by construction, not merely unnecessary.
  There is one `uc_engine`; nested `uc_emu_start` crashes Unicorn (the entire
  reason `redirect_guest` exists), and driving one engine from two host threads
  is not safe either. A second engine cannot share the guest address space. The
  mixer therefore cannot become a host thread without replacing the emulator —
  and it does not need to: it is buffer-driven with a ~120 ms cushion and
  measures **0 underruns/s** even at the 12 fps default.

## Why the native blit work still mattered

Before the timing bugs were understood, the province cost was (correctly)
attributed by the profiler to libmvos's software RGB565 rasteriser, and the
whole LFB16 blit family was reimplemented natively (`port/src/blit.cpp`):
entry-point code-hook overrides for `LFB16_PutBitmap8C1_AMask`, `PutBitmap8`,
`PutBitmap8_AMask`, `PutBitmap`, `VLineAlfa` — byte-exact transliterations of
the Ghidra decompiles, running at host speed instead of emulated pixel loops.
(All are plain cdecl despite Ghidra's `__regparm` labels — every arg is read
from the stack.)

That work removed real per-frame CPU cost and is correct and kept — it just was
**not** the province bottleneck (a stalled clock was). It matters for headroom
and for any genuinely blit-bound screen, and it is the first instance of the
incremental native-override seam. `THEOC_NATIVE_BLIT=0` disables it.

---

## Diagnostics built (all env-gated, zero cost when off)

| Env | What it does |
|-----|--------------|
| `THEOC_FPS=1` | Per-second frame instrument: fps, guest blocks/frame, blocks/sec (saturation check), heartbeat & mixer rates, `usleep`/`gettimeofday`/`select` rates, audio queue depth & underruns/s, guest heap used + growth rate. The tool that split throughput-vs-timing; the heap column also found the G14 `cIntuition` corruption. |
| `THEOC_PROFILE=1` | Size-weighted guest basic-block histogram, rolling top-15 every 3s (labelled `game`/`mvos+off`). Found the hot blit functions. |
| `THEOC_AUTO_PROVINCE=1` | Self-drives menu → Prophecy → OK into province view (wall-clock scheduled) for unattended timing tests. |
| `THEOC_FRAME_MS=N` | Frame-rate cap in ms (default 83 = 12fps, the designed province rate; 0 disables). |
| `THEOC_AUDIO_MS=N` | Mixer queue target = audio latency in ms (default 120). Lower = less latency, more underrun risk. |
| `THEOC_LEGACY_SLEEP=1` | Revert Bug-1 fix (blind `usleep`, heartbeat only from present). |
| `THEOC_NATIVE_BLIT=0` | Revert to emulated libmvos rasteriser. |
