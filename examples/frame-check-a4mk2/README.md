# frame-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's `ev_analog_frame`
(`analog-four-mk2/ui.h`): does a value a handler chooses, written into the
analog frame, do on the analog board what it should?

While armed (FRAME TEST selected in SETTINGS; select it again to disarm) it
raises voice 1's two oscillators one octave. For each oscillator it doubles
stock's pitch state for the block and writes the frame word stock's own
formula gives for it, with that oscillator's calibration. Every block it
first rebuilds stock's two words with the same formula and only writes when
they match, so with trigs on track 1, track 1 should sound the same one
octave up. Every block it also reads back the frame stock packed the block
before and compares voice 1's words with what it left there. What it
records, in its exported `frame_check`, is in the comment at the top of
`frame.c`.

0.2 copied voice 2's words over voice 1's (track 1 fell silent: KB T293).

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/frame-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/frame-check-a4mk2/out/frame-check-0.3.elemod \
    --out a4-frame.syx --version FC56
```
