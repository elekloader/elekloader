# store-check (Analog Four MKII)

A test for [core-a4](../../mods/core-a4)'s mod storage: the Analog Rytm MKII's
[store-check](../store-check-ar2) with the A4's objects. It declares a field
in every scope and does nothing until asked:

| field | scope | bits |
|---|---|---|
| `sc_track` | pattern track | 16 |
| `sc_pattern` | pattern | 32 |
| `sc_sound` | sound | 32: all core-a4 keeps a sound |
| `sc_kit_a`, `sc_kit_b` | kit | 32 each |
| `sc_global_a`, `sc_global_b` | global slot | 32 and 8 |
| `sc_project_a`, `sc_project_b` | project | 32 and 7 |

Selecting STORE CHECK in the settings menu (the GLOBAL key) writes a marker
into every field of every item, through `core_store_set`: the 6 tracks and
the pattern of each of the 128 patterns (not the working copy at the live
project +0x10, as on the Rytm), the 4 sounds of every kit in both of the
A4's kit arrays (the kits as edited and as last saved), every pool sound,
those kits, the four global slots and the project. Each marker depends on
the item and the field, and is never 0.

Seven columns at the top left of the screen then count the markers
`core_store_get` no longer reads back, left to right: pattern tracks,
patterns, kit sounds, pool sounds, kits, globals, project. Each grows a pixel
per bad marker, up to 16; globals have 8 markers and the project 2. To their
right, a bar two pixels longer for each `ev_project` event, a dot when the
last one was a load, and one dot for each scope it dropped (global, track,
pattern, project). A 3x3 square at the right end blinks while the mod runs.

It names the live objects and two firmware routines itself (core exports
none), at their 1.56 addresses.

Checked in an emulator (20 Oct 2026) and on a unit (21 Oct 2026, SC56): see
core-a4's README.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/store-check-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4/out/core-0.3.elemod --mod examples/store-check-a4/out/store-check-1.0.elemod \
    --out a4-store.syx --version SC56 --lockfile a4-store.lock.json
```

Keep `a4-store.lock.json`: a later build made with it reads the projects
this one saved.
