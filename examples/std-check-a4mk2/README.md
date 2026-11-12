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

From 0.2 its two SETTINGS rows, STD TEST and DRAW TEST, are std1's
`core_setting_add`'s, and everything it draws goes through std1's draw
helpers (rows counted from the top), so its bars are where 0.1 drew them.

## DRAW TEST

Selected (and again to stop), every frame draws this over the right half,
in glass coordinates (x 0 the left column, y 0 the top row), in this order:

| step | call | pixels |
|---|---|---|
| 1 | `core_fill(64, 24, 64, 40, 0)` | x 64..127, y 24..63 cleared |
| 2 | `core_fill(66, 26, 10, 4, 1)` | x 66..75, y 26..29 set |
| 3 | `core_blit(80, 26, 8, 8, ...)` | x 80..87, y 26..33: the box's edge and the diagonal x - 80 = y - 26 set, the rest cleared |
| 4 | `core_text(66, 36, TEXT_SMALL \| TEXT_LEFT, "STD1")` | font 16 from x 66, top row 36 |
| 5 | `core_text(126, 36, TEXT_MEDIUM \| TEXT_RIGHT \| TEXT_INVERT, "STD1")` | font 1 ending at x 126, top row 36, light on a set box |
| 6 | `core_text(66, 46, TEXT_LARGE \| TEXT_LEFT, "A4")` | font 6 from x 66, top row 46 |
| 6b | `core_text(104, 56, TEXT_SMALL \| TEXT_CENTRE, "STD1")` | font 16 from x 104 - w / 2, top row 56 |
| 7 | `core_px` at (64, 24), (127, 24), (64, 63), (127, 63) | the four corners set |
| 8 | `core_px(128, 30, 1)` | nothing: off the frame |
| 9 | `core_fill(124, 60, 8, 8, 1)` | x 124..127, y 60..63 set: clipped |

`std_check` counts the frames drawn so (`drawn`) and the row's selects
(`draw_selects`), and keeps the four widths `core_text` returned
(`w_small`, `w_medium`, `w_large`, `w_centre`); each should equal `core_text_width`'s
for that font and text (in core-a4mk2 0.6's screen-check, font 16's
widths come from the same routine). Steps 1-3 and 7-9 are exact pixels; the
text's glyphs are stock's, so only their boxes are checked.

In the A4 emulator (OS 1.56, 12 Nov 2026), with analoghealth for the remote
keys and memory reads, every event fired: `ev_boot` once, the tick about
every 8th tick for `core_timer` (mask 8), one post per 1024 audio blocks
with no refusal, key taps as a press and a `KEY_RELEASE` (0x10), a hold
with `KEY_REPEAT`, `ev_settings` once at boot, a project load, and turns of
32 counts or more on encoder A (smaller ones make no step and no call),
taken while armed with the value unchanged. On a unit (OS 1.56, 12 Nov
2026, SH56) the same plan passed with the same values; CPU about 73 %
with analoghealth, the heap flat.

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/std-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4mk2/out/core-0.7.elemod --mod examples/std-check-a4mk2/out/std-check-0.2.elemod \
    --out a4-std.syx --version SD56
```
