# hooks-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2): it subscribes to every event the
core provides, takes nothing, and shows what it counts. The unit should behave
exactly as stock apart from the drawing.

| what | where on screen | moves when |
|---|---|---|
| `ev_tick` | 3x3 square, top right | blinks about once a second, all the time |
| `ev_key` | bar at y 60-61 (the top) | any key: press, repeat and release each add one |
| `ev_enc` | bar at y 56-57 | any encoder detent |
| `ev_settings` | the HOOK TEST row in SETTINGS, and the bar at y 52-53 | the row is listed; selecting it adds one |
| `ev_render_in` | bar at y 48-49 | always: one pixel per 64 audio blocks, across in about 5.5 s, then it wraps |
| `ev_render_out` | a second square, below the first | never: it appears only if the two render events ever differ by more than one block |

Each bar is its count mod 128. The tick handler also asks for a redraw every
8 ticks, so the bars move with nothing else on screen changing.

The SETTINGS row uses the firmware's MenuItem with a label callback in
assembly (`label.s`, the Rytm's own label routine with other text) and
callbacks that do nothing for draw and change. That convention was the one
part not checked against the Digitakt's; on a unit (11 Oct 2026, build HC01)
the row was listed after SYSTEM, its selections counted, and every bar
moved as the table says.

```bash
export ELEKLOADER_CROSS=m68k-elf-
python -m elekloader.sdk.build examples/hooks-check-ar2 --stock Analog-Rytm_MKII_OS1.73.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.73.syx \
    --mod mods/core-ar2/out/core-0.1.elemod --mod examples/hooks-check-ar2/out/hooks-check-0.1.elemod \
    --out rytm-hooks.syx --version HC01
```
