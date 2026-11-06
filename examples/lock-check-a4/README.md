# lock-check (Analog Four MKII)

A test for [core-a4](../../mods/core-a4)'s lockable parameters and their
p-locks (core 0.5), and of their destinations (core 0.5, part 2: A, K and
V are `"mod": true`, so every modulation source of their track can target
them). It declares one lockable field of each track kind and a fine one,
and registers them in `core_lock_params`:

| field | scope | kind | code (from a new lockfile) | range | page knob | MIDI (1.2) |
|---|---|---|---|---|---|---|
| `lk_a` | sound | synth | 0x70 | 0..127 | id 0x200, LOCK CHECK (AMP key, synth tracks), knob A | NRPN 0:112, CC 12 |
| `lk_c` | sound | synth | 0x71 | 0..1000 | id 0x201, LOCK CHECK, knob B | NRPN 0:113 (14 bits), no CC |
| `lk_k` | kit | FX | 0x70 | 0..127 | id 0x202, LOCK CHECK FX (LFO key three times, FX track), knob A | NRPN 0:112, no CC |
| `lk_v` | kit, `"track": "cv"` | CV | 0x70 | 0..127 | id 0x203, LOCK CHECK CV (LFO key three times, CV track), knob A | NRPN 0:112, CC 35 |

The knobs show the current track's sound of the current kit (the FX and CV
pages the kit's field). With a trig held a knob sets that trig's p-lock and
shows it; in live recording a turn locks the step that plays.

Every audio block (`ev_render_out`) it writes `lk_seen`, for the emulator:

| offset | what |
|---|---|
| 0 | `LKCK` |
| 4 | the count of blocks |
| 8, 20, 32 | for voices 0..5 (16 bits each): the playing lock of A, of C, and C's slide delta |
| 44 | `core_lock_value` of A, C, K and V on tracks 1, 1, 5 and 6 (16 bits each, -1 none) |
| 52 | for A, C, K and V: the blocks a lock was seen in (32 bits each) |
| 68 | for A, C, K and V: the last lock seen (16 bits each, -1 none) |
| 76, 82 | for voices 0..5: `core_dst_voice`'s route and Dest counts (8 bits each) |
| 88 | for voices 0..5: the first modulation noted, `struct core_dst` (6 bytes each) |
| 124, 126 | `core_dst_macros`' count and its first target |
| 132 | voice 0's eight Dest selectors' engine target slots (stock's base words, 16 bits each) |

Two bars along the bottom of the screen show track 1's lock of A (half a
pixel a step) and of C (a pixel per 8), none while its trig has none.

From 1.1 it checks core-a4's value read (part 3). Each block, after core's
snapshot, `lk_ring` (2,048 entries of two 16-bit words, `lk_ring_n` the
count) keeps core's read of A (K on voice 4, V on voice 5) in the engine's
scale (`CORE_PARAM_ENGINE`) beside stock's engine copy (0x80005af0) of the
stock parameter `lk_cfg.index` on the same voice, at its slot by stock's
map. `lk_cfg` (magic LKCF) is poked to set a run up from the UI tick:

| offset | what |
|---|---|
| 4 | the stock parameter's index (16 bits) |
| 6 | 1: push the voice's sound (or the FX, CV block) as a trig does; 2: set A and the stock parameter by the knob push, as a knob turn |
| 8, 10 | at the push, A's value and the stock parameter's word (-1: none) |
| 12 | 1: lock A and the stock parameter with the same values on 48 of track 1's 64 steps |
| 13 | n + 1: push macro n |
| 14 | the voice compared (0..5) |

`lk_cost` (magic LKCT) has the read's cost in DMA timer ticks (one
parameter, four in a batch: min, max, sum, count), the block's length, the
batch's values against the single read, the voice each track reads
(`core_param_voice`) and track 1's read of A.

```bash
python -m elekloader.sdk.build mods/core-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/lock-check-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.lint examples/lock-check-a4/out/lock-check-1.1.elemod \
    --stock Analog-Four_MKII_OS1.56.syx --with mods/core-a4/out/core-0.5.elemod
```

From 1.2 (core 0.5, part 4, MIDI) A and V declare `"cc": true`, and each UI
tick `lk_midi` keeps the four fields as stored: after the magic `LKMI` and a
count of ticks, A and C in tracks 1..4's sounds of the current kit (16 bits
each), then K and V in the kit.
