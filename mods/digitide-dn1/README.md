# DigiTide

A two-channel pedal for the Digitone mk1's audio inputs: a wet channel
(REVERB, DELAY, GRAB, with FREEZE) and an always-listening micro-looper
(ENV, TAPE, STRETCH, with OVERDUB) that pass sound to each other as
ROUTING says, a CLOCK that lowers the sample rate of both, extra options
and switches, presets and MIDI CCs. DigiTide is an elekloader mod for
core-dn1 3.2.

Its MIDI CCs follow the Chase Bliss MOOD MKII's published CC chart, so a
controller set up for that pedal works with it too. DigiTide is not
affiliated with Chase Bliss and uses none of its code.

DIGITIDE is an entry in core-dn1's Mod Menu (hold a track key). While its
page is open, DigiTide owns the audio engine, as DigiSwarm and DigiPrism
do (core-dn1 3.2's `core_audio`): the synths are silent, the Digitone's own
effects are off, and what you hear is the inputs through the pedal. NO
gives the audio and the screen back. The sequencer keeps running, so PLAY,
STOP and TEMPO still work and the clock can follow the Digitone's tempo.
Opening the page steps the pedal on; BYPASS (trig 16) passes the inputs
through dry, with the wet channel's tail.

## The two channels

**The wet channel** is a suite of real-time effects, picked with WET on
page 3 or T3:

| Mode | TIME | MODIFY (WMOD) | Frozen (T1 held) |
|---|---|---|---|
| REVERB | its size and decay together: a decay of 0.3 s to about 15 s | the smear: at 0 a cluster of distinct echoes (a multi-tap delay), up to a dense reverb | an ambient pad: the tail holds, nothing new comes in |
| DELAY | the delay time: 20 ms to 1.2 s, or with the clock 1/32, 1/16, 1/8T, 1/8, 1/8D, 1/4 | the feedback; at 127 the repeats hold, as a looper | a looping echo |
| GRAB | how often it samples: 25-800 ms, or the same divisions | the playback's speed and direction: the middle plays at 1x, right goes up in semitones to 2x, left plays backwards, faster to 2x | a repeating tone |

A new TIME in DELAY crossfades to the new delay instead of bending the
repeats' pitch.

**The looper** always listens: a ring keeps the last 15 seconds of
what the pedal plays (at the 48K clock). Turning it on (T2) loops the last
LENGTH of it, so you catch what just happened; there is nothing to arm.
While it plays, the ring stops; turned off, it listens again from where it
stopped, so a loop stays in the ring for a while, and turning it off and on
again catches what is new. LENGTH is in time
with the tempo when the clock follows the Digitone (x/32 to x1 of two bars:
a 1/16 note to two bars), or free from 40 ms to 10 s.

| Mode | LENGTH | MODIFY (LMOD) |
|---|---|---|
| ENV | the slice's size, 4 ms to 1 s | the sensitivity: sound at the input over its threshold holds the slice the loop is in, which repeats until the input goes quiet, and the loop goes on from there. At 0 nothing holds |
| TAPE | shortens the loop from its start (up to what was caught) | the speed and direction in octaves: 4x, 2x, 1x, 0.5x backwards (left) and forwards (right) |
| STRETCH | the grains' size, 4 ms (blurry) to 1 s (clear) | how fast the loop moves under the grains, pitch kept: 1x backwards at 0, slower to stalled in the middle (the moment held), faster to 1x forwards at 127 (no effect) |

Holding T2 overdubs: the clean input goes onto the loop (the wet channel is
not recorded) while FADE (page 2) lowers the old loop a
little each pass. NO DUB (page 3) replaces the loop with the input while
overdubbing. The loop's seams are faded over XFAD (page 2) into what came
before the loop, so they do not click, either way the loop plays.

**ROUTING** (page 3, or T3 held) decides what the wet channel hears when both
channels are on: IN (the input; the loop goes to the output beside it),
ML+I (the input and the loop) or ML (the loop alone, the input dry beside
it). A loop routed through the wet channel is heard 100% wet; DIRECT
(page 2) blends its clean version back in. While the looper listens, it
records what the pedal plays, the wet channel included, whatever the
routing.

**CLOCK** is the sample rate, from 2K to 48K in steps (2K,
3K, 4K, 6K, 8K, 12K, 16K, 24K, 32K, 48K), or without steps with SMOOTH CLK
in SETUP. Both channels run at that rate (a one-pole before it keeps the
worst of the aliasing out, and another smooths the steps after it, so a
lower clock is also darker), and what they keep is counted in samples: a
loop caught at 48K plays an octave down and twice as long at 24K, a fifth
down at 32K; the delays and the reverb get longer and darker; at 2K-4K the
pedal is all aliasing and grit. A synced LENGTH or TIME is in time at 48K
and at the octaves below it (24K, 12K, 6K, 3K: twice, four times ...).
RAMP (page 2) bounces CLOCK from its knob down to the bottom and back.

## The page

DigiTide's page is drawn like the Digitone's own parameter pages, in their
fonts: the tempo in the box at the top left, the page in the title bar, the
channels' state on the left (W the wet channel on, F frozen; L the looper
playing, O overdubbing, a dot while it listens; under them the modes'
letters, R D S and E T S, a star while ENV holds a slice, B while
bypassed) and the output's meter, and eight controls, one per knob. A knob
that does nothing now is dotted (the wet's while it is off, LMOD while the
looper listens, WDTH without SPREAD on the wet, DIRECT with ROUTING IN, FADE
with NO DUB). While you turn a knob the title bar shows its value; LEFT and
RIGHT, or PAGE, move between the three pages, and holding FUNC shows page 2.

| Page | Knobs A-D | Knobs E-H |
|---|---|---|
| 1 Tide (the main knobs, the wet side left, the looper's right) | TIME, MIX, LNGT (LENGTH), BAL (level balance) | WMOD (the wet's MODIFY), CLK (CLOCK), LMOD (the looper's MODIFY), TONE |
| 2 Extras (the extra options) | WDTH (stereo width), FADE, DRCT (DIRECT, the clean loop), OUT (output level) | WOBL (the wet's modulation), DAMP (the damping inside the wet's feedback), XFAD (the loop's seams, 1-60 ms), RAMP (CLOCK's bounce) |
| 3 Toggles | WET (REV, DLY, GRAB), LOOP (ENV, TAPE, STR), ROUT (IN, ML+I, ML), SYNC (OFF, LOOP, WET) | SPRD (OFF, BOTH, WET, LOOP), LTCH (LATCH), DRYK (DRY KILL), NDUB (NO DUB) |

MIX blends the input and the pedal (both channels). BAL
sets the two channels' levels: the middle (EVEN) both at unity, left the
looper quieter, right the wet quieter. TONE is a
hi-cut on the wet channel from open (127) down to 600 Hz.

| Control | What it does |
|---|---|
| T1 | tap the wet channel on/off, hold FREEZE (with LTCH, a hold toggles FREEZE) |
| T2 | tap the looper on (plays the last loop) or off (listens), hold OVERDUB (with LTCH, a hold toggles OVERDUB) |
| T3 and T4 tapped | the next WET and LOOP mode; also on page 3 |
| T3 held | the next routing; also on page 3 |
| page 2, or FUNC held | the extra options; BAL and TONE are on page 1 |
| page 3's SYNC (T4 held: the next) | the looper to the wet, or the wet to the looper: LOOP makes the loop four of the wet's TIME; WET makes TIME a part of the loop (1/16, 1/8, 1/6, 1/4, 1/3, 1/2) |
| page 3's SPRD | SPREAD on both channels, the wet only or the looper only |
| SETUP's LOOP BUFFER | half or full: HALF halves LENGTH |
| page 3 | LATCH, DRY KILL, NO DUB |
| SETUP | SMOOTH CLK, INPUT (MONO: input L to both sides), BYPASS (TRAILS or CUT) |
| trig 15 | tap tempo |
| presets | 8: in SETUP a trig key 1-8 saves the pedal there; FUNC + a trig key 1-8 recalls it |
| trig 1-6, 7-12, 13 and 14 | TIME's six divisions; LENGTH's six; CLOCK a step down, up |

With SPREAD each mode makes its own stereo: REVERB widens its tail (WDTH,
0 to 2x the sides), DELAY ping-pongs (each repeat crosses sides, so a sound
on one side moves to the other and back), GRAB pans, a cycle every two
samples; on the looper, TAPE plays a copy backwards on the left, STRETCH
pans slowly, and ENV pans while a slice is held.

## MIDI

DigiTide takes its MIDI CCs (the chart below) that come on
the Digitone's auto channel (MIDI CONFIG > CHANNELS > AUTO CHANNEL), through
core-dn1's `ev_midi_cc`, while its page is open; with SETUP's CC CLOSED on,
also while it is closed, and with MIDI CC on ANY CH on a track's channel too.
The Digitone does not apply a CC DigiTide takes; every other CC goes on to
the Digitone. MIDI CONFIG > PORT CONFIG > INPUT FROM has to let them in.

| CC | | CC | |
|---|---|---|---|
| 14 | TIME (with the clock: its six divisions by the chart's bands) | 24 | stereo width (WDTH) |
| 15 | MIX | 26 | FADE |
| 16 | LENGTH (the chart's six bands, x/32 .. x1) | 27 | TONE |
| 17 | MODIFY (the wet channel) | 28 | level balance (BAL) |
| 18 | CLOCK (the chart's bands; its 64K is 48K here) | 29 | DIRECT (DRCT) |
| 19 | MODIFY (the looper; TAPE's and STRETCH's bands) | 31 | SYNC: 0-1 the looper to the wet, 2 none, 3-127 the wet to the looper |
| 20 | RAMP (here CLOCK's bounce speed) | 32 | which channels SPREAD is on: 0-1 the wet, 2 both, 3-127 the looper |
| 21 | WET: 0-1 REVERB, 2 DELAY, 3-127 GRAB | 33 | LOOP BUFFER: 0-1 half, 2-127 full |
| 22 | ROUTING: 0-1 IN, 2 ML+IN, 3-127 ML | 72-78 | mono input, SPREAD, DRY KILL, TRAILS, LATCH, NO DUB, SMOOTH (0 off, 1-127 on) |
| 23 | LOOP: 0-1 ENV, 2 TAPE, 3-127 STRETCH | 102, 103 | the looper, the wet channel: 0 off, 1-127 on |
| 104 | the extra options: 1-127 page 2, 0 page 1 | 105, 106 | FREEZE, OVERDUB: 0 off, 1-127 on |
| 93, 107 | TAP | | |

Not mapped (the Digitone does them): ramping's waveform (25) and its
switches (61-68), a classic mode (71), MIDI clock ignore and clock divisions
(51-54), true bypass, factory and MIDI reset (55, 56, 110), expression
(100), preset save (111) and program changes, and a synth mode.

## How it works

The render (`dsp.c`) runs at interrupt level in core-dn1's render, on each
block of 32 frames: the input to the clock's ticks (at 48K, every frame),
the looper, the wet channel fed per ROUTING, the ring (while the looper
listens), back to frames (each tick held, then smoothed), and the mix. The
pedal shell (`pedal_dsp.inc`) then sets OUTPUT LEVEL and a soft clip.
Everything is fixed point in C; the ring and the lines hold 16-bit samples
at half scale. REVERB is DigiPrism's SPACE (eight lines with a Householder
feedback matrix) with its allpasses' coefficient on MODIFY. The UI
(`ui.c`) runs in the Digitone's UI task.

- **Memory:** the ring (15 s at 48K, 2.9 MB), the wet channel's line
  (2.7 s, shared by DELAY and GRAB) and REVERB's lines, 3.4 MB of the region
  DigiTide claims in the Digitone profile's `bulk` area
  (0x47300000-0x47700000), cleared in the first 14 ticks after power-up.
  The code and its state take 33 KB of the 128 KB mods share (the
  exponential knob curves are worked out, not tabled, to keep it small).
- **CPU:** in digikit's emulator, with the factory pattern playing (the
  Digitone's own render is about 18% of a block with the synths muted):
  the whole render takes 38% of a block with the defaults (REVERB, the
  looper listening), 44% (at most 46%) with REVERB and STRETCH both
  playing, the heaviest (DigiTide's own share about 26%), 40% with REVERB
  and TAPE, 32-33% with DELAY or GRAB and STRETCH, and 25% at CLOCK 2K
  (the channels run on one frame in 24). REVERB is the heavy part: eight
  lines, two of them modulated, and two allpasses a side. The emulator's
  timing tables assume memory without wait states,
  so a unit runs slower: a guard times the whole render on DMA timer 0
  every block, and if the unit stays over 93% for 0.2 s it bypasses the
  pedal at once (no trails) and says so in the title ("CPU full:
  bypassed"); BYPASS turns it back on.
- **Project data:** the pedal (the knobs, the extra options, the toggles,
  the channels' latches and SETUP) and the eight presets are saved with the
  project (tag `DMOD`, 136 bytes). A preset keeps the eight knobs of page 1
  and the eight toggles; the extra options are the project's. The loop is
  not saved: the Digitone's +Drive has no files for mods.

## Build

With the SDK and a stock OS file:

```bash
python -m elekloader.sdk.build mods/digitide-dn1 --stock Digitone_and_Digitone_Keys_OS1.43.syx   # out/digitide-0.1.1.elemod
python -m elekloader.sdk.build mods/digitide-dn1 --stock Digitone_and_Digitone_Keys_OS1.44.syx   # out/digitide-0.1.1-os1.44.elemod
```

It names no firmware address, so its 1.44 port is empty. It needs core-dn1
3.2 (`"resources": {"core": "3.2"}`) and an elekloader with the profile's
`bulk` area. `gentables.py` makes `tables.h`.

## Checked

In digikit's emulator, OS 1.43 and 1.44, and with the render's own code
compiled for a computer and fed test signals (`tests.sh`):

- **On a unit:** tested on a Digitone mk1 by its author: it loads and works
  as expected. It is still a beta.

- **The firmware.** A build with the released core-dn1 3.2 and DigiTide
  passes every stage of the emulator's check against stock on 1.43 and
  1.44, with every screen identical.
- **In the emulator** (1.43, plucked notes on the inputs, the factory
  pattern playing): closed, the synths; open, the plucks through the
  REVERB and the synths silent (with the inputs silent too, an rms of 0
  against the synths' 1819); the three pages, SETUP, a knob turn, a preset
  saved as P5 and recalled, T1-T4 tapped and held (the wet on and frozen,
  the looper caught and overdubbing, the next modes, ROUTING and SYNC), a
  second catch, CLOCK down to 2K and a step up by trig 14, TIME's trigs,
  BYPASS; the chart's CCs (the knobs, the modes, ROUTING, the extra
  options, SYNC, DRY KILL, the channels on and off, FREEZE, TAP) taken and
  applied, and a CC outside the chart going on; after NO the synths again.
- **The effects** (the render on a computer): with both channels off the
  input untouched, with DRY KILL silence; REVERB's decay from 0.3 s to
  about 15 s, its first reflection earlier with MODIFY up, FREEZE holding
  the tail within 1 dB for 5 s and letting it go; DELAY's first repeat at
  the knob's time (20 ms, 157 ms, 1.2 s) and on the tempo (1/4: 500 ms,
  1/16: 125 ms), its feedback, a steady loop at MODIFY 127 and frozen, a new
  TIME without a click; GRAB at 1x, a fifth up, 2x and 2x backwards (440 Hz
  in: 440, 660, 880, 880 Hz), frozen a held tone; the looper catching the
  note that was just played, TAPE at 0.5x, 1x, 2x, 1x and 4x backwards
  (220, 440, 880, 440, 1760 Hz), LENGTH shortening the loop, seams with no
  step larger than the sine's own, the catch and the release without a
  click; STRETCH with no stretch (the loop unchanged), 4x slower and
  stalled at the loop's pitch; ENV holding slices while the input is loud;
  OVERDUB adding, with FADE settling, with NO DUB replacing; the three
  routings and DIRECT; CLOCK 24K and 32K playing a 48K loop an octave and
  a fifth down, 3K darker, SMOOTH and RAMP; SYNC (the loop four of TIME;
  TIME a quarter of the loop: 500 ms); the level balance at both ends; TONE;
  WIDTH 0 (mono); BYPASS with its tail and without; the ring listening while
  bypassed; the CPU guard's trip.
- **CPU** on the emulator's cycle clock: above.

## Not done (yet)

- A 64K clock (DigiTide's top is 48K, the Digitone's rate) and MIDI clock
  divisions; MIDI clock itself (the Digitone's tempo, or TAP, stands in).
- Ramping and its waveforms on any knob, expression and CV: here only
  CLOCK bounces (RAMP, a triangle).
- A classic mode (noise, degrading loops, a ringing reverb, a bendy
  delay).
- A synth mode, program changes and more than eight presets.
- An ENV where the loop's level follows the input; here ENV holds a slice
  while the input is loud.

## Licence

GPL-2.0-or-later, as the rest of elekloader ([LICENSE](../../LICENSE)).
