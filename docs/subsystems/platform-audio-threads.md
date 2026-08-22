# Platform backends: audio, threads, processes (libmvos)

> **This doc covers sound effects only.** Theocracy's *music* is Redbook CD
> audio on a separate path (`cVCD`, `/dev/cdrom`) that shares nothing with the
> sample mixer below — see [music-and-redbook.md](music-and-redbook.md). Note in
> particular that `cVCDThread` puts `cThread` at offset **0**, not at `+4` as
> `cSoundCard_Linux` does.

Decompile findings on the Linux platform layer. Addresses in `libmvos.so`
(Ghidra base `0x10000`). All of this sits *behind* the HLE boundary for the
macOS port — documented here as the behavioral contract to replicate.

## Audio — `cSoundCard_Linux` (ctor @ `0xa2ba0`)

Classic OSS, opened on the device path passed in (`/dev/dsp` string @
`0xbd95f`):

1. `open(path, O_WRONLY)`; failure → warning, card disabled (`fd = -1`).
2. `ioctl(fd, 0xc004500a /* SNDCTL_DSP_SETFRAGMENT */, ...)`; failure →
   `perror("Unable to set fragment")`, close, disabled.
3. Capability negotiation, best-first: `SetStereo(true)` → else mono (mono
   *must* work or `Fatal`); `SetSoundFormat(4)` (16-bit) → fallback format 1
   (8-bit); `SetFrequency(22050)` → fallback `11025`.
4. **Two** buffers, both via `__builtin_vec_new`:
   - `+0x4c` — ring/back buffer, `rate/10` samples (100 ms) × channels × sample size (`Sample_Size[fmt]`, 1 or 2; anything else → `Fatal("Illegal format")`). Sample count is cached at `+0x50`, rate at `+0x40`.
   - `+0x34` — the software-**mix accumulation** buffer, `rate/10` × channels × **4** (32-bit headroom per sample). Any previous one is `__builtin_delete`d first. Alongside it: `+0x39` = `Sample_Size[fmt]`, `+0x3a` = channels, `+0x38` = 0.
5. Inherits `cSoundCard_SoftwareMix` (all mixing in software;
   `cSoundChannel_SoftwareMix` per voice) **and `cThread`** — ctor ends with
   **`cThread::Launch(this + 4)`**: the mixer runs on its own thread, writing
   mixed blocks to the fd.

> **ABI correction (audit 2026-07-26).** This doc previously said the ctor ends
> with `cThread::Launch(this)`. It is **`Launch(this + 4)`** — `cThread` is a
> *secondary* base at offset `+4`, which is why the ctor writes the `cThread`
> vtable to `cSoundCard+0x08` (= `+0x04` within the `cThread` subobject, matching
> `cThread`'s own ctor). The guarding `if (this == 0) p = 0` is the g++ 2.95
> pointer-adjust idiom for that base cast. Consequence for anyone reading thread
> state off a `cSoundCard_Linux`: pipe fds are at `+0x0c`/`+0x10`, running flag at
> `+0x14`, `pthread_t` at `+0x18` (each `+4` from the `cThread`-relative offsets
> below).

Game-side usage (from `cApplication::Start`): a `cSoundServer` with **16
`cSoundServerChannel`s** on top of the card. `Sample_Size[]` table indexes
format → bytes/sample.

**Port note — superseded by what was actually built.** The pure-HLE plan was to
replace this mixer with native code and expose a pull-callback. Under
[guest-libmvos](../porting/guest-libmvos.md) the engine's *own* mixer runs, as
guest code: `/dev/dsp` is HLE'd onto SDL audio, and because the host is
single-threaded (nested `uc_emu_start` crashes Unicorn) the mixer cannot have a
host thread. It is **green-run** instead — `cSoundCard_Linux::Main` is patched
to mix one-shot, and a slice is spliced into the running emulation via
`redirect_guest` whenever the audio queue drains below its target. The
consequence, and the reason that queue target is a tuning knob rather than an
implementation detail, is in [frame-timing.md](../porting/frame-timing.md) (Bug
3: the mixer was originally serviced only at present, which coupled audio to the
frame rate). The format/rate negotiation above still happens for real — it is
the guest's own code doing it.

## Threads — `cThread` (ctor @ `0xa5860`, `Launch` @ `0xa58c0`)

- Object: `{+0x00 ?, +0x04 cStream vtable base, +0x10 running flag, +0x14
  pthread_t}` — `cThread` **is-a `cStream`** (message-pipe endpoint).
- `Launch`: `cPipe::CreatePipe()` (comms channel, fds at `+0x08/+0x0c`) then
  `pthread_create(&this->tid, NULL, Entry, this)`. `Entry` invokes the virtual
  run method → **game subclasses run guest code** (emulator: green thread).
- `Kill`/`Wait` exist (imported by the game).

## Processes — `cTask` (`Launch` @ `0xa5740`)

- `cPipe::CreatePipe("MVOSMessagePort")` then **`fork()`**; child path does
  **`execlp`** (target from ctor args), parent stores child pid at `+0x10`.
- This is how the game would spawn the multiplayer server from the menu — and
  the readme.linux "GNU C library bug" that broke in-game server start almost
  certainly lived here (workaround: run `theoserver` manually).
- Distinct from `cTask_` (game-binary class, vtable `__vt_6cTask_` @
  `0x84d3240`) — unrelated name collision, game-side.

## Timer heartbeat

`setitimer`/SIGALRM drives `cTimerSystem_Linux`; consumers register as `cVTimer`
subclasses. `cIntuition` ctor (@ `0x9d370`) registers itself with the global
`TimerSystem` (vcall `+0x18/+8`), `Fatal("Unable to activate timer")` on failure
— input polling/repaint is timer-driven (`NoTimerInterruptPaintFlag` gates
repaint during mode switches, see
[application-bootstrap.md](application-bootstrap.md)).

## Decompiler hygiene note (Ghidra, both libmvos + plugins)

Some import thunks (`printf`, `__builtin_new`, `__builtin_vec_new`, `Fatal`) are
mis-flagged **noreturn** → decompiler truncates functions at the first call
("WARNING: Subroutine does not return"). `Fatal` genuinely is noreturn; the
others are not — fix their signatures in the Ghidra DB when working there, or
read such decompiles as "continues after the call". (Same class of artifact
already noted in [memory-and-containers.md](memory-and-containers.md).)
