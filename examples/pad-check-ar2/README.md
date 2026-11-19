# pad-check (Analog Rytm MKII, core-ar2 0.15)

A test for core-ar2's pad strikes before stock (`ev_pad`) and LEDs from mods
(`ev_leds`, `core_led`) (`pads.c`, docs/ADAPTING.md "Pads and LEDs
(core-ar2 0.15, Analog Rytm MKII)"), on screen. `check.c`'s header says what it
does.

| pad | what the mod answers | what you see |
|---|---|---|
| BD | takes every strike | no sound, no note, the track stays where it was |
| RS | 0 to the strike, 1 to the release | stock plays it and keeps its release |
| the others | 0 | stock |

After an odd number of BD strikes the BD pad shows colour 15 and trig key 1
colour 47, whatever stock does with them; after an even number both are
stock's again. A bottom bar shows each pad's strike, release and pressure
counts, the current track, the last strike's flags and velocity, and
`core_pads_taken`.

It is a test, not for a project you keep.

```bash
python -m elekloader.sdk.build examples/pad-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
```
