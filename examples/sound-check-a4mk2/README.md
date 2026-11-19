# sound-check (Analog Four MKII)

A test tone for [core-a4mk2](../../mods/core-a4mk2) 0.5's mod parameters, so that
each modulation source can be heard on a mod parameter beside a stock
parameter set up the same way. [lock-check-a4mk2](../lock-check-a4mk2) stays the
data probe; this one makes sound.

It declares two lockable `"mod": true` fields of the synth tracks and
registers them in `core_lock_params`:

| field | scope | range | what it does | page knob | MIDI (lockfile beside lock-check 1.2) |
|---|---|---|---|---|---|
| `sc_lvl` (LVL) | sound | 0..127 | the tone's level; 0, a new sound's value, is silent; 127 is about -12 dBFS | id 0x210, SOUND CHECK knob A | NRPN 0:114, CC 13 |
| `sc_pit` (PIT) | sound | 0..48 | the tone's pitch, semitones above A2 (110 Hz): four octaves | id 0x211, SOUND CHECK knob B | NRPN 0:115, no CC |

The codes and the CC come from the build's lockfile, so a build with other
mods may number them differently: the NRPN is MSB 0, LSB the field's code.

SOUND CHECK is behind the AMP key on a synth track, after stock's AMP page
(and after LOCK CHECK when lock-check is in the build). The knobs show the
current track's sound of the current kit. With a trig held a knob sets that
trig's p-lock; in live recording a turn locks the step that plays. Both are
in every route, Dest and macro list of the synth tracks as LVL and PIT
(category SCHK).

## What it plays

Every audio block (`ev_render_out`, order 60) it reads LVL and PIT on each
synth voice (0..3) with one `core_param_values_voice` batch, so a voice's
p-locks, Param Slide, routes (velocity, bend, wheel, breath, aftertouch),
the performance macros, CONTROL IN and LFO1, LFO2, EnvF and Env2 Dest A and
B move the tone as they move a stock parameter of that voice. Each voice
whose LVL is above 0 plays a sine at its PIT; LVL ramps over the block. A
poly track's voices each play their own tone.

The tone does not follow the notes: it sounds while LVL is above 0, playing
or stopped. What moves it is the modulation.

## Where the sound goes

The A4's voices are analog; the audio interrupt (vector 191, 0x401013be)
runs the send effects. Its DAC block is one half of the TX ring at
0x80002000 (two halves of 32 frames, 8 words a frame, 48 kHz). At its entry
stock takes the half eDMA channel 54 is not reading (its source address,
0xfc0456c0, below 0x80002400 means the second half), and 0x400fed12 writes
the effects' return into it as words 4 (left) and 5 (right), signed 24-bit,
clamped. The outputs add the return to the voices' dry mix
(analog-emu's docs/A4-PORT.md, section 12).

`ev_render_in` notes the half as stock's entry finds it, and `ev_render_out`,
after stock has written the block, adds the tone to its words 4 and 5, with
the same clamp. So the tone is in the main outputs as the effects' return
is: the FX track's levels do not change it, the main volume does.

## For the emulator

`sc_seen` (magic `SNCK`) keeps the count of blocks, the TX half last
written, and each voice's LVL and PIT as read, in 8.8 steps (-1 for none),
32 bits each.

## Checked in the emulator (analog-emu, `dtboot --analog-wav`, 2026-11-05)

Build: core-a4mk2 0.5 + lock-check 1.2 + sound-check 1.0 on stock 1.56, a new
lockfile. Each run starts from the cold boot's snapshot, sets LVL and PIT
by NRPN on channel 1 and records 3 to 4.5 s of the outputs.

| run | set up | result |
|---|---|---|
| silent | nothing | the outputs are silent (the boot pattern plays nothing) |
| level | LVL 100 | a 110.0 Hz sine at -17.2 dBFS rms (100/127 of -12 dB peak, as written), THD+N -74.8 dB, no step larger than the sine's own |
| pitch | LVL 100, PIT 12 | 220.0 Hz, the same level and THD+N |
| vibrato | LFO1 Dest A on PIT, Dest B on stock parameter 0x24, both depths 0x6000 (sound words poked, pushed as a trig does) | the tone's pitch swings 110 to 843 Hz with a period of 0.50 s; stock's 0x24 under Dest B moves with a period of 0.49 s; over the last 2,048 blocks the tone's pitch and stock's value correlate at 0.984 |
| tremolo | LFO2 Dest A on LVL, Dest B on 0x24, depths 0x2000 | the tone's level swings 0.05 to 0.18 rms; it correlates with stock's 0x24 at 0.973 |

Where the two differ, the clamps differ: PIT stops at 0 and 48, stock's 0x24
at 0. The value read itself matches stock block for block for every source
(core-a4mk2 README, "Checked so far"). The audio load (`core_audio_load`, one
block's sample) was 130/256 without sound-check, 133 with it silent and
136 to 138 with one voice playing.

Not checked in the emulator: the effects' return's level against the dry
voices on a unit (the emulator adds it at unity, an assumption), and the
sound through the real DAC. Those are the unit's listening check.

```bash
python -m elekloader.sdk.build mods/core-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/sound-check-a4mk2 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.lint examples/sound-check-a4mk2/out/sound-check-1.0.elemod \
    --stock Analog-Four_MKII_OS1.56.syx --with mods/core-a4mk2/out/core-0.5.elemod
```
