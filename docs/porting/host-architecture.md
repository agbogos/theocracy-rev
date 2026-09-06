# Host architecture — the shape of `port/src`

This is a structural map of the emulator host: what each translation unit owns,
how the guest address space is laid out, and the exact call paths between guest
and host. For the full work log and the
symptom/diagnosis of each change, see [`guest-libmvos.md`](guest-libmvos.md).
Where the two disagree, this file describes the code as it stands and
`guest-libmvos.md` describes when a thing was introduced.

## Summary

Both shipped i386 ELFs are mapped into a single Unicorn `UC_ARCH_X86 /
UC_MODE_32` instance and run as original code: `theocracy.real` (ET_EXEC, mapped
at its own VAs, ~`0x08048000`+) and the real `libmvos.so.0.9` (ET_DYN, mapped at
`guestlink::MVOS_BASE` = `0x10000000`). The host is a loader, a dynamic linker, and a high-level emulation of the
*OS/library ABI only*: libc, pthread, libdl, BSD sockets, SMPEG, and the
`libmvos_vvc_x.so` device-plugin boundary.

Every `UND` symbol that one image
does not satisfy for the other is bound to a one-byte address inside a trap
window; jumping there fires a Unicorn code hook that runs a native C++ handler
and returns as if a cdecl callee had run. Two narrow exceptions to "guest code
runs unmodified" exist on purpose: five hot `cGD_LFB16` rasterizer leaves are
overridden by entry-point hooks (`blit.cpp`), and a handful of libmvos functions
are byte-patched in memory (`install_plugins_and_video`).

## File-by-file

| Unit | Owns | Key entry points |
|---|---|---|
| `main.cpp` | Boot orchestration. Chooses the executable, drives the fixed boot sequence, prints subsystem-flag and fault diagnostics. | `main`, `run_ctors`, `print_guest_backtrace` |
| `machine.hpp` / `machine.cpp` | The Unicorn instance, the guest memory map (`namespace guestmap`), the code-trap mechanism, host→guest calls, and all fault/profiling instrumentation. | `Machine::map/read/write/r32/w32/cstr`, `add_code_traps`, `install_traps`, `call`, `redirect_guest`, `return_double`, `request_stop`, `enable_profiling`/`enable_block_counter`/`enable_block_trace` |
| `elf32.hpp` | Header-only, dependency-free ELF32/i386 reader: PT_LOAD, `.dynsym`/`.dynstr`, every `SHT_REL`, `DT_INIT`/`DT_FINI`, section table. Bounds-checked. | `elf32::Image`, `Image::segments/dynsyms/relocs/sections/find_section/find_sym_idx` |
| `guestlink.hpp` / `guestlink.cpp` | The dual-image loader and linker: maps both images, computes the HLE symbol set, applies all i386 relocations in a specific order, handles `R_386_COPY` storage sharing, synthesizes `__ctype_b`, and reports zero GOT slots. Owns the `TrapLayer`. | `guestlink::link` → `LinkResult`, `guestlink::abs_sym` |
| `traps.hpp` / `traps.cpp` | Everything HLE: the import handler table, the guest heap allocator, filesystem/socket/stdio/SMPEG/dl handlers, the synthetic device plugins, SDL input translation, audio mixing, the green-thread and timer splices, and the whole diagnostic-harness family (watchdog, soak, FPS, render probe). 3661 lines; the largest unit by a wide margin. | `TrapLayer::dispatch`, `register_builtins`, `install_plugins_and_video`, `dispatch_plugin`, `guest_alloc`/`guest_release`, `register_handler`, `trap_addr`, `report` |
| `video.hpp` / `video.cpp` | Native SDL2 backend: one window, one RGB565 framebuffer, present + event pump, fullscreen toggle, crisp/smooth presentation policy, BMP capture. Replaces the whole `libmvos_vvc_x.so` X11/MIT-SHM plugin, whose only real job was presenting. | `Video::open`, `present`, `pump`, `toggle_fullscreen`, `set_crisp`, `save_bmp`, `fb()` |
| `mpeg.hpp` / `mpeg.cpp` | MPEG-1 decode for the SMPEG HLE, via libav (`avformat`/`avcodec`/`swscale`/`swresample`). Decodes a whole file to RGB565 frames plus interleaved S16 stereo @ 22050 Hz, keyed by the guest `SMPEG*` handle. Also owns the aspect-fit scaler and its cached sample tables. | `MpegStore::load/get/erase/clear`, `MpegMovie::fit_frame` |
| `cdaudio.hpp` / `cdaudio.cpp` | The virtual CD-ROM drive: the TOC and transport `cCD_Linux`'s seven ioctls talk to, plus a host-thread streaming decoder that sums the current Redbook track into the SDL mixer. Music, in other words — see [../subsystems/music-and-redbook.md](../subsystems/music-and-redbook.md). | `VirtualCD::scan/play/status/idle/mix`, `CDPlayer` |
| `config.hpp` / `config.cpp` | Reads `theoc.cfg` at startup and puts its `NAME = value` pairs into the environment, without overriding anything already set there. Refuses the four knobs that decide *what* to run rather than *how*. | `config::load` |
| `blit.hpp` / `blit.cpp` | Byte-exact native reimplementations of five `cGD_LFB16` blitters, installed as entry-point overrides over the real libmvos bodies. `THEOC_NATIVE_BLIT=0` disables for A/B against the profiler. | `install_native_blit` |
| `mvos.hpp` / `mvos.cpp` | **Superseded and not linked.** The pure-HLE MVOS method layer from before the guest-libmvos pivot: synthesized `__vt_*` vtables whose every slot is a trap, native `PaintTree`/`CalcAbsCoordTree`, host-side `cFile`/`cTextFile`. `port/CMakeLists.txt` lists it commented out (`# src/mvos.cpp`), so nothing in it is compiled. It is the only consumer of the `SINGLETON_BASE`, `NULL_FRAME`, and `VT_TRAP_BASE` regions. | (not built) |

`port/CMakeLists.txt` builds one executable, `theoc`, from `main.cpp machine.cpp
guestlink.cpp traps.cpp video.cpp mpeg.cpp cdaudio.cpp config.cpp blit.cpp`,
linking `unicorn SDL2 avformat avcodec avutil swscale swresample` from
`/opt/homebrew`. C++17, `-Wall -Wextra -Wno-unused-parameter`. There is no test
target and no library target.

## Guest memory map

All addresses below are in the **guest** (Unicorn) address space. Everything at
or above `0x50000000` is host-owned scaffolding; everything below it is real
guest code and data. That split is key — the block profiler and block
counter both filter on `addr >= 0x50000000` to decide "this is not guest work"
(`machine.cpp`, `count_hook` and `block_hook`).

| Region | Base | Size | Mapped by | Prot |
|---|---|---|---|---|
| game image (ET_EXEC) | its own VAs, ~`0x08048000`+ | per PT_LOAD | `guestlink::map_image` | per PT_LOAD; `PF_X` segments forced RWX |
| libmvos image (ET_DYN) | `0x10000000` (`MVOS_BASE`) | per PT_LOAD | `guestlink::map_image` | same; text RWX so text relocs and patches apply |
| `SCRATCH` | `0x50000000` | `0x00100000` | `main.cpp` | RW |
| — `ERRNO_ADDR` | `0x50000f00` | 4 | (inside `SCRATCH`) | — |
| — fake `argv` | `SCRATCH + 0x90000` / `+0x90100` | — | `main.cpp` | — |
| `SINGLETON_BASE` | `0x51000000` | `0x00100000` | `mvos.cpp` only — **unmapped in the shipped build** | RW |
| `NULL_FRAME` | `0x52000000` | `0x00001000` | `mvos.cpp` only — **unmapped in the shipped build** | RW |
| `GUEST_FB_BASE` | `0x53000000` | `0x00400000` (4 MB, ≥ 1600×1200×2) | `TrapLayer::install_plugins_and_video` | RW |
| `STUB_CODE` | `0x54000000` | `0x00010000` | `TrapLayer::stub_alloc`, lazily | RWX |
| `LIBC_DATA` | `0x55000000` | `0x00001000` | `guestlink::link` | RW |
| `HEAP_BASE` (bump arena) | `0x60000000` | `0x08000000` (128 MB) | `main.cpp` | RW |
| guest stack | `0x6f000000` … `0x70000000` (`STACK_TOP`, grows down) | `0x01000000` (16 MB) | `main.cpp` | RW |
| `PLUGIN_TRAP_BASE` | `0x76000000` | 1 page | `add_code_traps` from `install_plugins_and_video` | RX |
| `TRAP_BASE` | `0x77000000` | 1 byte per import, page-rounded | `add_code_traps` from `install_traps` | RX |
| `VT_TRAP_BASE` | `0x78000000` | 1 byte per synth vtable slot | `mvos.cpp` only — **unmapped in the shipped build** | RX |
| `STUB_BASE` (x87 return stub) | `0x79000000` | `0x1000` | `Machine::return_double`, lazily | RWX |
| — `FLOAT_SCRATCH` | `0x79000800` | 8 | (inside `STUB_BASE`) | — |
| `STOP_ADDR` | `0xDEAD0000` | — | never mapped | — |

`STOP_ADDR` is deliberately not memory: it is the `until` argument to
`uc_emu_start` and the sentinel return address pushed by `Machine::call`, so a
clean guest `ret` lands there and stops emulation.

The stack is 16 MB because the original 2 MB was overflowed by `Fatal`/abort
storms during early bring-up (`machine.hpp` comment). The arena is 128 MB and is
genuinely reclaimed — `free()` used to be a no-op, which leaked ~50 MB per
scenario load and let `malloc` return 0 on the second load (`traps.hpp`, and G15
in the milestone log).

## Call paths

### Guest calls an unresolved import

1. **Link time** (`guestlink::link`). Every `UND` symbol in either image that
   the *other* image does not define is collected into `need_hle`, sorted, and
   handed to `TrapLayer`'s constructor as `names_`. `Machine::install_traps(n,
   dispatch)` calls `add_code_traps(TRAP_BASE, n, fn)`, which maps
   `ceil(n/0x1000)` pages RX at `0x77000000`, records a `TrapRegion{lo, hi,
   fn}`, and installs a `UC_HOOK_CODE` over `[TRAP_BASE, TRAP_BASE + n - 1]`.
   `trap_of[name] = TRAP_BASE + slot`, and `resolve()` hands that address to the
   relocation applier, so `R_386_JMP_SLOT`, `R_386_GLOB_DAT`, `R_386_32` and
   `R_386_PC32` referencing the symbol all land on that byte. The window's
   contents are never executed — the hook rewrites EIP before the first
   instruction retires — it is mapped RX only so a jump there is a hook hit
   rather than a fetch fault.

2. **Run time** (`Machine::code_hook`). The hook fires with EIP inside the
   window. It reads `sp = ESP` and `retaddr = [sp]` — at this instant ESP points
   at the return address, exactly as it would on entry to a real callee — then
   calls `r.fn(*this, addr - r.lo, sp)`. Unless the handler took over EIP itself
   (see `trap_raw_`, below), the hook then emulates the `ret`: `ESP = sp + 4`,
   `EAX = <handler result>`, `EIP = retaddr`. Arguments are left on the stack
   because cdecl is caller-cleanup.

3. **Dispatch** (`TrapLayer::dispatch`). Bumps the per-slot hit counter and the
   watchdog breadcrumbs, looks `names_[slot]` up in `table_`, opens a
   `SlowSection` RAII timer (`THEOC_SLOWLOG`), and calls the handler with
   `(Machine&, esp)`. A missing entry logs `[trap] TODO <name>` on the first hit
   only and returns 0 — that log, plus the `UNIMPLEMENTED` block of
   `TrapLayer::report()`, is the worklist.

4. **Arguments and return.** Handlers read `arg(m, esp, i) == m.r32(esp + 4 +
   4*i)`. Under g++ 2.95 a `this` pointer is simply stack argument 0 —
   `Plugin_KeyMatrix` reads `self = arg(...,0)`, `code = arg(...,1)`; the
   `cDisplay` constructor handler reads `self` at index 0 and returns it,
   because a g++ 2.95 constructor returns `this` in EAX. Anything the handler
   returns becomes EAX. Pointer results **must be guest addresses**:
   `strchr`/`strrchr` compute `p + index` into the guest string, and `getenv`
   deliberately returns 0 rather than risk handing the guest a host pointer.

### Host calls into guest code

`Machine::call(addr, args, timeout_us)` is the only host→guest primitive. It
saves ESP, pushes `args` right-to-left, pushes `STOP_ADDR` as the return
address, sets ESP, and runs `uc_emu_start(uc_, addr, STOP_ADDR, timeout_us, 0)`.
On return it classifies the outcome:

- `last_returned()` — EIP is `STOP_ADDR` and no stop was requested: the guest
  function returned normally.
- `last_aborted()` — a handler called `request_stop()` (`abort` under
  `THEOC_LOUD_ABORT`, `exit`, `_exit`), which sets EIP to `STOP_ADDR`, sets
  `trap_raw_`, and calls `uc_emu_stop`.
- Neither — the wall-clock timeout expired and EIP is wherever the guest was.
- A Unicorn error that was not a requested stop captures EIP/ESP/EBP plus 16
  stack words and throws `std::runtime_error`, which `main.cpp` catches and
  renders as a labelled EBP-chain backtrace.

EAX is the return value, and ESP is restored to the caller's value in every
non-throwing path (cdecl caller-cleanup). `main.cpp` uses this for the entire
boot: libmvos `DT_INIT`, libmvos `.ctors`, the mvos.cfg loader at `MVOS_BASE +
0x94640` (libmvos file offset `0x94640`), game `.ctors`,
`Init__12cApplication(SCRATCH)`, `OpenSubsystems` at `MVOS_BASE + 0x94f20`, the
`cIntuition` constructor at `MVOS_BASE + 0x8d370`, and finally
`Start__12cApplication(SCRATCH, 1, argv)`. `.ctors` lists are walked in reverse
(`run_ctors`), skipping 0 and `0xffffffff`.

### The green run: `redirect_guest`

A nested `uc_emu_start` from inside a hook is not available. `machine.hpp`
states the constraint on `redirect_guest` ("no nested `uc_emu_start`");
`maybe_redirect_timer` records that nesting "crashes Unicorn"; the
`HLE_OpenDisplay` handler records that a nested `Machine::call` "is unreliable"
and builds `cGD_LFB16` field-by-field rather than invoking the guest
constructor. So the host can never *call* guest code from inside a trap. Instead
it **splices** guest code into the emulation that is already running:

```
uint32_t ret = m.r32(esp);      // the trap's own return address
uint32_t sp  = esp;
sp -= 4; m.w32(sp, arg);        // synthesize a cdecl frame ...
sp -= 4; m.w32(sp, ret);        // ... whose return address is the trap's
m.redirect_guest(entry, sp);    // EIP = entry, ESP = sp, trap_raw_ = true
```

`redirect_guest` sets EIP and ESP and raises `trap_raw_`, which makes
`code_hook` return without emulating the `ret`. The outer `uc_emu_start` simply
continues at the new EIP. The spliced function runs to its own `ret`, which pops
the address the trap would have returned to — so from the original caller's
point of view the import call returned normally, just after a detour. Two users:

- **Heartbeat.** `maybe_redirect_timer` stands in for `setitimer(ITIMER_REAL)` +
  `SIGALRM`; no real signal can be delivered into Unicorn. When the schedule is
  due it pushes `signo = 14` and redirects to `sigalrm_handler_` (captured from
  the guest's `sigaction`) or, failing that, `MVOS_BASE + 0x922e0`
  (`_TimerFunction__Fi`, libmvos file offset `0x922e0`). Backlog is collapsed to
  at most 16 skipped intervals so a slow frame does not storm. Collapsing rather
  than replaying is safe because of what this tick actually drives — the cursor
  click animation and cursor refresh, never the simulation, which clocks itself
  with its own bounded catch-up. See [frame-timing.md](frame-timing.md), "What
  the heartbeat actually drives".
- **Sound mixer.** `pthread_create` does not create a host thread; it records
  `{entry, arg}` in `soft_threads_` and calls `patch_sound_main_oneshot`.
  `maybe_redirect_sound` then picks a soft thread whose `cThread` running byte
  (`arg + 0x10`) is set and redirects into its entry with the `cThread*` as
  argument 0. This is buffer-driven, not clock-driven: it only fires when the
  host audio queue has drained below ~120 ms (`THEOC_AUDIO_MS`), with a 15 ms
  floor.

Both are serviced from two places — the `HLE_SwapBuffers` present handler and
the `usleep` handler — because servicing only at present couples the guest's 30
Hz heartbeat to the frame rate, which pins the province view at 12 fps and
starves the mixer. At most one redirect happens per trap invocation; at present,
sound wins over timer.

### Native override of a real libmvos function

`blit.cpp` uses the same `add_code_traps` mechanism, aimed at a *single byte
inside the already-mapped libmvos image* with `map_region = false`:

```
m.add_code_traps(mvos_base + OFF_PutBitmap8C1Mask, 1, blit_8c1_amask, false);
```

The hook fires at the function's first instruction, the handler reads its
arguments off the guest stack at `esp+4, esp+8, …`, and `code_hook`'s cdecl
`ret` returns to the caller — the real body never executes. Structurally it is
identical to an import trap; only the location of the one-slot window differs.
The five overridden functions, as **libmvos file offsets** (runtime address =
`MVOS_BASE + offset`; the corresponding Ghidra address is `file offset +
0x10000`):

| Offset | Function |
|---|---|
| `0x5c4e0` | `LFB16_PutBitmap` |
| `0x5c940` | `LFB16_VLineAlfa` |
| `0x5c9b0` | `LFB16_PutBitmap8` |
| `0x5cb70` | `LFB16_PutBitmap8_AMask` |
| `0x5cbb0` | `LFB16_PutBitmap8C1_AMask` |

Ghidra tags these `__regparm2`, but `blit.cpp` records that the disassembly
reads every argument from `[EBP+8]…` — the entry `PUSHAD`/exit `POPAD` make the
register slots dead — so they are treated as plain cdecl. Each handler mirrors
the touched guest span into a host buffer, mutates it natively, and writes it
back, so cost is O(1) guest accesses per call instead of O(pixels).
`install_native_blit` is called from `main.cpp` just before `Start`, and only
when not headless.

### Calling guest code from a handler: `Machine::call_guest_then`

`redirect_guest` gives a handler a tail call. The spliced function's return
address is the handler's own, so control never comes back and the handler can do
no work after the call. That is enough for the heartbeat and the sound slice as
they were, and not enough for anything that needs the result.

`call_guest_then` plants a one-byte trap of ours at `RESUME_BASE` as the return
address instead:

```
handler:  m.call_guest_then(fn, {a, b}, [state](Machine& m, uint32_t result) {
              return final_value;      // becomes the trapped call's EAX
          });
          return 0;                    // return immediately after
```

The handler writes a cdecl frame below the trapped function's own and redirects
into `fn`. `fn` runs as ordinary guest code; its `ret` pops `RESUME_BASE` into
EIP, which fires the resume trap. That hands the guest's EAX to the
continuation and then emulates the trapped function's `ret` itself — `ESP =
outer_esp + 4`, `EAX` from the continuation, `EIP` to the original return
address.

Each frame is built from the same `outer_esp`, so chaining calls does not stack
arguments. A continuation may call `call_guest_then` again; the last one's
return value is the trapped function's result. Continuations must own their
state — the handler's locals are gone by the time one runs.

This does not nest `uc_emu_start`. There is still exactly one, and the rule
below is unchanged: a handler hands control back to the emulation already
running rather than starting another.

`THEOC_RESUME_TEST` covers a single call, a chained pair, and a handler that
splices nothing.

### Patching guest code in memory

This is a **different mechanism** from trapping an import: the host overwrites
instruction bytes in the mapped libmvos text, which is possible because
`guestlink::seg_prot` maps any `PF_X` segment `UC_PROT_ALL`. All the patches
below are applied by `TrapLayer::install_plugins_and_video`, except the sound
one, and are given as **libmvos file offsets** (runtime = `MVOS_BASE + offset`).

| Offset | Target | Patch |
|---|---|---|
| `0x85ce0` | `OpenDisplay` | 7 bytes `mov eax, <PLUGIN_TRAP_BASE+slot>; jmp eax` → `HLE_OpenDisplay` |
| `0x85e20` | `SwapBuffers__4cVVC` | same shape → `HLE_SwapBuffers` (present only; `SwapBuffers__Fv` is left intact — see [The present chain](#the-present-chain) for exactly what that buys) |
| `0x8b69c` + `0x8b6ec` | `cSprite::AfterSwapBuffer` | 5-byte `JMP` to `0x8b6e7` skipping the two-slot background swap, plus 9 `NOP`s over the three stores that follow. Makes the single-buffer LFB correct (save → paint → present → restore the same rect). `THEOC_LEGACY_SPRITE=1` reverts. |
| `0x8df10` | `PushMouseInput__Fv` | single `0xC3` (`ret`) — SDL already feeds the Intuition ring, and leaving this live double-fed it |
| `0x92b3c` + `0x92b8e` | `Main__16cSoundCard_Linux` | `EB 02` to enter the loop body once, and `90 90` over the backward branch — turns `while (running)` into `do { } while (0)` so a green-thread slice mixes exactly one fragment. Applied by `patch_sound_main_oneshot` on the first `pthread_create`. |

One patch targets the **game** rather than libmvos, and so is given as an
absolute address and installed separately (`install_province_rate`, from
`main.cpp`):

| Address | Target | Patch |
|---|---|---|
| `0x81da52a` | `cProvince_Do`'s frame-limiter operand | rewrite the `imm32` of `push 0x14585` (83,333 µs = 12fps) — `THEOC_PROVINCE_MS`, off unless set. The operand is read back and verified before writing. Because province steps its sim once per frame, this scales **game speed** with frame rate; see [frame-timing.md](frame-timing.md). |

The synthetic plugin exports themselves live in a second trap window at
`PLUGIN_TRAP_BASE` (`0x76000000`), registered by `install_plugins_and_video` and
dispatched by `dispatch_plugin`. `dlopen` on any `libmvos_*`/`vvc` path returns
a synthetic handle; `dlsym` maps an export name to `PLUGIN_TRAP_BASE + slot`.
The current export list is `QueryDevice`, `CreateVideoDevice`,
`CreateKeyboardDevice`, `CreateMouseDevice`, `CreatePointerDevice`,
`Plugin_NoopOK`, `Plugin_Return0`, `Plugin_KeyMatrix`, `Plugin_SetVideoMode`,
`HLE_OpenDisplay`, `HLE_SwapBuffers`.

### The present chain

We replace one function in the present path, and it is the innermost of
three. Every link below has exactly one call site, so this is the whole of it
(libmvos, read off the disassembly):

```
cScreen::EndRefresh                       (0x9d2d0 — PaintTree, then:)
  └─ SwapBuffers__Fv                      (0x8e820 file / 0x9e820 Ghidra)
       ├─ rolling-demo record/compare     only if Intuition_Mode != 0 — never for us
       ├─ Frame_Counter++
       ├─ PushKeyInput                    (0x8e690 — the G16 spin site)
       ├─ MouseRefresh                    (0x8e7c0)
       │    ├─ PushMouseInput             we patch this to a bare `ret`
       │    └─ cSprite::MoveTo            if Intuition_Mode == 0, i.e. always here
       ├─ push a type-0x20 event into the Intuition+0x28 ring
       ├─ VBlankInProgress = 1
       ├─ cSprite::BeforeSwapBuffer       (0x8b650 — SaveBg + paint the pointer)
       ├─ cVVC::SwapBuffers               (0x85e20) ← the only part we replace
       ├─ cSprite::AfterSwapBuffer        (0x8b690 — patched single-buffer, G17)
       └─ VBlankInProgress = 0
```

So cursor tracking, key input and the frame counter all stay **guest-side**, and
the real `cSprite` is already composited onto the LFB before our handler runs.
The two `cSprite::MoveTo` call sites are mutually exclusive on `Intuition_Mode`
— the rolling-demo one reads `Intuition+0xa0`, `MouseRefresh`'s reads the live
`Intuition+0x14/0x18` — which is why nopping `PushMouseInput` does **not** cost
us pointer tracking.

For a long time the guest's `SwapBuffers`/`BeforeSwapBuffer` path was "abandoned as fragile",
however this proves that the opposite is
true: it is load-bearing, and G17's cursor-trail fix is a patch *inside* it.
What is HLE'd is the VVC-level present alone, which is architecture, not debt.

**The cVVC slot layout**, since `HLE_OpenDisplay` writes it and the old comment
had it wrong:

| Slot | Role |
|---|---|
| `+0x00` / `+0x04` | the two real buffers |
| `+0x08` | **not a GD** — an optional memblock-backed overlay bitmap (`+0x0c` cMemBlock, `+0x14` refcount, `+0x20`/`+0x24` w/h). `cVVC::SwapBuffers` takes a wholly different path when it is non-null |
| `+0x10` | the "current" GD — `cSprite::Refresh` reads it |
| `+0x14` | the paint GD — `cScreen::EndRefresh` and `cSprite::BeforeSwapBuffer` read it |
| `+0x18` / `+0x19` | buffer parity bytes, toggled at the end of `cVVC::SwapBuffers` |

`cVVC::SetBuffers` (`0x85c90`) is what copies `+0x00`/`+0x04` into
`+0x10`/`+0x14` according to `+0x18`. It has exactly two callers —
`cVVC::OpenDisplay` and `cVVC::SwapBuffers` — and the host replaces both, so
it never executes. Nothing in the guest can therefore move a GD slot under us.
Two consequences: `HLE_OpenDisplay` is the **sole** writer of those slots, and
our writing `gd` into `+0x08` is meaningless but harmless, since its only two
readers are those same two replaced functions.

Two bring-up-era paths were deleted from `HLE_SwapBuffers` on that basis — a per-frame re-stamp of the five GD slots, and the G5 magenta
crosshair (`draw_software_cursor`) drawn whenever the active screen had no
pointer sprite. Both were defensive code whose trigger had been designed out
long before, and neither had ever been shown to fire.

The static argument above is what identified them; it is not what closed them.
Each was first turned into a counter in the trap report and tested at runtime.
A session covering every transition that could plausibly produce a spriteless
frame or a moved slot — movies, save/load, quit, new game — recorded 0 and 0
across 466 presents, and they were removed. Both paths live at screen
transitions, so the session was scoped to cover every transition rather than
to run long.

> **Method note.** Both `cVVC::SwapBuffers` and `cVVC::OpenDisplay` have
> **truncated reported bodies** in the Ghidra DB, so `get_xrefs_to` attributed
> their tails to phantom `FUN_00095da0` / `FUN_00096040` and the `SetBuffers`
> call sites appeared to be outside both. The decompiler follows the
> fall-through and was right; the xref list was not. This is the split-function
> artifact in [re-methodology](../reference/re-methodology.md) — resolved, as it
> says to, by reading the instruction stream.

### Asynchronous cursor refresh: implementing `cGD_LFB16::Refresh`

The engine repaints the pointer between frames, and that is how a 12fps game
felt responsive in 2000. `cIntuition::TimerProc` runs on the 30Hz `setitimer`
heartbeat and, gated on `GD->IsAsyncRefreshCapable()`, calls `MouseRefresh` +
`cSprite::Refresh`. `cSprite::Refresh` erases the old pointer (`RestoreBg`),
paints the new one, and flushes each touched rectangle through the GD's vtable
slot `+0x14` — `cGD_LFB16::Refresh(const cRectangle&)`.

Both halves of that contract are trivially satisfied on the original, and both
are traps for us:

| | Original | Us |
|---|---|---|
| `IsAsyncRefreshCapable()` | returns 1 — an LFB *is* the display | returns 1, but an LFB is no longer the display |
| `Refresh(rect)` | `{ return; }` — writing the LFB already displayed it | `{ return; }` — the pixels sit in a staging buffer, so the no-op drops the frame |

`cGD_LFB16` is the **linear framebuffer** GD: on real hardware, writing to it is
writing to the screen, so an empty flush is correct. (The X backend substitutes
`cGD_X`, whose `Refresh` pushes the rect over MIT-SHM — same contract, different
implementation.) Our LFB is neither: it is a staging buffer copied to SDL only
at present. So we inherited an empty `Refresh` that silently discarded every
between-frame pointer update, pinning the cursor to the scene's frame rate —
**12fps in province**, which is exactly the responsiveness the separate 30Hz
timer existed to buy.

The fix implements the method rather than patching anything: an entry-point
override (`install_gd_refresh`, the same seam `blit.cpp` uses) marks the frame
dirty, and `present_async_cursor` does the LFB→SDL copy at the next safe point.
Supporting this:

- It does not present from inside the override. `cSprite::Refresh` calls
  `Refresh` *twice* — once after the erase, once after the repaint — so
  presenting on the first would show the erased pointer as its own frame.
- The present happens on the `usleep` resume path, right after the spliced
  tick returns. The frame limiter sleeps at the *top* of `cProvince_Do`, so the
  LFB holds the **last completed frame** at that moment: no half-drawn scene, and
  `TimerProc`'s erase/repaint pair has finished.
- It is skipped when the scene already outruns the heartbeat (>25 ms since
  the last present). The realm screen presents faster than 30Hz on its own, where
  an extra present would buy nothing.

`THEOC_LEGACY_CURSOR=1` reverts to the inherited no-op. Note the side effect on
instruments: province now presents ~30×/s while its simulation still steps
12×/s, so `THEOC_FPS`'s fps figure is no longer a proxy for sim rate — see
[diagnostics.md](diagnostics.md).

### x87 float returns: `return_double`

The trap contract is "handler returns a `uint32_t`, `code_hook` puts it in EAX".
A function returning `double` on i386 System V returns it in `st0`, and a native
handler cannot push onto the guest's x87 stack by writing a register — the FPU
stack top is architectural state that only an x87 instruction maintains
correctly. `Machine::return_double(v)` solves it by making the guest do the
push:

1. Lazily map `STUB_BASE` (`0x79000000`) RWX and write seven bytes: `DD 05
   <disp32 = FLOAT_SCRATCH>` (`FLD qword ptr [0x79000800]`) followed by `C3`
   (`RET`).
2. Write `v` to `FLOAT_SCRATCH`.
3. Set EIP to `STUB_BASE` and raise `trap_raw_`.

The stack is untouched, so the trap's return address is still on top: the stub's
`RET` returns straight to the guest caller with `st0` loaded by a genuine `FLD`.
A handler that calls `return_double` should still `return 0`; the value is
ignored. The only current caller is `__strtod_internal`.

## Adding a new HLE function

**Case 1 — an unresolved import** (the common case; it appears as `[trap] TODO
<name>` on stderr and in the `UNIMPLEMENTED` list of the end-of-run trap
report). Add an entry to `table_` inside `TrapLayer::register_builtins`
(`traps.cpp` — search for the definition, not a line number; it moves), in
whichever of the labelled sections it belongs to. The sections in registration
order are: memory/`str*`/`printf` family and `sscanf`; `abort`/`exit`; misc libc
(`__errno_location`, `getenv`, `__strtol_internal`, `__strtod_internal`,
`gettimeofday`, `usleep`, `ioctl`, `strcat`); real BSD sockets;
timers/signals/`pthread_create`; SMPEG; `cDisplay`; libdl; `FILE*` stdio; POSIX
`open`/`read`/`write`/`close`/`__xstat`/`opendir` family; and a trailing loop of
harmless no-op stubs. The shape is:

```
t["name"] = [this](Machine& m, uint32_t esp) -> uint32_t {
    uint32_t a0 = arg(m, esp, 0);            // = m.r32(esp + 4)
    std::string s = m.cstr(arg(m, esp, 1));  // guest string
    ...
    return result;                            // -> EAX
};
```

Read and write guest memory only through `m.read/write/r32/w32/cstr`. Return
guest addresses, never host pointers. For a `double` return call
`m.return_double(v)` and `return 0`. To end the current `Machine::call`
(abort/exit semantics) call `m.request_stop()`. To splice guest code in, build
the frame and call `m.redirect_guest(...)` — never `m.call(...)`. If the handler
can block for long, wrap the slow part in a `SlowSection` so `THEOC_SLOWLOG` can
attribute it.

**Case 2 — the symbol already has a handler you want to replace** (or you are
writing a layer outside `traps.cpp`): `TrapLayer::register_handler(name, fn)` is
the public injection point, and `TrapLayer::trap_addr(name)` gives the guest
address of an import's trap so the host can invoke a handler directly.

**Case 3 — a real libmvos function you want to run natively.** Follow
`blit.cpp`: `m.add_code_traps(mvos_base + <file offset>, 1, handler,
/*map_region=*/false)`, and call your installer from `main.cpp`. Confirm the
calling convention from the disassembly rather than trusting Ghidra's
`__regparmN` inference.

**Case 4 — a new device-plugin export the guest will `dlsym`.** Append the name
to `plugin_exports_` in `install_plugins_and_video` and add a branch to
`dispatch_plugin`.

## Invariants

**Host objects planted in guest space must come from `TrapLayer::guest_alloc`
(i.e. `bump_alloc`) or from a dedicated region outside the arena.** A hardcoded
address inside `[HEAP_BASE, HEAP_BASE+HEAP_SIZE)` is a delayed corruption bug,
not an immediate one: the arena hands that address out as ordinary memory once
cumulative allocation reaches it, and the guest then paints over the live
object. This actually happened — `cIntuition` was carved at a fixed `HEAP_BASE +
0xf00000`, and past ~15 MB of allocation the guest got it back and wrote a
bitmap over it; `Intuition+0x24` (the active `cScreen*`) became RGB565 pixel
pairs, which is non-null, so `ActivateScreen`'s null guard passed and it faulted
(`main.cpp`, the `cIntuition` construction block — the comment there records the
old carve). The dedicated regions are `GUEST_FB_BASE` (framebuffer), `STUB_CODE`
(guest-callable stubs), `LIBC_DATA` (synthetic glibc data objects), and
`SCRATCH`. `SINGLETON_BASE` and `NULL_FRAME` are reserved for the same purpose
but are currently referenced only from the unlinked `mvos.cpp`, so nothing maps
them in the shipped build — a new user would have to map them first.

**Never nest `uc_emu_start`.** `call_guest_then` above does not weaken this: it
returns control to the emulation already running. What stays forbidden is
`Machine::call` from inside a trap handler, a
code hook, or a block hook. Use `redirect_guest`, or build the guest structure
by hand instead of calling its constructor (which is what `HLE_OpenDisplay` does
for `cGD_LFB16`).

**A handler that takes over EIP must not also have its return emulated.**
`request_stop`, `return_double`, and `redirect_guest` all set `trap_raw_`, which
`code_hook` consumes exactly once. Do not perform two of these in one trap
invocation, and return 0 from a handler that has done one.

**Guest-callable code must live in an executable mapping.** The bump arena is RW
only, so a stub the guest will `call` cannot be `bump_alloc`ed — use
`TrapLayer::stub_alloc`, which carves from the RWX `STUB_CODE` page. Data-only
objects (vtables of stub pointers, event rings, mailboxes) can and do live in
the arena.

**Keep host regions at or above `0x50000000`.** The block profiler and block
counter treat `addr >= 0x50000000` as "not guest code". A host region placed
below that line would be silently counted as guest work and would corrupt
`THEOC_FPS`/`THEOC_PROFILE` numbers.

**Never hardcode a trap slot index.** `names_` is the sorted set of symbols the
two specific ELFs failed to resolve for each other; the ordering is a property
of those files, not a stable ABI. Use `trap_addr(name)`, and for plugin slots
search `plugin_exports_` by name (as `patch_jmp`, `dlsym`, and `make_device` all
do).

**Do not reintroduce manual mvos↔game mirroring for `R_386_COPY` globals.**
`guestlink` gives libmvos's own absolute references to those symbols the
*game's* `.bss` address (`copy_to_game`), so the storage is genuinely shared and
both images see one set of bytes. The old post-call sync loops in `main.cpp` for
`EnvSystem`, the nine `_12cApplication.*` flags, and the device singletons are
gone on purpose. The one deliberate exception is `__vt_*`: those keep pointing
at libmvos's own relocated vtable body, because virtual dispatch must reach
libmvos code.

**libmvos text is mapped RWX deliberately.** `seg_prot` forces `UC_PROT_ALL` on
`PF_X` segments. That is required both for the thousands of text relocations
libmvos carries and for the in-memory patches above. Do not "tighten" it to RX.

**Pointer-returning libc handlers return guest addresses.** A host pointer
written into guest memory is a fault waiting for the guest to dereference it.
`strchr` returning 0 instead of a guest pointer is what produced an `eip=0`
fetch fault on the netgame path.

**`free` must really free.** `bump_alloc`/`guest_free` maintain `alloc_sz_`, a
coalescing `free_addr_` map, and a best-fit `free_size_` multimap.
`THEOC_HEAP_TEST=1` runs a randomized alloc/free/realloc self-test asserting no
overlapping blocks and real reclamation; it leaves the arena fragmented and
exits without booting.

**Emulation is single-threaded.** Only two other host threads exist: the stall
watchdog (relaxed atomics only — `present_seq_`, `trap_seq_`, `last_trap_`,
`Machine::exec_blocks`/`last_block`) and the SDL audio callback (guarded by
`audio_mu_` around `audio_q_`). Guest `pthread_create` does not create a host
thread. Anything a handler touches without a lock must be touched only from the
emulation thread.

## Gaps and things I could not confirm from source

- **The game's load base is not read from a variable, but it is confirmed.**
  `theocracy.real`'s program headers give `PT_LOAD 0` at va `0x08048000`
  (`R-X`, filesz = memsz `0x47e565`) and `PT_LOAD 1` at `0x084c7580`
  (`RW-`, filesz `0xd0ae4`, memsz `0x1ba978`), re-derivable with `readelf -l`
  or [`tools/elfq.py`](../../tools/elfq.py). The *host* never names a game base,
  though:
  `main.cpp`'s backtrace labeller and the profiler simply call anything outside
  `[MVOS_BASE, MVOS_BASE + 0x200000)` "game". That is fine for labelling and
  wrong as a general test, since it would also label a host region "game" —
  except that host regions all sit at or above `0x50000000` and are filtered
  earlier.
### Accepted behaviours and won't-fixes

Deliberate non-bugs. Recorded so they are not rediscovered as defects and
"fixed" at cost.

- **A cutscene is bracketed by ~1.35 s of decode.** `SMPEG_new` decodes the whole
  movie up front (~0.9 s for the intro) and `SMPEG_delete` frees it (~0.4 s).
  Lazy or threaded decode is not worth it: this happens once per cutscene, on a
  screen a keypress already skips.
- **`[` and `]` are not mapped.** Keyboard coverage is letters, digits, arrows,
  modifiers, F-keys, enter, space and backspace. `[` and `]` are absent from the
  **original** libmvos eKey table, and nothing in the game depends on them — so
  there is nothing to map them *to*.
- **Eight Alt+letter combinations do nothing** — C, E, G, K, O, R, X, Z fall to
  the default case of the game's own dispatcher and return. Not our bug; see
  [../subsystems/dev-console.md](../subsystems/dev-console.md).
- **Audio can blip during the ~1 s province-load spike.** The emulator is
  genuinely compute-bound there and rarely yields. Steady state is clean (0
  underruns/s); see [frame-timing.md](frame-timing.md).
- **Guest heap grows ~18 KB per load/unload cycle** — measured very linearly over
  a 20-cycle soak, i.e. you'd need ~7000 cycles to exhaust the 128 MB arena. Left unchased;
  attributing it needs an allocation-site histogram that does not exist; see [heap-growth-trials.md](heap-growth-trials.md).

### Why teardown skips `CloseSubsystems`

libmvos's `main` ends by calling `CloseSubsystems` (file `0x950e0`), which shuts
down `TimerSystem`, `VVC`, `VKeyboard`, `VMouse`, `SystemPointer`, `VCD` and
`SoundCard` in order. The host never calls it, and that is a **decision, not an
oversight** — the constant that used to sit unused in `main.cpp` has been
deleted so it stops implying otherwise.

The reasoning, in short, is that the game owns *nothing* since we emulate the OS boundary,
and the application terminating frees up everything we'd otherwise manually free.
In a few more words:

- Nothing outlives the process. Everything `CloseSubsystems` releases is
  either host-owned or inside the Unicorn mapping. SDL audio and video are opened
  and closed by us (`Video`, the audio device in `traps.cpp`); the guest heap is
  one big mapping the OS reclaims. There is no guest-side resource that survives
  exit for the guest to leak.
- It closes devices, not files. Nothing in that sequence flushes game state,
  so skipping it cannot lose a save or corrupt a file.
- Calling it would add a failure surface, not remove one. Those seven close
  paths run through HLE device stubs that no other code path exercises, at the
  one moment a fault is most annoying and least diagnosable — a crash *after* the
  session is otherwise over. Zero upside, non-zero downside.

Windows was one candidate
for this being wrong — a platform where an audio or video device left open by
the guest's own bookkeeping might matter. Once shipped and tested, nothing
surfaced: no device left claimed after exit, no handle warning, no second-launch
failure on any of macOS, Linux or Windows.

What would reopen this, narrowed by that evidence to two things, neither of
which is a platform:

- **The host stops being one-shot** — an in-process restart, a "quit to menu that
  re-runs `Init`", or a test harness that boots the guest twice. Everything the
  argument rests on is "the process is about to exit", and none of it survives
  that.
- **A guest-side resource that outlives the process** appears — the current list
  is empty by inspection, but a future HLE addition (a named object, a lock file,
  a device claimed outside our own SDL handles) would put something on it.

If either happens the entry point is `MVOS_BASE + 0x950e0`, called with no
arguments, after `Start` returns and before the video hold.

### Game-space addresses in the host

**Resolved by name.** Four singleton pointers (`VVC`
`0x08598cec`, `Intuition` `0x08598454`, `VMouse` `0x08598c3c`, `VKeyboard`
`0x08598b58`) used to be hardcoded in `traps.cpp`. All four are `R_386_COPY`
globals listed in `data/theocracy_copyrelocs.tsv`, so the executable's dynamic
symbol table names them. `main.cpp` now resolves all four through
`guestlink::abs_sym` from whichever image was booted and hands them to
`TrapLayer::set_game_globals`; the trap layer looks them up via `game_glob()`
and falls back to libmvos's own slot as before. Boot prints `[link] game
singleton globals resolved by name: 4/4`. This is the same fix that let the
`server` executable boot at all (G20).

**Two that cannot be, and why that is accepted.** `THEOC_CONSOLE` and
`THEOC_EDIT` need two game-internal globals that are **not** copy-relocs and
**not** dynamic symbols — `theocracy.real` is `.symtab`-stripped and none of its
348 exports covers them:

| Address | What | Used by |
|---|---|---|
| `0x085c0fe0` | `g_LogConsole` (`cVOConsole` in game `.bss`) | `maybe_redirect_console` |
| `0x084c9610` | `g_GameSession` (`cGameSession*`) | `apply_edit_mode` |

There is no name to resolve, so `abs_sym` cannot help and no amount of work
makes these safe against a differently built executable. Both are accepted
deliberately:

- `THEOC_CONSOLE` and `THEOC_EDIT` reach into the game to expose its own debug
  surface, which is a step beyond running the shipped binary faithfully — a
  deliberate scope expansion for developer features, not part of the port itself.
- Both are opt-in and default-off. With the env vars unset, neither address is
  ever read, so a mismatched executable behaves exactly as before.
- Both are guarded at use: the console open is refused unless a `cShell` is
  attached, and the edit stamp is skipped while `g_GameSession` is null. A wrong
  address yields a no-op, not a corrupted write.

If the game surface ever needs to be addressed properly, the answer is a
signature scan or a per-build address table — not another bare constant. Full
context: [../subsystems/dev-console.md](../subsystems/dev-console.md).

Notes:
- Teardown deliberately skips `CloseSubsystems`, see
  below.
- `Machine::install_traps` vs `add_code_traps`. The header calls
  `install_traps` a "back-compat shim". Both are live; `install_traps` is used
  only for the import window.
- `Video::keep_open_for` presents outside the frame counter, which is why
  `stop_watchdog()` must be called before it. That ordering is a real coupling,
  not a stylistic one.
- Trap-window sizing. `add_code_traps` maps `(nslots + 0xfff) & ~0xfff`
  bytes, minimum one page. With ~119 HLE symbols today that is one page. The function now checks the rounded window against the nearest
  higher trap base (`PLUGIN_TRAP_BASE`, `TRAP_BASE`, `VT_TRAP_BASE` sit
  `0x01000000` apart) and against address overflow, and throws naming both
  extents rather than mapping over its neighbour. Unreachable at today's slot
  counts — the point is that the failure would otherwise be baffling: the
  overlap maps silently and it is the *neighbour's* traps that stop dispatching,
  far from the import set that grew.
