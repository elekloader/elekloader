# lock-check (Analog Rytm MKII)

A test for [core-ar2](../../mods/core-ar2)'s lockable parameters (core 0.8), from 1.1 their p-locks, from 1.2 their scene and performance locks, from 1.3 a value above 127, from 1.4 a lockable parameter that is not an LFO destination, from 1.5 a category of its own, from 1.6 core's cut of strings longer than it shows (core 0.8), from 1.7 the smoothed value (core 0.9), from 1.10 a field's init (core 0.10), and from 1.11 the batch read (core 0.10).
It declares three sound fields with `"lock": true` and registers them in
`core_lock_params` (1.6: four); `lc_a`, `lc_c` and `lc_d` are also `"lfo": true`:

| field | bits | code (from a new lockfile) | CC (`"cc": true`, from a new lockfile) | DST menu (`"lfo": true`) | DST value | range |
|---|---|---|---|---|---|---|
| `lc_a` | 7 | 42 | 8 | MOD:Lock Check A (core's category) | LCA | 0..127, page knob id 1 |
| `lc_b` | 4 | 43 | none | not listed | ERR (NRPN 1:35 only) | 0..15, `"smooth": false` (1.7) |
| `lc_c` | 10 | 44 | 9 | LCHK:Lock Check C (its own) | LCC | 0..1000, page knob id 2 |
| `lc_d` (1.6) | 1 | 45 | none | LCHK:Lock Check D, a name longer… (cut to 4, 32 and 4 characters) | LCDX (a 200-character short name, cut) | 0..1 |
| `lc_k` (1.9, kit) | 7 | 46 | 48 | the FX LFO's: LCHK:Lock Check K | LCK | 0..127, FX page knob id 3 |
| `lc_n` (1.10) | 7 | 47 | 49 | LCHK:Lock Check N | LCN | 0..127, `"init": 100`, page knob C, id 4 |

A bar along the bottom edge shows the offset track 1's LFO gives A or C
(`core_lfo_offset`), from the middle, a pixel per stock knob step; a small
square at the left end shows which (top A, bottom C). Behind the LFO key a
LOCK CHECK page shows `lc_a` of track 1's sound in the current kit on knob A
(borrowing the dead record id 1), so RANDOMIZE PAGE ([LFO]+[YES]) and
RELOAD PAGE ([LFO]+[NO]) can be tried on it. From 1.1, knob A
with a trig held sets that trig's p-lock of `lc_a`, and a second bar just
above the first shows the lock of the trig track 1 plays
(`core_lock_value`), half a pixel per step from the left, and is empty
when that trig has no lock of `lc_a`. From 1.2, knob A with a scene's pad
held in scene edit sets the scene's lock of `lc_a`, and with a
performance's pad held in performance edit its depth; a third bar above
the second shows `lc_a` as the active scene and the performances leave it
(`core_scene_value`, else the sound's value, plus `core_perf_offset`), half
a pixel per step, and is empty while neither touches it. From 1.3, knob B
shows `lc_c`, 0..1000 (borrowing the dead record id 2), with its value as a
number in the pop-up (the knob's ring draws a 0..127 knob's); its p-locks,
scene locks, performance depths and MIDI follow core's rules for a value
above 127 (two rows, coarse steps of 8, a 14-bit NRPN). From 1.6 the
second and third bars show C as well, in the right half from x 64, a pixel
per 16 steps: the lock of the trig track 1 plays, and C as the active scene
and the performances leave it.

From 1.7 (core 0.9) every audio block's `ev_render_out` records, for
track 1, `core_param_value` of A, B and C, A's and C's values in the
sound, A's LFO and performance offsets, and A's p-lock and scene lock, in
a ring of 512 blocks (`lc_ring`: the magic `SMCK`, the next index, the
three reads' cost in DMA timer 0's ticks, most and sum, and the blocks
counted; then the records). An emulator snapshot finds it with
`dtpeek SNAP find 534d434b`. Nothing on the screen shows it.

From 1.8 A and C are `"mod": true`: they end every sound's VELOCITY MOD
and AFTERTOUCH lists. From 1.9 (core 0.9) a kit field, `lc_k`, is the FX
track's parameter: behind the FX track's LFO key a LOCK CHECK FX page
shows it on knob A (id 3), it ends the FX LFO's DST list, and NRPN 4:4 or
CC 48 on the FX track's channel sets it. Each ring record then also holds
`core_param_value(12, K)`, K in the kit, the FX LFO's offset to K and its
p-lock, scene lock and performance offset (44-byte records).

From 1.10 (core 0.10) a sound field, `lc_n`, is declared `"init": 100`, so
it reads 100 in a new project and in a sound saved without it: knob C of
the LOCK CHECK page shows it (id 4). Each audio block also writes N's
state on track 1 to `lc_init`, after the magic `INIT`: the block, then
`core_param_value(0, N)`, N in the sound, the LFO's offset to N, and N's
p-lock and scene lock (16-bit, -1 for none).

From 1.11 each audio block also reads A, B and C again in one
`core_param_values` call by their handles (`core_param_handle`) and
writes to `lc_batch`, after the magic `BTCH`: that call's cost in DMA timer
0's ticks, the most and the sum, the blocks, and the blocks where any of
the three differed from its own `core_param_value` read in the same block
(0 when the batch read gives the same values).

The screen's y counts from the bottom row (y 0 is the bottom edge). 1.0
drew its bars at y 57..63, which is the top edge, over the stock header's
top rows; 1.0.1 and later draw them from y 0 (1.2's third bar at y 8..9).

```bash
python -m elekloader.sdk.build mods/core-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.sdk.build examples/lock-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
python -m elekloader.patch --stock Analog-Rytm_MKII_OS1.74.syx \
    --mod mods/core-ar2/out/core-0.10.elemod --mod examples/lock-check-ar2/out/lock-check-1.10.elemod \
    --out rytm-lock.syx --version LK10
```

Core labels the knobs with the parameters' short names, LCA and LCC, in place
of stock's labels for ids 1 and 2 (ERR).

From 1.12 (core 0.11) knob D of LOCK CHECK is B (id 5, LCB), a 4-bit field
shown on the dial's 0..127: its get answers round(v * 127 / 15) and its
`core_param_scales` entry asks for core's linear map, so with a trig or a
scene's pad held, on NRPN 4/1 and in its text, B agrees with the knob
(mods/core-ar2/README.md, "Dial scale (0.11)").

From 1.13 `lc_render` runs every 8th audio block (`LC_STRIDE`), not every
block: `lc_ring`, `lc_init` and `lc_batch` get a record every 8th block,
each with its block number, so the ring holds 512 records over 4096 blocks.
On the unit the per-block reads took 59 us of the 667 us block, 8.8 % of
the CPU, which with core 0.11 left the UI task no time (remote keys
expired, CPU 100 %).
