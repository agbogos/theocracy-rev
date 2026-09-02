# Game data census (`data/game/data/`)

A survey of the extracted CD data (from `tdat.pck` — see
[phls-format.md](phls-format.md)): 7191 files in 489 directories. The `data/`
tree is the game's runtime root, reached through the launcher's `./data`
symlink.

The port does not need any of this — the real engine loads every format itself.
The census is here for format archaeology, and as the starting point for anyone
reading the assets outside the game. Files are as-shipped: the config/text layer
is XOR-encrypted and decrypted at runtime by `cTextFile`, so the canonical tree
must **not** be pre-decrypted (phls-format.md §1).

## Formats by count

| Ext | Count | Enc | What | On-disk struct (engine) |
|-----|------:|:---:|------|-------------------------|
| `.txt` | 4104 | yes | help/encyclopedia prose (×6 languages) + `selap.txt` balance | text |
| `.raw` | 1454 |  | raster images (backgrounds, panels) | `sRawPicHeader` (`mhwanh`) |
| `.spn` | 854 |  | sprite packs | sprite (`cSprite`/`sSPR*`) |
| `.idx` | 196 | yes | per-animation frame index (`mananim/`): `frame flag` lines | text |
| `.wav` | 140 |  | sound samples | `sWave`/`sRiff` |
| `.sp0` | 105 |  | sprites | `sSPR0` (`LastChance_SPR0`) |
| `.map` | 81 |  | maps | map loader (`FUN_081c7a00`) |
| `.drn` | 60 | yes | localized MP map descriptions | text |
| `.man` | 55 | yes | mission unit spawn tables (`mission/mancfg/`): `type tribe x y count` | text |
| `.cfg` | 44 | yes | menu + help-system config | text (INI) |
| `.anim` | 37 |  | FLC/animations | `sFLC_*`/`sMVOSANIMHeader` |
| `.dsc` | 16 | yes | tribe formation definitions (`descr/forms/`, `>`-scripted) | text |
| `.pic` | 10 |  | images | (tbd) |
| `.sp1` | 9 |  | sprites | `sSPR1` |
| `.mft`/`.dat`/`.sdb` | 9/9/6 |  | (tbd — manifest / data / db?) | (tbd) |
| `.pal` | 1 |  | palette | `cData_Palette` |
| `.ico` | 1 |  | icon | — |

Encrypted total: 4473 (all text-family: `.txt`/`.idx`/`.drn`/`.man`/`.cfg`/
`.dsc`). Binary assets are plaintext.

## What feeds the simulation

- `data/selap.txt` — the balance file, read throughout the simulation, and the
  bulk of the `cTextFile` traffic at boot. INI sections: `[Building_sys]`,
  `[Buildings]` (per-building stone/wood/jewel/room costs), `[Man]` (unit + hero
  combat stats: attack/defense/HP/stamina, `HEROnn_MOD_*`), `[Education]`,
  `[Food]`, `[Mission]`, `[Mitem]` (magic items), `[Scenario]`,
  `[Spell_Cost_And_ConcTime]`, `[Spell_Others]`, `[OtherValues]`, `[Network]`,
  `[Emulation]`.
- `data/mitem.cfg`, `data/menu/menu.cfg` (`entry x y` menu layout),
  tutorial texts, and the `mananim/*.idx` animation indexes.
- `mission/mancfg/*.man` — scripted unit placement per mission;
  `descr/forms/*.dsc` — formation shapes per tribe.

## Front-end and localized data

`data/help/` is the in-game encyclopedia: `help/config/*.cfg` (topic tree) +
`help/texts/{english,french,german,italian,spanish,magyar}/…` prose. `.drn` map
descriptions are likewise per-language. Six languages ship; Hungarian (`magyar`)
dev comments appear in `.dsc` files (Philos was Hungarian — see the heritage
note in [overview.md](../overview.md)).

## Regenerating a decrypted tree for inspection

```
python3 tools/phls_extract.py data/cd/tdat.pck /tmp/game-dec --decrypt
# or decrypt an already-extracted tree in place:
python3 tools/theocracy_crypt.py --tree /tmp/game-dec
```

## Open threads

- The asset loaders — the `c…` (runtime) ↔ `s…` (on-disk) pairings: FLC video,
  `sSPR1` / `sTER1` sprites and terrain, and the bitmap, font, sample and
  palette formats. The largest block of file-format archaeology left, and what
  an asset viewer or a modding effort would have to read first.
- The map loader `FUN_081c7a00`, and the file read it performs.
