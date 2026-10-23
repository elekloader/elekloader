# Settings for the Octatrack

One place on the unit for every mod's settings (Octatrack MKI and MKII, OS
1.40C). The companion adds a fifth category, MODS, to the MAIN MENU, beside
PROJECT, SYSTEM, CONTROL and MIDI. It needs the Octatrack's core 0.6
(`mods/core-ot`).

- **The menu.** MODS lists each mod's settings under the mod's name, with
  the value at the right of the row. YES steps a value: OFF/ON, the next
  option, or the next number, wrapping. An encoder turns it.
- **Keeping the values.** Card settings are kept on the card for every
  project, through core's card data (`/ELEKLOADER/card.work`, the `SETC`
  block). Project settings are kept with the project
  (`<project>/elekloader.work`, `SETP`). Each value is stored by its mod's
  tag and the setting's key, so it survives a build that leaves the mod
  out, or puts the settings in another order.
- **Early settings** are also kept in battery RAM (`SETE`) and set at
  `ev_boot_done`, before the card is read.
- **Without this companion** a mod's settings stay at their defaults: the
  table is core's, so a mod builds the same with or without it.

## Declaring a setting

In C, with `octatrack/core.h`:

```c
volatile int32_t my_mode = 1;                       /* the default; the mod reads it */
static const char *const my_modes[] = { "OFF", "PRE", "POST" };
const struct ot_setting my_mode_setting = {
    OT_SETTING_ABI, OT_TAG('M', 'I', 'N', 'E'), 1,  /* tag + key: the identity on the card */
    OT_SET_OPTION, OT_SET_CARD,                     /* kind, scope (| OT_SET_EARLY) */
    "MINE", "TRACKS",                               /* the heading, the row's label */
    0, 2, my_modes,                                 /* min, max, labels */
    &my_mode, 1, 0 };                               /* the variable, its default, changed() */
```

```json
"contribute": [{"to": "ot_settings", "order": 50, "data": "00000000",
                "relocs": [[0, "abs32", "sym:my_mode_setting", 0]]}]
```

- **Kinds.** `OT_SET_BINARY` (OFF/ON), `OT_SET_OPTION` (an index into
  `labels`, 0 to max) and `OT_SET_NUMBER` (min to max).
- **Keys.** Never reuse a key for another meaning.
- **Option labels.** Append new ones at the end. Keep them to about four
  characters: the value column is narrow.
- **Row labels** are the menu's: about eleven characters fit beside a
  value.
- **`changed(s)`**, when given, runs after an edit (in the UI task) and
  after a load (in the engine task).
- **Order.** Settings show in the table's order, under a heading wherever
  the `group` changes, so give a mod's settings neighbouring orders.

## How it works

- **The menu's sites.** The MAIN MENU root's rows pointer (0x400cbda4) is
  pointed at the companion's five rows: the four stock ones, copied at
  `ev_boot_done`, then MODS. Its row count (0x400cbd8c) goes from 4 to 5.
  The root window is five rows tall in stock, so the engine draws the fifth
  row as it draws the others. These are octabam's facts (MIT,
  docs/firmware/MAINMENU.md); octalab's fifth category ran on an MKI. The
  code is ours.
- **The rows.** MODS' list is the companion's own descriptor, set up at
  boot. A heading is a row with no action; a setting's row runs the
  companion's action on YES. Its value comes from the row's getter, which
  the menu calls when it draws the right column.
- **The encoder.** It is taken (`ev_enc`) only while the MAIN MENU is open
  and its cursor is in MODS.
- **Conflicts.** Claimed as `menu:root`: another mod that adds a root
  category is refused beside it.

## Checked

In octabam's emulator (`ot_emu --mkii`), with three settings (a card
binary, a card option and a project number):
- PROJ, DOWN ×4 and RIGHT opened MODS. YES turned FLAG OFF; YES then an
  encoder turned MODE to its last option, FAST (clamped); an encoder took
  LEVEL from 64 to 54.
- 2 s later `card.work` held FLAG and MODE and the project's
  `elekloader.work` held LEVEL. The next boot set all three back from them.
- With no setting in the build, MODS shows NO SETTINGS, and YES or an
  encoder turn there does nothing.
