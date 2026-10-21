# store-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2)'s mod storage (core 0.3). From
1.1 it is for OS 1.74 only, as core 0.3 is; 1.0, with core 0.2, was for 1.73
and 1.74.
It declares a field in every scope and does nothing until asked:

| field | scope | bits |
|---|---|---|
| `sc_track` | pattern track | 16 |
| `sc_pattern` | pattern | 32 |
| `sc_sound`, `sc_sound_b` | sound | 24 and 32: the 56 bits core 0.3 keeps a sound |
| `sc_kit_a`, `sc_kit_b` | kit | 32 each |
| `sc_global_a`, `sc_global_b` | global slot | 32 and 8 |
| `sc_project_a`, `sc_project_b` | project | 32 and 7 |

Selecting STORE CHECK in SETTINGS writes a marker into every field of every
item, through `core_store_set`: the 13 tracks and the pattern of each of the
128 patterns (not the working pattern, which the stock OS rebuilds from the
current one), the sounds of every kit and of the pool, every kit, the four
global slots and the project. Each marker depends on the item and the
field, and is never 0.

Seven columns at the bottom left then count the markers `core_store_get` no
longer reads back, left to right: pattern tracks, patterns, kit sounds, pool
sounds, kits, globals, project. Each grows a pixel per bad marker, up to 16;
globals have only 8 markers (4 slots x 2 fields) and the project 2, so those
two columns are full at 8 and 2 pixels.
Empty columns after a save, a power cycle or loading the project back mean
everything survived; full columns after loading another project, or with a
build whose layout can't read this one, mean the data was dropped to zeros
as it should be. A 3x3 square at the right end of the same edge blinks while the mod runs.

It also counts `ev_project`: right of the columns, a bar two pixels longer
for each project event (it wraps at 16), and beside it a dot when the last
event was a load (none for CREATE NEW), then one dot for each scope that
load dropped: global, track, pattern, project.

It names the live objects and two firmware routines itself (core exports
none), at their 1.74 addresses (`mod.json`'s `os`; 1.0 had 1.73 as its `os`
and 1.74 in `ports`, as hooks-check-ar2 has).

Checked in an emulator and on a unit, on 1.73 and 1.74 (19 Oct 2026; 1.0,
with core 0.2), and 1.1 with core 0.3 in an emulator (20 Oct 2026): see
core-ar2's README. 1.1 adds `sc_sound_b` and needs core 0.3.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/store-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.3.elemod --mod examples/store-check-ar2/out/store-check-1.1.elemod \
    --out rytm-store.syx --version SC01 --lockfile rytm-store.lock.json
```

Keep `rytm-store.lock.json`: a later build made with it reads the projects
this one saved. The same mods built without it get the same layout, so they
read them too. For a build that can't, give it an edited copy of the
lockfile, with another `id` and no `ancestors`.
