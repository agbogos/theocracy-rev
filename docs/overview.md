# libmvos.so — Technical Report

Reverse-engineering notes for the Theocracy (Philos Laboratories, 2000) game
engine shared library.

## Binary facts

- **Type:** ELF 32-bit shared object (x86), load base `0x00010000` in Ghidra
  (**file addresses = Ghidra − 0x10000**; `objdump` shows file addresses).
- **Toolchain:** GCC 2.x (g++ 2.95-era, GNU v2 mangling, sjlj exceptions).
  `.symtab` is stripped but — being a shared object — the **`.dynsym` carries
  all 2400 exported C++ symbols** with full signatures encoded in the mangling
  (huge win for this project). The game binary imports 232 of them.
- **Code:** `.text` `0x52980`–`0xa8c95` (~353 KB).
- **Functions:** 4120 total — 2199 named (from symbols), 1921 unnamed
  (`FUN_`, mostly thunks/statics/exception glue).
- **Role:** shared engine. The game's main executable links against it and calls
  in via PLT (engine classes appear there as `EXTERNAL` imports). See sibling
  analysis of the game binary.
- Names are stored **demangled**: method name is the symbol, class is the Ghidra
  namespace; each class also has a `<class>_type_info_function` (RTTI).

## Distribution inventory (`linux/` folder — the Linux CD install set)

| File | What |
|------|------|
| `theocracy.real` (5.6 MB) | the game, ELF i386, `.symtab`-stripped but 348 dynamic exports + RTTI |
| `theocracy`, `theoserver` | launcher shell scripts (set up `~/.theocracy`, symlink `data/`, copy `mvos.cfg`, set `LD_LIBRARY_PATH`) |
| `server` (47 KB) | standalone MP server (links libmvos + libc only) |
| `libmvos.so.0.9` | the engine (this doc) |
| `libmvos_{vvc,keyboard,mouse,pointer}_x.so.0.9` | X11 device plugins ([porting/vvc_x-backend.md](porting/vvc_x-backend.md)) |
| `libsmpeg.so.1.0`, `smpeg-philos.tgz` | Loki SMPEG (MPEG-1 movies) + Philos' patched **source** |
| `inst.linux`, `uninst.linux` | installer/uninstaller. `inst.linux` is **fully unstripped** (518 syms): zlib statically linked + `Unpack(cString, cString, cDirEntry)` — RE target for a native CD-data extractor |
| `readme.linux` | notes the 15/16bpp requirement (root-caused in vvc_x doc) and the broken in-game MP-server spawn (root-caused: `cTask::Launch` fork/execlp + era glibc bug) |

Game **data is not supplied in this repo** — installed from CD by `inst.linux`.

## Heritage note — MVOS is an AmigaOS homage, not a port

The API recreates AmigaOS concepts on Linux:
`cIntuition`+`cScreen`/`ActivateScreen` (Intuition screens), Exec-style lists
(see [memory-and-containers.md](subsystems/memory-and-containers.md)), message
ports (`MVOSMessagePort`), `cTask` vs `cThread` (Task/Process split), `CopyMem`,
rawkey codes/`KeyMatrix`/qualifiers (keyboard.device vocabulary), `WaitVBlank`.

But the pixel pipeline is PC-native chunky linear-framebuffer (`cGD_LFB*`,
15/16bpp first-class) so this is Amiga-fluent devs
(Hungarian scene) rebuilding their preferred idiom for PC, not a converted Amiga
codebase. Useful practically: AmigaOS docs are a design map for unfamiliar MVOS
corners.

## Architecture — class map by subsystem

~200 classes. Naming conventions: `c…` = class, `s…` = struct/POD (mostly
on-disk/wire formats), `t…<T>` = template instantiations (containers). `_Linux`
suffix = platform backend behind a portable interface.

### Core primitives
- Memory: `cMemBlock`, `cMemBlockPTR`, `cMemBlock_`, `cSystemMemory`,
  `cHeapBlock`, `cHeap_Compatibility`, `cMemoryPipe`
- Containers: `cList`/`cNode`, `cHList`/`cHNode` (hashed?), `cArray<char*>`,
  template `tList<T>`/`tNode<T>`/`tHNode<T>`/`tMemBlock<T>`
- Strings/streams: `cString`, `cOldString`, `cStream`, `cPipe`
- Util: `cConvert`, `cStdConv`, `cRandom`, `cClientInfo`, `cLibrary`; helper
  namespaces `CHARCONVERT`, `CHARFILTER`, `DEC`, `HEX`, `DOUBLE`

### Filesystem
- `cFile`, `cTextFile`, `cDirectory`, `cDirent`, `cDirentname`,
  `cCompatibilityFileSystem`, `cFFilter`, `cFFHidden`, `cProcIniFile`

### Graphics device (GD) & display
- Framebuffer backends: `cGD` (base) + `cGD_LFB8/15/16/24/32` (linear),
  `cGD_SFB8/16` (segmented?), `cGD_Minimal`
- Display/mode: `cDisplay`, `cScreen`, `cVVC`, `cLibVVC`, `cVCD`, `cVMode`,
  `cVModeRequest`, `sVModeInfo`, `cDimension`

### Imaging: bitmaps / fonts / palettes
- Bitmaps: `cBitmap`, `cAnimBitmap`, `cData_Bitmap`, `cData_AnimBitmap`,
  `cSharedData_Bitmap`, `cSharedData_AnimBitmap`, `cData_MapItem`; on-disk
  `sBMPInfoHeader`, `sRawPicHeader`
- Fonts: `cFont`, `cData_Font`
- Color/palette: `cColor`, `cPalette`, `cPalette15/16/32`, `cPaletteFull`,
  `cData_Palette`
- Sprites: `cSprite`, `cSprABitmapAdd`, `cSprClick`; on-disk `sSPR1`,
  `sTER1` (+ `LastChance_SPR0/TER0`)

### Animation / video (FLC/FLIC)
- `cFLCAnimPlayer`, `cAnimSkeleton`, `cAnimTextReader`; on-disk `sFLC_*` family
  (header, frame variants: ByteRun, LC, SS2, Color/Color256, Uncompressed,
  Black, Sound), `sMVOSANIMHeader`

### Audio
- High level: `cAO` (audio object?), `cSample`, `cData_Sample`, `cMixer`,
  `cSoundPlay`, `cVolume`, `cVolumeHP`
- Cards/channels: `cSoundCard` + `_Dummy`/`_Linux`/`_SoftwareMix`,
  `cSoundChannel` + `_SoftwareMix`, `cSoundServer`, `cSoundServerChannel`
- Format/convert/record: `cSoundConvert`, `cSoundFormat`,
  `cSoundRecorderBuffer`, `cSoundRecorderThread`, `cSyncSystem`; on-disk
  `sWave`, `sRiff`

### Input & events
- `cKeyboard`/`cLibKeyboard`/`cKeySequence`, `cMouse`/`cLibMouse`,
  `cPointer`/`cLibPointer`, `sInput`, `cEventManager_Linux`, `cEvent_Forwarder`

### UI — Visual Objects (VO) & console
- Widgets: `cVObject` (base), `cVOAButton`, `cVOButton`, `cVOBitmap`,
  `cVOConsole`, `cVODragBox`, `cVOEditRow`, `cVOFGraphs`, `cVOFiler`,
  `cVOListReq`, `cVOMsgBox`, `cVOMultiLine`, `cVOPulldown`, `cVOSliderV`,
  `cVOTextBox`, `cVOWindow`
- Management/editing: `cMasterVO`, `cIntuition`, `cEditRow`, `cLineEditor`,
  `sVOMessage`
- Console: `cConsole`, `cConsoleVO`, `cShell`

### App / process / threading / time
- `cApplication`, `cProcess`, `cTask`, `cThread`, `cSemaphore`
- Time: `cLinuxTimer`, `cTimerSystem`(+`_Linux`), `cDayTime`, `cVTimer`

### Networking / IPC / messaging
- IPC (IPX + TCP/IP variants): `cIPCBrowser`, `cIPCO`, `cIPCServer`,
  `cIPCSession`, `cIPCSystem`(+`_Linux`), `cIPCOStream2Block`
- IPX: `cIPXPacketHeader`, `cIPXSocket`(+`_Linux`)
- Messaging: `cMsgCenter`, `cMsgReceiver`, `cMsgSender`, `cMsgRecNode`,
  `cMsgTypeNode`

### Environment / config / locale
- `cEnvSystem`, `cEnvClass`, `cEnvVar`, `cLocaleDataBase`, `cLocaleEntry`;
  logging namespaces `Log_File`, `Log_SafeFile`, `Log_Stdout`

### CD
- `cCD_Linux`

## Progress

- Memory & containers — See
  [subsystems/memory-and-containers.md](subsystems/memory-and-containers.md).
  Key result: `cSystemMemory` is a 32 MB budgeted, evictable asset cache;
  `cMemBlock` = lockable/prioritized heap block; `cString` *is* a `cMemBlock`;
  `cList` is an AmigaOS Exec-style list.
- Application bootstrap — See
  [subsystems/application-bootstrap.md](subsystems/application-bootstrap.md).
  Key result: `main()` lives in libmvos (Ghidra `0xa51e0`) and calls the
  game's exported `Init`/`Start` — full framework inversion. Platform: MPEG via
  SMPEG, device backends dlopen'd (shipped family is X11, see
  [porting/vvc_x-backend.md](porting/vvc_x-backend.md)), SIGALRM heartbeat, BSD
  sockets + IPX, pthreads. Engine and game are mutually linked (game exports
  348 symbols back).
- Audio / threads / processes — See
  [subsystems/platform-audio-threads.md](subsystems/platform-audio-threads.md).
  OSS `/dev/dsp` + software mixer on a `cThread` (pthread+pipe); `cTask` =
  fork/execlp.
- Video plugin — See
  [porting/vvc_x-backend.md](porting/vvc_x-backend.md): complete backend
  contract incl. `SetVideoMode` depth table, no-op `ShowBuffer`, event pump.
- Port — PLAYABLE (via guest-libmvos): both `theocracy.real` and real
  `libmvos.so` run under Unicorn; only the OS/library boundary is HLE'd.
  Single-player and multi-player are playable end-to-end. See
  [porting/guest-libmvos.md](porting/guest-libmvos.md). (The earlier pure-HLE
  native-replace plan in
  [porting/macos-hle-emulator.md](porting/macos-hle-emulator.md) is superseded
  but remains the best ABI-contract/boot writeup.)
