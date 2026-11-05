# save-check (Analog Rytm MKII)

A probe for [core-ar2](../../mods/core-ar2)'s `ev_project_save` (core 0.10,
OS 1.74). It keeps one project field, `sv_saves` (16 bits), and adds 1 to it
in every call before a save, so a saved copy carries the number of saves
made before it.

Along the bottom edge of the screen (y counts from the bottom row), each row
two pixels high, from the left edge:

| y | what |
|---|---|
| 0..1 | the calls before a save so far, two pixels each (wraps at 32) |
| 4..5 | the calls after a save so far, the same |
| 8..9 | `sv_saves` as the project holds it now, the same |
| 12..13 | the last call's `how`: one to three dots for its kind (SAME, OTHER, NEW), then from x 15 the slot's eight bits, most significant first, a dot for each 1, three pixels apart; a dot at x 45 when it failed, and one at x 120 when the last call was the one before |

So SAVE PROJECT AS to another slot makes the first three bars a step
longer; loading the slot it was saved from then shows `sv_saves` one step
shorter, the count that slot was saved with.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/save-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx --mod core-0.10.elemod --mod save-check-1.0.elemod --out save-check.syx --version 1.74
```
