# Frame timing: the present-coupled timer and frame-tied simulation

This documents a subtle, high-impact class of bug in the guest-libmvos emulator:
**the game's real-time behaviour depends on host-side timing that our HLE has to
synthesise**, and getting it wrong produces symptoms that *look* like performance
problems but are not. Province view was the case that exposed it.

Two distinct bugs, discovered in sequence:

1. **Present-coupled heartbeat** → province stuck at ~12fps (looked like a slow
   renderer; was actually a stalled clock).
2. **Frame-tied simulation** → once (1) was fixed, province ran at ~40fps but the
   whole sim ran ~1.5× too fast ("turbo").

Both are inherent to a 2000-era Linux game that assumes a real POSIX interval
timer and a roughly fixed frame rate.

---

## Background: how the game paces itself

`theocracy.real` + `libmvos.so` pace themselves with two independent clocks:

- **The heartbeat.** `setitimer(ITIMER_REAL, 33ms)` arms a **30Hz** timer;
  `SIGALRM` → `_TimerFunction` (`libmvos+0x922e0`). On real Linux the **kernel**
  delivers this signal 30×/s regardless of what the process is doing — even
  mid-`usleep` (delivery interrupts the sleep with `EINTR`).

  > **Correction (2026-08-03).** This used to read "the game's master tick
  > (advances game timers, drives timed logic)". That is **wrong**, and it made
  > the heartbeat look far more load-bearing than it is. `_TimerFunction` is a
  > one-line wrapper around `cTimerSystem_Linux::Proc`, which dispatches to the
  > **single** `cVTimer` registered at `+0x1c` — `_ActivateTimer` is an
  > exclusive one-slot registration, not a list. In game that timer is
  > `cIntuition`, whose `TimerProc` advances the **cursor click animation** and
  > refreshes the **cursor sprite**; during animation playback `cFLCAnimPlayer`
  > or `cAnimSkeleton` takes the slot instead. Not one of them reads a clock,
  > and none of them touch the simulation. See "What the heartbeat actually
  > drives" below.

- **The frame limiter.** The game's main loop renders a frame, then calls
  `cSyncSystem::Sleep(us)` (`libmvos+0x95b50`, a thin `usleep` wrapper) to pad out
  to its target frame period. The sleep duration is computed in the game binary
  (caller `theocracy.real:0x81da59b`).

Our HLE has **no real signal delivery and no host thread running the guest** —
Unicorn is single-threaded and *nested* `uc_emu_start` crashes it. So the
heartbeat can only be serviced when the guest voluntarily yields to a trap. The
original design serviced it **only at `SwapBuffers`/present** (`maybe_redirect_timer`),
via `redirect_guest` (splice `_TimerFunction` into the current emulation instead of
nesting). See [guest-libmvos.md](guest-libmvos.md) for the green-run mechanism.

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
- **Guest work is ~1.5M blk/s — *lower* than the idle menu.** The emulator is
  ~95% idle, doing almost no computation. **Not throughput-bound.**
- **~850ms of every second is spent in `usleep`** — ~12 calls/frame-sec of ~68ms
  each. The wall-clock is going into *host sleep*, not work.
- **Heartbeat is 6Hz, not the armed 30Hz.**

The `usleep` caller was `cSyncSystem::Sleep`, driven from the game's frame limiter
(`theocracy.real:0x81da59b`), requesting a steady **~68ms** per frame.

### Root cause — a circular timing dependency
- The frame limiter sleeps waiting for the game clock to reach the next frame
  deadline. The game clock is advanced by the **heartbeat**.
- The heartbeat only fires **from present**, at most once per rendered frame.
- Present only happens after the limiter's sleep ends.

So: *sleep waits for a tick → tick needs a present → present needs the sleep to
end.* The loop settles at a slow equilibrium (~12fps / 6Hz). On Woody the kernel
delivers `SIGALRM` at 30Hz independent of rendering, so the wait resolves in
~33ms and province runs normally. **Our present-coupled timer is the whole bug.**

### Fix — deliver the heartbeat during `usleep` (real Linux `EINTR` semantics)
On Linux, `SIGALRM` *interrupts* `usleep` to run the handler. We reproduce that:
in the `usleep` trap, if a timer tick is due, deliver it right there (same
`redirect_guest` splice present uses) instead of sleeping through it; and never
sleep past the next tick deadline, so the 30Hz cadence is preserved and the loop
re-evaluates promptly.

`port/src/traps.cpp`, `t["usleep"]`:
```
if (!legacy && maybe_redirect_timer(m, esp)) return 0;   // tick due → deliver (EINTR)
// else sleep, bounded to time-until-next-tick, so the clock keeps 30Hz cadence
```

Result: province **12fps → ~40fps**, heartbeat back to a solid **30Hz**.
`THEOC_LEGACY_SLEEP=1` reverts to the old blind sleep for A/B.

> Corollary: this fixes the game clock **everywhere**, not just province — any
> screen that was heartbeat-starved now ticks at the correct rate.

---

## Bug 2 — frame-tied simulation ("turbo" province)

### Symptom
With Bug 1 fixed, province rendered smoothly at ~40fps, but **the whole
simulation/animation ran too fast** ("turbo").

### Root cause
The engine **steps physics/animation once per rendered frame** (a common
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
(The `40000`/`30000` constants just below are unrelated: the async asset-streaming
budget — spend up to 40ms/frame preloading bitmaps — which is also what causes the
province-**entry** load spike.) This means the *original* 11.9fps we measured was
the **designed** rate — province was never too slow in frame rate; what read as
"slow/bad" was Bug 1's 6Hz heartbeat (laggy input/UI), which Bug 1 fixes
independently. On Woody it's **12fps render + async 30Hz heartbeat**, the two
decoupled by the kernel; our single-threaded emulator cannot run them independently.

### Fix — cap the render rate to the designed cadence
Clamp the **minimum** present interval to the game's own limiter period
(default **83ms = 12fps**; `THEOC_FRAME_MS` overrides, 0 disables). Measured
*present-to-present* so it only slows frames that are already too fast; the game's
own `usleep` pacing counts toward the interval and is never double-limited.

`port/src/traps.cpp`, in `HLE_SwapBuffers` after present:
```
static const int frame_ms = env THEOC_FRAME_MS ?: 83;   // 0 disables
if (frame_ms > 0 && elapsed_since_last_present < frame_ms) usleep(remainder);
```

Result: province **12fps with correct sim speed** — faithful to the original.
12fps is choppy by modern standards but authentic; the proper way to get
smooth-**and**-correct is to decouple the sim step from the render frame (render at
30fps, step the sim at 12Hz), which means patching the frame-tied stepping in
`theocracy.real` — tracked as a future task in `../../task_fifo.md`.

---

## Bug 3 — audio mixer coupled to frame rate (stutter)

### Symptom
After the frame cap, audio **stuttered**, worst at low fps, still faintly audible
at 30fps. `THEOC_FPS` `underrun=N/s` (callback samples pulled from an empty queue)
made it objective.

### Root cause
The soft-threaded mixer (`cSoundCard_Linux` Main, one-shot patched) is green-run to
produce one OSS fragment (~91ms of audio) per call — and was serviced **only at
present**, gated to one fragment / 90ms (~11/s). The drain is 44.1k int16/s ≈ one
91ms fragment every 90ms — so production == consumption with **zero margin**, and
any servicing jitter (worse as the present grid coarsens at low fps) underruns.

Same shape as the heartbeat: a real-time obligation tied to the render loop.

### Fix — buffer-driven, serviced off present *and* usleep
- Drive the mixer by **queue level** (top up whenever it drains below a target),
  not a fixed clock — a light 15ms floor only prevents re-firing every yield.
- Service it from **`usleep` as well as present**, so throughput is decoupled from
  fps (the same extra yield points the heartbeat fix uses).

**The queue depth *is* the audio latency**, so the target is a direct
latency↔margin tradeoff: too high delays SFX (0.5s buffer = 0.5s lag — audible),
too low re-introduces underrun. Target is **~120ms** (`THEOC_AUDIO_MS`), which
holds the queue at ~0.16–0.21s with **0 underruns/s** even at the 12fps default
(the buffer absorbs the sparse yields — the mixer stays ~11/s, decoupled from the
frame rate). A brief blip remains during the **province-load spike** (~1s of heavy
asset loading where the emulator is genuinely compute-busy and rarely yields) —
transient, on screen entry, not the continuous stutter.

## What the heartbeat actually drives — and why "real threads" was closed

The three bugs above left a follow-up on the worklist for a year: *real threads /
signal delivery — no multi-tick catch-up when frames stall*. Closed 2026-08-03
**as a non-issue**, once the chain was actually read rather than assumed. It is
worth recording why, because the premise was wrong in a way that made the port
look fragile where it is not.

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
`cVTimer` owns SIGALRM at any moment. **None of the three handlers reads a
clock.** The heartbeat is the *cursor and animation* tick. It is not, and never
was, the simulation's clock.

**The simulation clocks itself, and already has catch-up.**
`SimulationUpdate` (`theocracy.real:0x81f97e0`, see
[game-loop-and-simulation.md](../subsystems/game-loop-and-simulation.md)) runs
from `RealmGameLoop` once per frame and computes its own work:

```
ticks = elapsed(world+0x1410) / tickDuration(world+0x1408)
if (ticks > 10) ticks = 10          // anti "spiral of death" clamp
while (ticks--) SimulationStep(g_World)
```

That is a fixed timestep with bounded multi-tick catch-up, **in the game, driven
by elapsed wall-clock**. A stalled frame is absorbed there and nowhere else.

**So the two halves of the old item dissolve differently:**

- **Multi-tick catch-up — nothing to build.** `maybe_redirect_timer` collapses
  backlog to one call, and what that costs is *cursor-animation frames*, not sim
  ticks. Cosmetic, self-correcting (`MouseRefresh` re-reads the live pointer
  position on the next tick), and already covered by a host fallback —
  `tick_pointer_click_anim`, run every present for exactly this case. The thing
  that genuinely needs catch-up has its own, and it is better than anything we
  would have added.
- **Real host threads — infeasible by construction, not merely unnecessary.**
  There is one `uc_engine`; nested `uc_emu_start` crashes Unicorn (the entire
  reason `redirect_guest` exists), and driving one engine from two host threads
  is not safe either. A second engine cannot share the guest address space. The
  mixer therefore cannot become a host thread without replacing the emulator —
  and it does not need to: it is buffer-driven with a ~120 ms cushion and
  measures **0 underruns/s** even at the 12 fps default.

**Where the requirement came from.** It is a leftover from the superseded
*pure-HLE native-replace* plan, where the host would have reimplemented libmvos
in C++ and would then have owned real threads and real signal delivery as
genuine obligations. Under guest-libmvos the guest owns its own pacing, and the
obligation moved with it. That is the same pathology as the "abandoned guest
SwapBuffers path" item — an inherited premise that no longer described the
system, surviving because nobody re-read it.

**Reopen on evidence:** a cursor that visibly stalls or lags its true position
during heavy load, or a `cVTimer` other than the three above turning up in the
`+0x1c` slot with real work in its `vt+0xc`.

## Why the native blit work still mattered

Before the timing bugs were understood, the province cost was (correctly)
attributed by the profiler to libmvos's software RGB565 rasteriser, and the whole
**LFB16 blit family was reimplemented natively** (`port/src/blit.cpp`): entry-point
code-hook overrides for `LFB16_PutBitmap8C1_AMask`, `PutBitmap8`, `PutBitmap8_AMask`,
`PutBitmap`, `VLineAlfa` — byte-exact transliterations of the Ghidra decompiles,
running at host speed instead of emulated pixel loops. (All are plain cdecl despite
Ghidra's `__regparm` labels — every arg is read from the stack.)

That work removed real per-frame CPU cost and is correct and kept — it just was
**not** the province bottleneck (a stalled clock was). It matters for headroom and
for any genuinely blit-bound screen, and it is the first instance of the
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

## The general lesson

Under this emulator, **wall-clock-shaped bugs masquerade as performance bugs.**
The decisive question is never "what's hot?" but "**is the emulator saturated or
idle?**" — if idle while slow, the bottleneck is a host-side wait we synthesised
(a sleep, a signal we didn't deliver, a clock we advanced too slowly), not the
guest's compute. The `THEOC_FPS` blocks/sec figure answers that in one line.
