# TELOS for the Octatrack: the MODS page and conflicts

Part of [README.md](README.md).

## 5. On the unit: the MODS page

MAIN MENU > MODS (settings-ot's fifth category) gets the mods above the
settings. The menu engine draws label/value rows, 7 at a time, with
headings; settings-ot already builds that list at run time.

```
 MODS
 ── MODS ON THE CARD ──
 MIDI SCENES          ON
 QUANTIZER          +ON      ticked, applies at the next start
 SYNTH              -OFF     unticked, applies at the next start
 TUNER               OFF
 REPITCH             ERR     refused at the last start
 APPLY             2 CHG
 ── QUANTIZER ──
 SCALE             MAJOR     the mods' own settings, as now
```

- **The rows.** One per `.elemod` file in `MODS/` and its folders, by the
  mod's title (the file's name until it is read), in name order; a folder is
  a heading. The value says what will be, against what is: `ON`, `OFF`,
  `+ON` (ticked, not yet running), `-OFF` (unticked, still running), `ERR`
  (refused or dropped at the last boot), `NEW` (on the card, never ticked).
- **YES** ticks or unticks. Every tick runs the check (section 6, below) on the set
  as it would be, at once (a check of a few mods takes milliseconds; the
  files' headers are read when the page opens, in the engine task).
- **FUNC + YES** on a row opens its card (stock's dialog, three lines):
  title and version; author; and the state, or the reason for `ERR` from
  `.status` (`CLASHES WITH SYNTH`, `NEEDS CORE 0.42`, `RUNS AT START-UP:
  FLASH IT`).
- **APPLY** (YES) writes `active` from the ticks. It asks "SAVED. APPLY AT
  NEXT START. TURN OFF NOW?" and shows how much of the TELOS area the set
  takes (`AREA 412K/1024K`). Nothing changes in the running unit.
- **Leaving** the MODS page with changes not applied asks once: "APPLY
  CHANGES? YES / NO: DISCARD".
- **Safe start.** [NO] held at power-on loads no TELOS mod, and the page
  says so on its first row (`SKIPPED: NO KEY`, `SKIPPED: LAST START FAILED`).

The pieces exist:
- the rows and their getters are settings-ot's (MAXROW grows from 8);
- dialogs are the stock dialog core's store already uses (0x4006d57c, YES/NO
  callback);
- a choice of three is core-ot 0.29's popup lists (`ev_popup`);
- the encoder is taken while the cursor is in MODS (settings-ot).

## 6. Conflict resolution when a mod is enabled

### The rules

One rule set, in one order, in three places:
- the boot link;
- the page's check on each tick;
- `elekloader check` on a computer.

It is the Rytm's (FORMAT.md, "+Drive refusal codes"), so the codes keep
their numbers, plus the Octatrack's from 21. First failure wins.

| code | name | the mod is refused when |
|---|---|---|
| 1-3 | `missing`, `changed`, `too-big` | the file is gone, edited since APPLY, or past the staging buffer |
| 4-7 | `json`, `format`, `target`, `feature` | not a format-2 mod for the Octatrack 1.40C; or it holds what TELOS can't load (DSP sections, `copied` past the image, an early setting) |
| 8 | `duplicate` | its id is the flashed build's or another chosen mod's |
| 9 | `core` | it needs a newer core than the flashed one |
| 10 | `requires` | a required mod is neither flashed nor chosen |
| 11 | `conflicts` | it names, or is named by, a flashed or chosen mod in `conflicts` |
| 12-14 | `export`, `claim`, `import` | an export or a resource name is taken (`menu:root`, `projdata:TAG`, `octabam:part-window@...`, `usb:*`); an import is nobody's |
| 15-16 | `table`, `full` | it adds to a closed table (a boot table), or past a table's spare entries |
| 17 | `room` | past the TELOS area |
| 18-19 | `range`, `limit` | a `pc16` out of range; past the loader's own room |
| 21 | `site` | a site overlaps another chosen mod's site (detail: the other mod and the address) |
| 22 | `flashed` | a site overlaps a flashed site, or writes a kept byte (`Device.keep`, the DELAY bytes) |
| 23 | `early` | a site lies in code that ran once before the load and doesn't run again before use; or a data site in a table read then |
| 24 | `stock` | a site's bytes on the unit are not what the descriptor expects (another loader, a bad build) |
| 25 | `sram` | its battery RAM claims don't fit the TELOS pool |
| 26 | `busy` | quiescence didn't come within 50 ticks (boot only) |

### The order

The order is the Rytm's, so a set gives the same verdict on every unit and
on a computer:

1. Each file on its own, in `active`'s order.
2. Check rounds over the living mods in id order, again until a round
   refuses nothing. Of two that clash (`site`, `claim`, `conflicts`,
   `export`, `duplicate`), the later in id order goes.
3. The layout; past the area, the last mod goes, and the check runs again.

### What the user sees when ticking a mod (the page)

| the check says | the dialog | YES does |
|---|---|---|
| passes | none; the row shows `+ON` | |
| clashes with a ticked mod B (`site`, `claim`, `conflicts`, `export`, `duplicate`) | `CLASHES WITH B` / the reason (`BOTH CHANGE THE SAME CODE`, `BOTH USE PART BYTES`) / `YES: TURN B OFF  NO: CANCEL` | unticks B, ticks this one; any ticked mod that requires B is unticked too, and named first (`ALSO TURNS OFF C`) |
| requires a mod W on the card, not ticked | `NEEDS W` / `YES: TICK BOTH  NO: CANCEL` | ticks both, after checking W the same way (one level) |
| requires a mod not on the card, or flashed-only (`usb`, `intercepts`) | `NEEDS W` / `NOT ON THE CARD` or `FLASH A BUILD WITH IT` | nothing: one button, OK |
| `flashed`, `early`, `feature`, `target`, `core` | `CAN'T LOAD FROM THE CARD` / the reason (`CLASHES WITH THE FLASHED CORE`, `RUNS AT START-UP: FLASH IT`, `HAS DSP CODE`) | nothing |
| `room`, `full`, `sram` | `NO ROOM` / `NEEDS 120K MORE` or `TABLE EV_TICK FULL` / `TURN ANOTHER MOD OFF` | nothing |

- **Nothing is resolved behind the user's back.** The page only ever turns
  a mod off when the user says YES to a dialog that names it. At boot, a
  mod refused by the rules is left out and marked `ERR` with its reason, and
  `active` is not rewritten: the user sees the reason and chooses.
- **Unticking** never asks, except when other ticked mods require the mod:
  `C REQUIRES IT` / `YES: TURN BOTH OFF`.
- **Handler order** between TELOS mods and flashed ones is the entries'
  order, then the id (docs/DRIVE.md, "The boot link", step 5), so a set behaves the same as the
  same mods flashed.
