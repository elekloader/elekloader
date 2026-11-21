# std-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2) 0.17's standard events, the
Rytm's twin of [std-check-a4mk2](../std-check-a4mk2): a std1 mod
(`"events": "std1"`, docs/ADAPTING.md, "Standard events (std1)") that
subscribes to the eleven events core maps, `ev_boot`, `ev_tick`, `ev_draw`,
`ev_key`, `ev_enc`, `ev_settings`, `ev_project`, `ev_render_in`,
`ev_param_turn`, `ev_sysex_in` and `ev_seq_tick`, and uses `core_redraw`,
`core_post`, `core_timer`, `core_setting_add` and the draw helpers. What it
records, in its exported `std_check` and as bars on screen, is in the comment
at the top of `std.c`. It takes nothing, except that while armed (STD TEST
selected in SETTINGS; select it again to disarm) it takes every turn of
encoder A on a parameter page (`ev_param_turn`) and every turn of encoder H
(`ev_enc`).

## DRAW TEST

Selected (and again to stop), every frame draws the A4 check's pattern over
the right half, in glass coordinates (x 0 the left column, y 0 the top row);
the table is in [std-check-a4mk2's README](../std-check-a4mk2/README.md#draw-test),
with "AR" in place of "A4" in step 6. `std_check` counts the frames drawn so
(`drawn`) and the row's selects (`draw_selects`), and keeps the four widths
`core_text` returned (`w_small`, `w_medium`, `w_large`, `w_centre`), each of
which should equal `core_text_width`'s for that font and text.

## Not run yet

Built in the cloud with core-ar2 0.17 and linked; not yet run in the Rytm
emulator or on a unit. What the runs must confirm: the key flags (std.c's
`AR2_KEY_*`, a tap a press and a `KEY_RELEASE`, a hold `KEY_REPEAT`), the
encoder numbers and `ev_enc`'s delta, `ev_sysex_in`'s port for a DIN and a
USB message, and the DRAW TEST pattern exact.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/std-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.17.elemod --mod examples/std-check-ar2/out/std-check-0.1.elemod \
    --out ar2-std.syx --version SD74
```
