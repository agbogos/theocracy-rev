# Game data census (`data/game/data/`)

A light survey of the extracted CD data (from `tdat.pck` — see
[phls-format.md](phls-format.md)), to inform which loaders M2+ needs and in what
priority. **7191 files / 489 dirs.** The `data/` tree is the game's runtime root
(the launcher's `./data` symlink). Files are as-shipped: the config/text layer is
XOR-encrypted and decrypted at runtime by `cTextFile` (do **not** pre-decrypt the
canonical tree — see phls-format.md §1).

## Formats by count

| Ext | Count | Enc | What | On-disk struct (engine) |
|-----|------:|:---:|------|-------------------------|
| `.txt` | 4104 | ✓ | help/encyclopedia prose (×6 languages) + `selap.txt` balance | text |
| `.raw` | 1454 |  | raster images (backgrounds, panels) | `sRawPicHeader` (`mhwanh`) |
| `.spn` | 854 |  | sprite packs | sprite (`cSprite`/`sSPR*`) |
| `.idx` | 196 | ✓ | per-animation frame index (`mananim/`): `frame flag` lines | text |
| `.wav` | 140 |  | sound samples | `sWave`/`sRiff` |
| `.sp0` | 105 |  | sprites | `sSPR0` (`LastChance_SPR0`) |
| `.map` | 81 |  | maps | map loader (`FUN_081c7a00`) |
| `.drn` | 60 | ✓ | localized MP map descriptions | text |
| `.man` | 55 | ✓ | mission unit spawn tables (`mission/mancfg/`): `type tribe x y count` | text |
| `.cfg` | 44 | ✓ | menu + help-system config | text (INI) |
| `.anim` | 37 |  | FLC/animations | `sFLC_*`/`sMVOSANIMHeader` |
| `.dsc` | 16 | ✓ | tribe formation definitions (`descr/forms/`, `>`-scripted) | text |
| `.pic` | 10 |  | images | (tbd) |
| `.sp1` | 9 |  | sprites | `sSPR1` |
| `.mft`/`.dat`/`.sdb` | 9/9/6 |  | (tbd — manifest / data / db?) | (tbd) |
| `.pal` | 1 |  | palette | `cData_Palette` |
| `.ico` | 1 |  | icon | — |

Encrypted total: **4473** (all text-family: `.txt`/`.idx`/`.drn`/`.man`/`.cfg`/
`.dsc`). Binary assets are plaintext.

## What feeds the simulation

- **`data/selap.txt`** — the **master balance file** the sim reads pervasively
  (the heavy `cTextFile` boot path M1 saw). INI sections: `[Building_sys]`,
  `[Buildings]` (per-building stone/wood/jewel/room costs), `[Man]` (unit + hero
  combat stats: attack/defense/HP/stamina, `HEROnn_MOD_*`), `[Education]`,
  `[Food]`, `[Mission]`, `[Mitem]` (magic items), `[Scenario]`,
  `[Spell_Cost_And_ConcTime]`, `[Spell_Others]`, `[OtherValues]`, `[Network]`,
  `[Emulation]`.
- **`data/mitem.cfg`**, **`data/menu/menu.cfg`** (`entry x y` menu layout),
  tutorial texts, and the `mananim/*.idx` animation indexes.
- **`mission/mancfg/*.man`** — scripted unit placement per mission;
  **`descr/forms/*.dsc`** — formation shapes per tribe.

## Front-end / localized (lower priority for first pixels)

`data/help/` is the in-game encyclopedia: `help/config/*.cfg` (topic tree) +
`help/texts/{english,french,german,italian,spanish,magyar}/…` prose. `.drn` map
descriptions are likewise per-language. Six languages ship; Hungarian (`magyar`)
dev comments appear in `.dsc` files (Philos was Hungarian — see the heritage note
in [overview.md](../overview.md)).

## To regenerate a decrypted dev tree (inspection only — never the canonical one)

```
python3 tools/phls_extract.py data/cd/tdat.pck /tmp/game-dec --decrypt
# or decrypt an already-extracted tree in place:
python3 tools/theocracy_crypt.py --tree /tmp/game-dec
```
