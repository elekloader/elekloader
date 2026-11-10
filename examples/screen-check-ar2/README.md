# screen-check (Analog Rytm MKII, core-ar2 0.13)

A test for core-ar2 0.13's screen calls (`screen.c`, docs/ADAPTING.md
"Screen calls (core-ar2 0.13, Analog Rytm MKII)"), on screen. PLAYMODE
(taken) steps through four modes; `check.c`'s header says what each draws.

| mode | what it shows |
|---|---|
| 0, off | stock, untouched |
| 1, FONTS | each of stock's 17 fonts by `core_text_draw` (RIGHT steps the font), with its width, height and descent, the label drawn through the mod's own bitmap (`core_bitmap_init`) |
| 2, CALLS | a bottom bar with `core_stock_overlay`, `core_level_readout`, the key events held and the progress box: FLTR's events handed to stock through `core_stock_key` a second later, QPER opening a progress box that fills and closes, NO closing it at once |
| 3, COVER | CALLS over a checkerboard that keeps the TRK LEVEL readout's field while it runs (`core_level_field_save`, `_put`) |

It is a test, not for a project you keep.

```bash
python -m elekloader.sdk.build examples/screen-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
```
