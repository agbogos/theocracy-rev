# Running the original on its intended OS

There are two ways to play Theocracy on a modern machine. One is this repo's
port. The other is to run the shipped Linux binaries unmodified on a period
Linux inside a VM, which is what was done first, before any of the port or the
disassembly existed.

This is the record of that setup. It is the only configuration in which the game
runs as *entirely* original code, so it is the reference the port is judged
against, and every workaround in it turned out to have a cause the disassembly
later named. Each section says which.

**Provenance.** These are session notes from a UTM setup on macOS, taken while
getting the game running and not re-verified since. Where a claim has since been
confirmed against the binaries, it is cross-referenced. Where it has not, it is
labelled *observed*. The notes predate the port and were written up here
afterwards.

## 1. Which past to choose

Two guest systems were tried. **Debian Woody (3.0r6, i386)** is the one to use.

| | Woody | Windows XP SP3 (32-bit) |
|---|---|---|
| Stability | no crashes, no speed problems, no windowing bugs once configured | changing the emulated CPU (e.g. to Pentium III) blue-screened on the spot; occasional failure to resume or mid-boot corruption |
| Audio | OSS, one module, then smooth — see §4 | stuttered, sometimes refused to initialise |
| Timing | correct | game timers ran fast, cutscenes stuttered, under both VirtualBox and UTM |
| Responsiveness | snappy despite the age of the OS | sluggish in the VM even with more RAM |
| Control | colour depth, resolution and daemons are all manually settable | none of the three is directly settable |

The reason has little to do with Windows against Linux. The Linux release runs
natively from its own ELF binaries with `LD_LIBRARY_PATH` pointed at the shipped
`libmvos.so` — no installer, no registry, no DLL closure — and a 2002
distribution still lets you set the two things the engine is fussy about, the X
visual depth and the OSS device, by editing a file.

Use XP only if you need a Windows-only tool in the same VM. Note also that the
Windows release is protected where the Linux one is not,
which is why the port runs the Linux binaries on all three hosts
([../porting/other-os-ports.md](../porting/other-os-ports.md)).

### VM settings that worked

| Element | Woody VM | XP VM |
|---|---|---|
| Emulator | UTM (QEMU frontend) | UTM / VirtualBox |
| CPU | `i386`, Q35 or i440FX | varies (Q35, i440FX, P3) |
| RAM | 2 GiB | 2–4 GiB |
| Audio device | AC97 | AC97 / SB16 |
| Video | `virtio-vga` or Cirrus | VBoxSVGA / VMwareSVGA |
| Disk | 8 GiB | 8–16 GiB |
| Kernel | Linux 2.4.x | Windows NT 5.1 SP3 |

## 2. Colour depth

**Symptom.** The game exits with

```
Fatal: Unable to activate screen
```

even though X is running and the resolution looks right.

**Cause, since confirmed — and the message is the game's own.** `theocracy.real`
calls `Fatal("Unable to activate screen")` (libmvos's `Fatal` supplies the
`Fatal:` prefix) at the end of a two-step fallback that appears wherever the
game switches screens. Read at `0x0819ea00`, the save/load slot dialog, in
Ghidra space:

```c
screen.depthCode = 5;                                   // 16-bit
if (!ActivateScreen(Intuition, &screen)) {
    screen.depthCode = 4;                               // 15-bit
    if (!ActivateScreen(Intuition, &screen))
        Fatal("Unable to activate screen");
}
```

There is no 24-bit branch to fall through to. The same block sits in the
`SaveGame` error
path at `0x081a0a10`, and the string appears many times over in the image, so it
is the general shape rather than one site's quirk.

Underneath, the engine's `SetVideoMode` requires the X server's *actual visual
depth* to match the requested mode, so on a server running at depth 24 both
`ActivateScreen` calls fail and `OpenDisplay` returns 0. The full path is in
[../porting/vvc_x-backend.md](../porting/vvc_x-backend.md), "Color-depth
gotcha". Modern distributions pick 24-bit or better automatically and give you
no obvious way to go back, which is most of why the game appears not to run on
them at all; Woody's X defaulted to 16 bpp.

**Fix.** In `/etc/X11/XF86Config-4`, in the active `Screen` section:

```
Section "Screen"
    Identifier   "Default Screen"
    ...
    DefaultDepth 16

    SubSection "Display"
        Depth 16
        Modes "800x600" "640x480"
    EndSubSection
EndSection
```

The depth needs at least one resolution listed under it or X will not use it.
XFree86 requires every mode to be declared up front and there is no `xrandr` to
fall back on, so declare a conservative one (800×600 or 640×480) if unsure, and
match it
to the game's own resolution to avoid the server stretching or centring the
image.

Check what you actually got:

```sh
xdpyinfo | grep "depth of root window"
```

and restart X after editing (`service xdm restart`, or reboot if you are using
`startx`).

| Depth | Result |
|---|---|
| 16-bit | boots and runs |
| 24-bit | "Unable to activate screen" |

On a modern host the same requirement is met by running a 16-bit X server for
the game alone (`Xephyr -screen 800x600x16 :1`), which needs no VM and no code
changes. The port sidesteps it entirely by owning the framebuffer.

## 3. Fullscreen, and the launch ritual

Fullscreen is sensitive to how the session was started and to when the CD was
mounted. What was observed:

| Condition | Result |
|---|---|
| CD mounted before launch | broken fullscreen — centred, offset, borderless-window artefacts |
| CD mounted *after* the game asks for it | correct fullscreen, disc recognised, no artefacts |
| Launched from a KDE session | window decorations and focus conflicts, inconsistent positioning |
| Launched from a bare `startx` session | correct fullscreen, nothing to interfere |
| The whole VM window fullscreened on the macOS host | fine — the game stays at 640×480 or 800×600 and the host layer scales it, cursor alignment stable |

### The sequence that works

1. Leave the CD unmounted.
2. Start the game from a TTY or a minimal `startx` session, not from inside a
   desktop environment.
3. Wait for the disc prompt — on the Spanish release, *"Por favor, inserta el
   disco Theocracy"*.
4. Mount it: `mount /cdrom`.
5. Return to the game. It finds the disc and continues, and fullscreen stays
   correct for the rest of the session.

For a dedicated game session with no window manager at all:

```sh
echo "/usr/games/theocracy" > ~/.xinitrc
chmod +x ~/.xinitrc
startx      # from a TTY
```

### How much of this is explained

The window-manager half is ordinary: a bare X session has nothing to add
decorations, reparent the window or steal focus, so the game gets the screen it
asked for.

The CD half is **not explained by anything in the disassembly**. The only CD
interaction found is `VM_GetCDRomName`, which opens `/mnt/cdrom/cd.key` and
checks that the body reads `Theocracy`
([../porting/guest-libmvos.md](../porting/guest-libmvos.md), G6b) — a single
file read with no path to the video code. Mount timing changing the *window
geometry* has no mechanism we can point at. It is recorded here as an
observation, and it is the one item below in "Open threads".

## 4. Audio

The engine is OSS and only OSS: it opens `/dev/dsp` directly and configures it
with `SNDCTL_DSP_SETFRAGMENT`, with no ALSA, no daemon and no fallback — see
[../subsystems/platform-audio-threads.md](../subsystems/platform-audio-threads.md).

**"Permission denied" or "no such device".** OSS does not share a device between
clients, and KDE's `artsd` holds it. Kill it, or turn it off for good in Control
Centre → Sound & Multimedia → Sound System:

```sh
killall artsd
```

If the device node itself is missing:

```sh
mknod -m 666 /dev/dsp c 14 3
```

**"No such device" with the node present.** Nothing is driving the emulated
hardware. For AC97 on a 2.4 kernel:

```sh
modprobe i810_audio
```

Add `i810_audio` to `/etc/modules` to make it stick.

**Pick AC97 in the VM, not SB16.** The QEMU/UTM default SoundBlaster 16 plays,
but stutters and drifts out of sync. AC97 is emulated well by QEMU and a 2.4
kernel still has a driver for it:

```
-device AC97,audiodev=audio0
```

With that combination sound was smooth and needed no mixer configuration.
Neither ALSA nor PulseAudio is involved: the engine never uses ALSA, and
PulseAudio postdates this software.

| Component | Setting |
|---|---|
| VM audio device | AC97 |
| Kernel module | `i810_audio` |
| Sound daemon | `artsd` disabled |
| Device | `/dev/dsp`, confirmed present and openable |

## Open threads

- **Why mounting the CD before launch breaks fullscreen.** Reproducible at the
  time, unexplained now. The known CD check is a file read that touches no video
  state. Either there is a second CD interaction we have not found, or the
  correlation was with something else that changed at the same time; the
  evidence cannot distinguish the two. The cheap test is the port itself:
  `THEOC_CD` points the CD check at any directory, so the check can be made to
  succeed or fail on demand without a disc, and the window geometry watched.
- **Which screen switch a real 24-bit failure dies on first.** The 16→15→`Fatal`
  block in §2 is duplicated across the image; the one a modern X server would
  hit at startup has not been pinned to an address, only the two save-path
  copies have.
