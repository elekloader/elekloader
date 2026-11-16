# liverec-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2)'s `ev_live_rec` and
`core_mutes`. Live recording's three pattern writes each ask `ev_live_rec`
first (ui.s, ui.c): a played note as a trig (0x400a954a, kind 1), the note,
velocity or length into the step's trig (0x400a6a08, kind 2) and a live
p-lock (0x400a690c, kind 3). A handler that returns nonzero takes the
write, and stock writes nothing.

This mod takes every call on track 2 and lets the others through, so live
recording on track 2 records nothing while tracks 1 and 3..6 record as stock.
It counts each call by track and kind, and each tick it records
`core_mutes`, how often it changed, and which steps of pattern A01 hold a
note trig on tracks 1 and 2 (`core_step_trig`). All of it is in the exported
`liverec_check` (the layout is in the comment at the top of `liverec.c`),
for an emulator's or a unit's memory read, with the magic `LRA4` once an
event ran. Nothing else changes.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/liverec-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.8.elemod --mod examples/liverec-check-a4mk2/out/liverec-check-0.1.elemod \
    --out a4-liverec.syx --version LR56
```
