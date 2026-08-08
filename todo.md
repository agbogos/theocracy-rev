# todo

Open tasks only. A task leaves this file when it is done — the finding it
produced goes into `docs/`, which stays the record. Nothing here is a plan or a
rationale; if it needs either, it belongs in a doc that this file links to.

Two sections because the split matters: **YOU** is work that needs a machine,
hardware or a judgement only you have. **CLAUDE** is work I can do unattended.

---

## YOU

### 1. Windows on bare metal — blocked until ~2026-08-18

Hardware arrives via a third party around then. When it does, on that machine:

```
win-timing-probe.exe
win-timing-probe.exe --busy <core count>
win-timing-probe.exe --busy <core count x 2>
```

plus one ordinary game session with `THEOC_FPS=1`.

Two specific things only bare metal answers, both recorded in
[`docs/porting/other-os-ports.md`](docs/porting/other-os-ports.md), "The
contention runs": whether the ~98 ms loaded province frame is a VM artefact, and
what happens on a machine where nothing has already raised the global timer
resolution to 1 ms (the VM's was already raised before the probe ran).

### 2. Optional: TOC of the other releases — not blocking anything

The UK rip works and the port ships one track mapping. Unknown: whether that
mapping holds for a *different* release, i.e. whether a tester with the Chinese
or prototype disc gets the right music or nonsense. The Chinese `.mds` sidecar
carries a full TOC and needs no drive at all, so this is desk work whenever you
feel like it. Agreement means the numbering is release-invariant; disagreement
is a finding and the port would have to detect rather than assume.
[`music-and-redbook.md`](docs/subsystems/music-and-redbook.md)

---

## CLAUDE

### 1. Diff the newly ripped discs against the working CD tree

Three sources to compare: `data/cd-uk/theocracy-d1.iso`,
`data/cd-uk/theocracy-d2.iso`, and `data/cd/` — the tree every address and
finding in `docs/` was derived from, whose provenance is now only *inferred*
(the LCID splash bitmaps say European/UK, nothing says which disc).

Produce a per-file SHA-256 manifest for each source and a structured diff:
identical / differs / only-here. Call out `linux/` (the binaries we actually
run), `tdat.pck` / `tex.pck`, and `movie/` specifically.

`bsdtar -tf` and `bsdtar -xOf` read ISO9660 on macOS, so **no mounting and no
sudo** — prefer that to `hdiutil attach`. Write the manifests and the report
under `data/` as untracked output; they are derived from copyrighted content, so
keep them out of git and confirm `.gitignore` covers wherever they land.

What it answers: whether `data/cd/` is disc 1 of this exact release, whether
disc 2 holds anything the port has never seen, and whether any file the port
loads differs between them.

### 2. Config file for the `THEOC_*` knobs

Every runtime knob is an environment variable today
([`diagnostics.md`](docs/porting/diagnostics.md) is the canonical list). For
release bundles they need to be settable permanently, in a file read at startup.

Constraints that matter:

- **Absent file = today's behaviour, exactly.** No new failure mode for anyone
  who never writes one.
- **Environment wins over file**, so a one-off `THEOC_X=1 ./theoc` still
  overrides a permanent setting.
- **Do not call it `mvos.cfg`.** That name is taken by the *guest's* own config,
  which libmvos parses as guest code — two config files with one name is a trap.
- Ship a commented default from `tools/package-linux.sh` /
  `tools/package-windows.sh`, listing the knobs a player might actually want
  (`THEOC_MUSIC_VOL`, `THEOC_SCANLINES`, `THEOC_FRAME_MS`, …) rather than all of
  them.
- `diagnostics.md` gains a line saying how the file and the environment relate.
