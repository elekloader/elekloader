# adopt-check (Analog Rytm MKII)

A probe for [core-ar2](../../mods/core-ar2)'s `ev_store_adopt` (core 0.20,
OS 1.74). It counts the sounds and kits core hands it, and stands in for a
mod whose older build kept its data in the stored item's first spare bytes:
a nonzero stored sound +0x70..+0x72 becomes its sound field `ad_sound` (24
bits), and a nonzero stored kit +0x816..+0x819 its kit field `ad_kit` (32
bits). Any layout keeps its first 24 sound bits and first 32 kit bits there,
so a project saved by [store-check](../store-check-ar2) (`sc_sound`,
`sc_kit_a`) shows up here.

Along the bottom edge of the screen (y counts from the bottom row), each row
two pixels high, from the left edge:

| y | what |
|---|---|
| 0..1 | sounds handed over with no layout ID, two pixels each (wraps at 32) |
| 4..5 | sounds handed over with another layout's ID, the same |
| 8..9 | kits handed over with no layout ID, the same |
| 12..13 | kits handed over with another layout's ID, the same |
| 16..17 | a dot at x 0 when the current sound's `ad_sound` is not 0, one at x 4 when the current kit's `ad_kit` is not 0 |

A check: build core 0.20 with store-check, run STORE CHECK and save the
project. Build core 0.20 with adopt-check against a lockfile of its own (so
its layout has no ancestor in common with the first), flash it and load
that project: the bars for another layout's ID grow and both dots show.
Save, then load the project again: the bars stay as they were, since the
saved sounds and kits now carry this build's ID.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/adopt-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx --mod core-0.20.elemod --mod adopt-check-1.0.elemod --lockfile adopt.lock.json --out adopt-check.syx --version 1.74
```
