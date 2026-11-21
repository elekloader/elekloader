# ui-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2) 0.17's UI and system events, the
Rytm's twin of [ui-check-a4mk2](../ui-check-a4mk2): a std1 mod
(`"events": "std1"`) that subscribes to `ev_boot_done`, `ev_idle`,
`ev_crash`, `ev_usb`, `ev_key_leds`, `ev_mode`, `ev_param_set`, `ev_clip`,
`ev_pattern`, `ev_mute` and `ev_view` (`analog-rytm-mk2/ui.h`, `leds.h`).
What it records, in its exported `ui_check`, is in the comment at the top of
`ui.c`. It takes nothing, except while armed (UI TEST selected in SETTINGS;
select it again to disarm): then it takes every mute and every pattern
clear, and sets one LED with `core_led_set`. UI CRASH raises an illegal
instruction in the UI task while armed, so `ev_crash` runs on a real fault;
stock's crash screen follows and the unit needs a power cycle.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/ui-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.18.elemod --mod examples/ui-check-ar2/out/ui-check-0.1.elemod \
    --out ar-ui.syx --version UI74
```

## Not run yet

Built in the cloud with core-ar2 0.17 and linked; not yet run in the Rytm
emulator or on a unit. What the runs must confirm: which modes the four
`MODE_*` bytes show, `ev_usb`'s level on a replug, the LED `core_led_set`
lights and its colour, that `ev_param_set` follows a knob turn, a CC and an
NRPN, each `ev_view` op on a page key and a menu, `ev_clip`'s kinds, and
`ev_crash`'s frame. As on the A4, a build with an `ev_idle` handler may
answer no USB MIDI in the emulator, which feeds USB only at the image's own
self-branches: check `ev_idle` on the unit.
