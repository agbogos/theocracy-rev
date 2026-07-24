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
  `SIGALRM` → `_TimerFunction` (`libmvos+0x922e0`) is the game's master tick
  (advances game timers, drives timed logic). On real Linux the **kernel**
  delivers this signal 30×/s regardless of what the process is doing — even
  mid-`usleep` (delivery interrupts the sleep with `EINTR`).

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
simulation/animation ran ~1.5× too fast.**

### Root cause
The engine **steps physics/animation once per rendered frame** (a common
2000-era assumption: "frame rate is fixed, so one step per frame"). It was tuned
for a capped frame rate; the *reason the frame limiter exists* is to hold that
rate constant across hardware. Rendering faster than the design cadence therefore
runs the sim faster. This is inherent to the engine, not our emulation — you
cannot have uncapped-smooth **and** correct-speed with a frame-tied sim without
decoupling them (which means patching game logic in `theocracy.real` — risky).

### Fix — cap the render rate to the design cadence
Clamp the **minimum** present interval to the game's own master-clock period
(default **33ms = 30Hz**, matching the `setitimer` rate — the most defensible
target since the engine is built around that clock). Measured *present-to-present*
so it only slows frames that are already too fast; the game's own `usleep` pacing
counts toward the interval and is never double-limited.

`port/src/traps.cpp`, in `HLE_SwapBuffers` after present:
```
static const int frame_ms = env THEOC_FRAME_MS ?: 33;   // 0 disables
if (frame_ms > 0 && elapsed_since_last_present < frame_ms) usleep(remainder);
```

Result: province **~30fps, heartbeat 30Hz**, sim stepping 30×/s (design rate).
`THEOC_FRAME_MS` tunes the cap; if 30fps is not exactly Woody-correct, the exact
target constant lives in the game's frame limiter (`theocracy.real:0x81da59b`) and
can be read by loading `theocracy.real` in Ghidra.

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
too low re-introduces underrun. Target is **~120ms** (`THEOC_AUDIO_MS`), which in
steady province holds the queue at ~0.08–0.19s (the ~91ms fragment granularity
sets the swing) with **0 underruns/s** — the low point is still ~5× the ~16ms
inter-yield gap. A brief blip remains during the **province-load spike** (~1s of
heavy asset loading at ~14fps where the emulator is genuinely compute-busy and
rarely yields) — transient, on screen entry, not the continuous stutter.

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
| `THEOC_FPS=1` | Per-second frame instrument: fps, guest blocks/frame, blocks/sec (saturation check), heartbeat & mixer rates, `usleep`/`gettimeofday`/`select` rates, audio queue depth & underruns/s. The tool that split throughput-vs-timing. |
| `THEOC_PROFILE=1` | Size-weighted guest basic-block histogram, rolling top-15 every 3s (labelled `game`/`mvos+off`). Found the hot blit functions. |
| `THEOC_AUTO_PROVINCE=1` | Self-drives menu → Prophecy → OK into province view (wall-clock scheduled) for unattended timing tests. |
| `THEOC_FRAME_MS=N` | Frame-rate cap in ms (default 33 = 30fps; 0 disables). |
| `THEOC_AUDIO_MS=N` | Mixer queue target = audio latency in ms (default 120). Lower = less latency, more underrun risk. |
| `THEOC_LEGACY_SLEEP=1` | Revert Bug-1 fix (blind `usleep`, heartbeat only from present). |
| `THEOC_NATIVE_BLIT=0` | Revert to emulated libmvos rasteriser. |

## The general lesson

Under this emulator, **wall-clock-shaped bugs masquerade as performance bugs.**
The decisive question is never "what's hot?" but "**is the emulator saturated or
idle?**" — if idle while slow, the bottleneck is a host-side wait we synthesised
(a sleep, a signal we didn't deliver, a clock we advanced too slowly), not the
guest's compute. The `THEOC_FPS` blocks/sec figure answers that in one line.
