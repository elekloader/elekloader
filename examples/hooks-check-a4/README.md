# hooks-check (Analog Four MKII)

A test for [core-a4](../../mods/core-a4): the Analog Rytm MKII's
[hooks-check](../hooks-check-ar2) (the same `hooks.c` and `label.s`) built
with the A4's `fill_rect` (0x40059f1a) and `std::string` constructor
(0x4018471e), each found by masked matching against the Rytm 1.74's and
checked by its callers. What each bar and square means is in
[hooks-check-ar2's README](../hooks-check-ar2/README.md).

On a unit (OS 1.56, 20 Oct 2026), HC56 passed every check above.

In rytm-emu's A4 build (20 Oct 2026, build HC56,
with an A4 "modified image" fallback added to the emulator): the tick ran
at about 20 Hz and its square blinked, each key press counted twice (down
and up) and each repeat once, the encoder counted every detent the
emulator delivered, the two render counts never differed, and SETTINGS
(GLOBAL) listed HOOK TEST last, after SYSTEM, whose selection counted.
hello-marker-a4 (HM56) differed from stock only in its 3x3 corner on every
screen taken.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/hooks-check-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4/out/core-0.2.elemod --mod examples/hooks-check-a4/out/hooks-check-0.1.elemod \
    --out a4-hooks.syx --version HC56
```
