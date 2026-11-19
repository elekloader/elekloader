# modset-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's `ev_mod_set`
(`analog-four-mk2/ui.h`): it contributes to core's `ev_mod_set` table and
records every modulation setup edit core reports (destination or depth, on
the VELOCITY, MOD WHEEL, PITCH BEND, BREATH, AFTERTOUCH and PERF setups) in
its exported `ms_check`; the fields are in the comment at the top of
`modset.c`. It takes nothing, except while armed (MS TEST selected in
SETTINGS; select it again to disarm): then it takes every depth change of
slot 4 (knob J), so that depth stays.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/modset-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/modset-check-a4mk2/out/modset-check-0.1.elemod \
    --out a4-modset.syx --version MS56
```
