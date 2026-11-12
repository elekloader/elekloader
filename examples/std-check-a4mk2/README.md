# std-check (Analog Four MKII)

A test for [core-a4mk2](../../mods/core-a4mk2) 0.7's standard events: a
std1 mod (`"events": "std1"`, docs/ADAPTING.md, "Standard events (std1)")
that subscribes to the eight events core maps, `ev_boot`, `ev_tick`,
`ev_draw`, `ev_key`, `ev_settings`, `ev_project`, `ev_render_in` and
`ev_param_turn`, and uses `core_redraw`, `core_post` and `core_timer`. What
it records, in its exported `std_check` and as bars on screen, is in the
comment at the top of `std.c`. It takes nothing, except that while armed
(STD TEST selected in SETTINGS; select it again to disarm) it takes every
turn of encoder A on a parameter page.

Its SETTINGS row uses core's own `core_additem`: std1's `core_setting_add`
is not on the A4 yet. Not run yet in an emulator or on a unit.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/std-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/std-check-a4mk2/out/std-check-0.1.elemod \
    --out a4-std.syx --version SD56
```
