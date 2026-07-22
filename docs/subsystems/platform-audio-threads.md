# Platform backends: audio, threads, processes (libmvos)

Decompile findings on the Linux platform layer. Addresses in `libmvos.so` (Ghidra base `0x10000`). All of this sits *behind* the HLE boundary for the macOS port — documented here as the behavioral contract to replicate.

## Audio — `cSoundCard_Linux` (ctor @ `0xa2ba0`)

Classic OSS, opened on the device path passed in (`/dev/dsp` string @ `0xbd95f`):

1. `open(path, O_WRONLY)`; failure → warning, card disabled (`fd = -1`).
2. `ioctl(fd, 0xc004500a /* SNDCTL_DSP_SETFRAGMENT */, ...)`; failure → `perror("Unable to set fragment")`, close, disabled.
3. Capability negotiation, best-first: `SetStereo(true)` → else mono (mono *must* work or `Fatal`); `SetSoundFormat(4)` (16-bit) → fallback format 1 (8-bit); `SetFrequency(22050)` → fallback `11025`.
4. Ring/back buffer sized `rate/10` samples (100 ms) × channels × sample size; allocated via `__builtin_vec_new`.
5. Inherits `cSoundCard_SoftwareMix` (all mixing in software; `cSoundChannel_SoftwareMix` per voice) **and `cThread`** — ctor ends with `cThread::Launch(this)`: the mixer runs on its own thread, writing mixed blocks to the fd.

Game-side usage (from `cApplication::Start`): a `cSoundServer` with **16 `cSoundServerChannel`s** on top of the card. `Sample_Size[]` table indexes format → bytes/sample.

**Port note:** mixer thread is MVOS-internal → fully native in the HLE build; expose a pull-callback (SDL audio) instead of the push loop. Honor the format/rate fallback order only if game code queries the resulting format (check `GetSoundFormat` users).

## Threads — `cThread` (ctor @ `0xa5860`, `Launch` @ `0xa58c0`)

- Object: `{+0x00 ?, +0x04 cStream vtable base, +0x10 running flag, +0x14 pthread_t}` — `cThread` **is-a `cStream`** (message-pipe endpoint).
- `Launch`: `cPipe::CreatePipe()` (comms channel, fds at `+0x08/+0x0c`) then `pthread_create(&this->tid, NULL, Entry, this)`. `Entry` invokes the virtual run method → **game subclasses run guest code** (emulator: green thread).
- `Kill`/`Wait` exist (imported by the game).

## Processes — `cTask` (`Launch` @ `0xa5740`)

- `cPipe::CreatePipe("MVOSMessagePort")` then **`fork()`**; child path does **`execlp`** (target from ctor args), parent stores child pid at `+0x10`.
- This is how the game would spawn the multiplayer server from the menu — and the readme.linux "GNU C library bug" that broke in-game server start almost certainly lived here (workaround: run `theoserver` manually).
- Distinct from `cTask_` (game-binary class, vtable `__vt_6cTask_` @ `0x84d3240`) — unrelated name collision, game-side.

## Timer heartbeat

`setitimer`/SIGALRM drives `cTimerSystem_Linux`; consumers register as `cVTimer` subclasses. `cIntuition` ctor (@ `0x9d370`) registers itself with the global `TimerSystem` (vcall `+0x18/+8`), `Fatal("Unable to activate timer")` on failure — input polling/repaint is timer-driven (`NoTimerInterruptPaintFlag` gates repaint during mode switches, see [application-bootstrap.md](application-bootstrap.md)).

## Decompiler hygiene note (Ghidra, both libmvos + plugins)

Some import thunks (`printf`, `__builtin_new`, `__builtin_vec_new`, `Fatal`) are mis-flagged **noreturn** → decompiler truncates functions at the first call ("WARNING: Subroutine does not return"). `Fatal` genuinely is noreturn; the others are not — fix their signatures in the Ghidra DB when working there, or read such decompiles as "continues after the call". (Same class of artifact already noted in [memory-and-containers.md](memory-and-containers.md).)
