# struct cTribe

A faction/realm. `new(0x84)` (132 bytes), one per slot in
`cGameSession::tribes[11]`. Built by `cTribe_Construct(this, template)` then
`cTribe_SetId(this, id)`. Game binary.

## Layout (0x84 bytes)

| Offset | Type | Name | Meaning / evidence |
|--------|------|------|--------------------|
| `0x00` | `u8` | `id` | Tribe index (0–10). Set by `cTribe_SetId`. |
| `0x01` | `u8 [7]` | `_resv0` | ctor zeroes `+1..+4`; `+1..+7` not yet identified. |
| `0x08` | `i32 [11]` | `relations` | Diplomacy state toward each tribe. `0`=unknown (`cTribe_IsKnown` = `!=0`), `5`=allied (`cTribe_IsAllied`), `1`/`3`=other states (inferred neutral/war). Diplomacy valid only among tribes **0–5** (`cTribe_CheckValidTribe`). |
| `0x34` | `i32 [5]` | `resource` | Five per-tribe resource/mana pools. Init `g_TribeResourceInit` (`0x84c85f1`); `cTribe_AddResource(amt, idx)` adds & clamps to `g_TribeResourceMax` (`0x84c85f5`). Both globals are **scalars**, not per-resource tables — all five pools share one starting value and one cap. |
| `0x48` | `ptr` | `controller` | Per-tribe object (AI/orders/command state); allocated elsewhere, freed+nulled by `cTribe_FreeController`. Sub-objects at `controller+0x2c/+0x30/+0x50`. |
| `0x4c` | `ptr` | `template` | Static tribe definition = `DAT_08645240 + id*0x10`. **Decoded: 4× `int32`** (see roster below). |
| `0x50` | `u8` | `flag0` | zeroed in ctor |
| `0x51` | `u8 [5]` | `_flags` | zeroed in ctor (per-resource flags?) |
| `0x56` | `i32 [6]` | `allyDate` | World-clock timestamp of alliance with each playable tribe (0–5). `cTribe_GetAllyDate` (asserts allied), stamped by `cTribe_Modification`. Compared against the world tick in `SimulationStep`'s periodic event. |
| `0x6e` | `u8 [22]` | *(unmapped)* | tail `+0x6e..+0x83` not yet identified. |

## C form (partial)
```c
struct cTribe {                 // 0x84
    uint8_t  id;                    // 0x00
    uint8_t  _resv0[7];             // 0x01
    int32_t  relations[11];         // 0x08  (0=unknown, 5=allied, 1/3=?)
    int32_t  resource[5];           // 0x34
    void*    controller;            // 0x48  (AI/orders)
    void*    template_;             // 0x4c  (DAT_08645240 + id*0x10)
    uint8_t  flag0;                 // 0x50
    uint8_t  _flags[5];             // 0x51
    int32_t  allyDate[6];           // 0x56
    uint8_t  _tail[22];             // 0x6e  (unmapped)
};
```

## Tribe roster (11 slots)
From `cTribe_SetId`'s initial-relations logic:
- **0–5** — the playable/competing realms (start `unknown` to each other; must
  meet; full diplomacy).
- **6, 7, 8** — minor factions (6 starts at relation `1`; 7/8 default `3`).
- **9, 10** — always **allied** (`5`) to everyone → nature/neutral/independent.

## Diplomacy API (named)
`cTribe_IsKnown` (`relations[x]!=0`), `cTribe_IsAllied` (`==5`),
`cTribe_CheckValidTribe` (`x<6 && id<6`), `cTribe_CloseBorders`,
`cTribe_Modification` / `cTribe_GetAllyDate` (allyDate), `cTribe_AddResource`.

## Roster template (`cTribe::template`, 16 bytes)
Filled once by `InitTribeRoster` (`0x817bdd0`) via `TribeTemplate_Set4(entry, a, b, c, d)`. Each of the 11 entries (`DAT_08645240 + id*0x10`) is **four `int32`s**:
- **Tribes 0–7:** values from compile-time `.data` constants at `DAT_084c817c`
  (8 × 4 = 32 ints, `0x84c817c..0x84c81fb`) — distinct per tribe.
- **Tribes 8–10** (nature/neutral): literal `100, 100, 100, 100` (baseline).

So the four ints are **per-tribe balance/difficulty parameters** (percent-style;
neutrals = 100% baseline). Exact meaning of each of the 4 not yet pinned — needs
the raw `.data` values (visible directly in Ghidra at `0x84c817c`) or a
`tribe->template` reader. (`InitTribeRoster` also sets up the `flyinghelp1/2`
tooltip locale entries and the `small_1pix` font — it's the general game-data
init, not tribes only.)

## Still open
- `+0x01..+0x07`, `flag0`/`_flags`, and the `+0x6e..+0x83` tail (~22 bytes) —
  likely population/AI bookkeeping. Trace resource getters and `controller`
  (`+0x48`) users next.
- The **16-byte roster template** at `DAT_08645240` (name/color/type per tribe)
  — decode for the human-readable faction identities.
- Confirm relation codes `1` vs `3` (neutral vs war).
