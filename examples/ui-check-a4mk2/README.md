# ui-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's UI and system events:
a std1 mod (`"events": "std1"`) that subscribes to `ev_boot_done`,
`ev_idle`, `ev_crash`, `ev_usb`, `ev_key_leds`, `ev_mode`,
`ev_param_set`, `ev_clip`, `ev_pattern` and `ev_mute`, and contributes to
core's own `ev_knob_turn` table (`analog-four-mk2/ui.h`). What it records,
in its exported `ui_check`, is in the comment at the top of `ui.c`. It takes
nothing, except while armed (UI TEST selected in SETTINGS; select it again
to disarm): then it takes encoder B's turns and every mute, and lights LED
0x20 with `core_led_set`.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/ui-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/ui-check-a4mk2/out/ui-check-0.1.elemod \
    --out a4-ui.syx --version UI56
```
