# PHLS archive format (`*.pck` data packs)

Reverse-engineered from the CD packs (`data/cd/tdat.pck`, `tex.pck`). Extractor:
[`tools/phls_extract.py`](../../tools/phls_extract.py). Resolves open-question
#24 (native CD-data extractor) — no need to run `inst.linux`.

```
python3 tools/phls_extract.py data/cd/tdat.pck data/game   # -> data/game/data/...
python3 tools/phls_extract.py <archive.pck> <outdir> --list  # dry-run / inspect
```

## Container

The `.pck` files are **gzip-wrapped** (`1f 8b`); after decompression the payload
is a **PHLS** archive:

```
offset  size  field
0x00     4    magic "PHLS"
0x04     4    uint32  dir_block_bytes   (LE; size of the record block)
0x08   dir_block_bytes   record block  — dir_block_bytes/64 records
 ...          file data — all file payloads concatenated, in record order
```

Each record is **64 bytes**:

```
0x00    60   char   name[60]   NUL-padded
0x3c     4   uint32 size       LE
```

The record block is a **flattened depth-first traversal** of the directory tree:

- `size & 0x80000000` → `name` is a **subdirectory**; descend into it.
- a record named `..` → **pop** back to the parent directory.
- otherwise → a **file** of `size` bytes; its payload is the next `size` bytes
  of the file-data region (files are stored contiguously in record order).

There are no per-file offsets — position is implicit from the DFS order, so a
reader walks records and a data cursor in lockstep. Verified byte-exact: for
both packs `data_base + Σ(file sizes)` equals the decompressed length exactly
(`tex.pck`: 8 files; `tdat.pck`: **7191 files / 489 dirs**, 0 leftover bytes).

`tdat.pck` is the Linux game data: everything lives under a top-level `data/`
dir → extracts to `data/game/data/…` (anim, map, locale, menu, sounds, scenario,
campaign, …). `tex.pck` is the **Windows** installer payload (`Setup.exe`,
splash BMPs, `theocracy-*.exe/.icd`) — not needed for the port.

## Inner layers (not part of the container)

Two independent layers sit *inside* extracted files:

1. **Text/config encryption — SOLVED.** Config/text files begin with the ASCII
   marker **`RSA4096`** (7 bytes) followed by ciphertext. The `RSA4096` name is a
   joke — the actual cipher is a symmetric **XOR against two short repeating
   keys**:

   ```
   out[i] = in[i] ^ key2[i % 17] ^ key1[i % 13]      (i indexes the body)
   key1 = "theocracy sux"        (period 0x0D = 13)
   key2 = "mutant technology"    (period 0x11 = 17)
   ```

   Its own inverse; verified byte-exact (`encrypt(decrypt(selap.txt)) ==
   original`). Only header-bearing files are encrypted — `.cfg`/`.txt`/`.idx`
   (e.g. `selap.txt` → `[Buildings] BARRACKA1_STONE=120…`, `menu.cfg`). Files
   without the marker (e.g. `hero.cfg`, all binary assets) are plaintext and
   must **not** be XORed. This is the heavy `cTextFile::OpenR/ReadLine`+`sscanf`
   path M1 saw — the game decrypts on read. Tooling: `tools/theocracy_crypt.py`
   (module + CLI; `phls_extract.py --decrypt` decrypts on extract) — our own
   implementation, verified byte-exact against the shipped files.

   **The port does not decrypt anything.** The superseded pure-HLE layer did port
   `XorBuff` into a native `cTextFile` (`port/src/mvos.cpp`, no longer linked);
   under [guest-libmvos](../porting/guest-libmvos.md) the engine's *real*
   `cTextFile` runs as guest code and decrypts on read, exactly as it did in 2000.
   The host only serves bytes. So the canonical tree must stay **as-shipped
   (encrypted)** — a pre-decrypted tree would be XORed a second time by the game
   and parse as garbage.
2. **Raw image format** — `.raw` assets start with `mhwanh` = the engine's
   `sRawPicHeader` (see [overview.md](../overview.md) imaging section). Plaintext;
   decode when the bitmap loaders are implemented.
3. **The `.sdb` locale database — SOLVED 2026-08-08.** `data/locale/*.sdb` (one
   per language) carries every piece of UI and flavour text, keyed by name. It
   does **not** use the `RSA4096` scheme above and has no marker: the whole file
   is a flat **XOR against the single byte `0x2a`**, length fields included.

   ```
   u32            entry count            (e.g. english.sdb = 1689)
   repeated:  u8  key length
              …   key bytes      ^0x2a   (e.g. "manname_hero1")
              u8  0x00                   (plaintext NUL, i.e. 0x2a in the file)
              u32 text length    ^0x2a
              …   text bytes     ^0x2a
   ```

   Two traps, both of which cost time here. A naive single-byte-XOR scan ranks
   `0x2a` *below* the right answer, because plaintext spaces (`0x20`) are stored
   as `0x0a` and a scorer reads those as newlines in the wrong candidate. And the
   NUL between key and length is a NUL **in the file**, so it appears as `0x2a`
   after decoding — searching the decoded buffer for `\x00` finds nothing and
   silently returns no entries.

   Reader: `cLocaleEntry` (`__12cLocaleEntryPCc`), 24 bytes, text pointer at
   `+0x10`. Entries are constructed by key at fixed addresses — so the array
   behind any indexed lookup can be bounded exactly by reading the constructor
   run, as done in [heroes.md](../subsystems/heroes.md). There is no tool for
   this yet; the parse above is the whole format.

## Runtime filesystem shape (for M2 HLE)

The `theocracy` launcher runs the game with CWD `~/.theocracy`, containing a
`data` symlink (→ the extracted tree) and a copied `mvos.cfg`. So our HLE
filesystem root should expose `./data/…` (→ `data/game/data`) and `./mvos.cfg`.
**`mvos.cfg` is not in the packs** — `inst.linux` installs it. **Resolved**
: rather than reversing the installer, we ship a hand-authored
minimal `data/game/mvos.cfg`, reconstructed from the `EnvSystem` keys the boot
actually reads — `[vmachine] device/fullscreen/fillobjmem/cdrom_mountpoint`,
`[sound] card`, `[network] enable`. It is tracked in git via a `.gitignore`
carve-out, since the rest of `data/game` is extracted content. Note `[vmachine]
fullscreen` is **inert** under the current port: the engine's fullscreen path ran
through the X11 plugin's `_MOTIF_WM_HINTS` + `XF86VidModeSwitchToMode`, which the
SDL backend replaced wholesale — use `THEOC_FULLSCREEN=1` instead.
