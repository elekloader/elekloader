# frame-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's `ev_analog_frame`
(`analog-four-mk2/ui.h`): does a handler's write reach the analog board?
While armed (FRAME TEST selected in SETTINGS; select it again to disarm) it
copies voice 2's 13 frame words over voice 1's every block, so with trigs
on track 1 only, track 1 should fall silent. Every block it also reads back
the frame stock packed the block before and compares voice 1's words with
what it left there. What it records, in its exported `frame_check`, is in
the comment at the top of `frame.c`. On a unit (stock 1.56 base), track 1
went silent while armed and came back when disarmed, with every packed
frame matching what the mod left.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/frame-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/frame-check-a4mk2/out/frame-check-0.2.elemod \
    --out a4-frame.syx --version FC56
```
