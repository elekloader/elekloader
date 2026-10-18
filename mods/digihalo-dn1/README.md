# DigiHalo

A reverb pedal for the Digitone mk1's audio inputs: twelve reverb
machines (ROOM, HALL, PLATE, SPRING, SWELL, SWIRL, NEBULA, VOWEL, SHIMMER,
TAPE, NONLINEAR, REFLECTIONS), each with its own DECAY range, its
PARAM 1 and PARAM 2 and the rest of its parameters, HOLD as FREEZE or INFINITE on
a footswitch, PERSIST, SPILLOVER, a cab filter, presets and MIDI CCs. DigiHalo
is an elekloader mod for core-dn1 3.2.

Its MIDI CCs follow the Strymon BigSky's published CC chart (as listed on [midi.guide](https://midi.guide/d/strymon/bigsky/)), so a controller set up for that pedal works with it too. DigiHalo is not affiliated with Strymon and uses none of its code.

DIGIHALO is an entry in core-dn1's Mod Menu (hold a track key). While its
page is open, DigiHalo owns the audio engine, as DigiSwarm and DigiPrism do
(core-dn1 3.2's `core_audio`): the synths are silent, the Digitone's own
effects are off, and what you hear is the inputs through the pedal. NO gives
the audio and the screen back. The sequencer keeps running, so PLAY, STOP
and TEMPO still work and PRE-DELAY can follow the Digitone's tempo. BYPASS
(trig 16) passes the inputs through dry; with PERSIST on, the reverb's tail
rings out.

## The page

DigiHalo's page is drawn like the Digitone's own parameter pages, in their
fonts: the tempo in the box at the top left, the page in the title bar (page
1 is named after the machine: "Shimmer (1/3)"), the machine (two letters,
with F or I while HOLD freezes or sustains, S while a swell rises, B while
bypassed) and the output's meter on the left, and eight controls. While you
turn a knob the title bar shows its value (the
RT60, the pre-delay in ms or as a division, the interval, the vowel). LEFT
and RIGHT, or PAGE, move between the pages; holding FUNC shows page 2.
Pushing a knob sets it back to its default; on page 3 it steps the setting.

| Page | Knobs A-D | Knobs E-H |
|---|---|---|
| 1 (the machine) | DECAY, PRE-DELAY, MIX, TONE (TAPE and NONLINEAR: TIME and FEEDBACK for the first two) | PARAM 1, PARAM 2 (the machine's own, named for it), MOD, LOW END |
| 2 Params | the machine's other parameters (up to three: SHIMMER's AMOUNT and MODE, TAPE's DIFFUSION, NONLINEAR's LATE DECAY, LATE LEVEL and MOD SPEED, REFLECTIONS' SHAPE), BOOST | DIFFUSION (the machine's diffusers, 0 to 2x), MOD RATE (two octaves either side of the machine's), WIDTH, DRY |
| 3 Setup | TYPE (the machine), HOLD (FRZ or INF), PERSIST (trails), SYNC (PRE-DELAY on the tempo; TAPE's and NONLINEAR's TIME) | CAB (the cab filter), SPILL (SPILLOVER) |

A knob or a parameter the machine does not have is drawn dotted. PERSIST is
the shell's SETUP BYPASS setting (TRAILS or CUT): either one sets the other.

### PARAM 1 and PARAM 2

PARAM 1 and PARAM 2 are fixed to two of each machine's parameters, and the
rest are on page 2. LOW END is knob H for every machine.

| Machine | DECAY | PARAM 1 | PARAM 2 | page 2 |
|---|---|---|---|---|
| ROOM | 200 ms - 20 s | SIZE: STUDIO, CLUB | DIFFUSION 0-20 | |
| HALL | 500 ms - 20 s | SIZE: CONCERT, ARENA | MID 0-20 (10 flat) | |
| PLATE | 500 ms - 20 s | SIZE: SMALL, LARGE | | |
| SPRING | 800 ms - 10 s | DWELL: CLEAN, COMBO, TUBE, OVERDRIVE | SPRINGS: 1, 2, 3 | |
| SWELL | 500 ms - 20 s | RISE TIME 0.08 - 4 s (23 steps) | MODE: SWELL WET, SWELL DRY | |
| SWIRL | 500 ms - 20 s | LENGTH 0-17 | FEEDBACK 0-17 | |
| NEBULA | 1 s - 50 s | DIFFUSION 0-20 | | |
| VOWEL | 500 ms - 20 s | VOWEL: AAHHOO, AAHH, AAHHOH, OH, OOOHOH, OOOO, RANDOM | RESONANCE: MILD, MEDIUM, HIGH | |
| SHIMMER | 500 ms - 20 s | SHIFT 1: -1 octave .. +2 octaves (28 intervals) | SHIFT 2: OFF or the same 28 | AMOUNT 0-18, MODE: INPUT, REGEN, IN+REG |
| TAPE | TIME 200 ms - 1.5 s (the last head) | HEADS: 3, 4, 6 | SPACING: EVEN, UNEVEN | DIFFUSION 0-20 |
| NONLINEAR | TIME 50 ms - 2 s | SHAPE: SWOOSH, REVERSE, RAMP, GATE, GAUSS, BOUNCE | DIFFUSION 0-20 | LATE DECAY 0-17, LATE LEVEL 0-18, MOD SPEED 0-17 |
| REFLECTIONS | 133 - 400 ms (the room's size) | LOCATION X: L3 .. C .. R3 | LOCATION Y: F3 .. C .. B3 | SHAPE: SQUARE, RECT, OBLONG |

Choosing a machine (trig, T3/T4, TYPE, CC 19) puts its parameters at their
starting values; recalling a preset keeps the preset's PARAM 1 and 2. The parameters are one
set of knobs shared by the machines, not twelve sets: the project has room
for the shell's 136 bytes only.

## The keys

| key | on DigiHalo |
|---|---|
| T1 | held, HOLD while held (from the press); pressed for under 0.4 s, HOLD latched on or off. HOLD's mode is page 3's HOLD (or trig 14) |
| T2 tapped | the next saved preset (P1-P8, in turn) |
| T2 held | the wet out; on release it swells back over SWELL's RISE TIME (1 s on the other machines) |
| T3, T4 | the machine down, up |
| trig 1-12 | the machines, in TYPE order |
| trig 13, 14 | HOLD latched on or off; HOLD's mode |
| trig 1-8 in SETUP, FUNC + trig 1-8 | eight presets: in SETUP a trig key 1-8 saves the pedal there; FUNC + a trig key 1-8 recalls it (the knobs and page 3) |
| trig 15 | TAP (the clock becomes TAP) |
| trig 16 | BYPASS |

The global settings are on page 3 (PERSIST, SPILLOVER, CAB) and in YES's
SETUP (INPUT, CLOCK, BYPASS, MIDI CC, CC CLOSED).

## The machines

All the machines but SPRING share one tank: a feedback delay network of
eight lines (41-157 ms at the default sizes) mixed through a Householder
matrix, each line's gain set from DECAY's RT60 and its own length, a
one-pole damping in each line from TONE, and MOD moving two of the lines
(one a side) a quarter cycle apart. L feeds the even lines and R the odd
ones; the output is four lines a side. Before it are the pre-delay (up to
1.5 s, gliding as you turn it) and each machine's diffusers (two to five
allpasses a side) and own stage; after it TONE's low-pass, LOW END's shelf
(-9 to +9 dB under 250 Hz), WIDTH and the mix. The diffusers, SWIRL's and
SHIMMER's stages and the tank run at 24 kHz (each two frames averaged in,
and the output interpolated back up), which halves their cost; the
pre-delay, the reflections, the springs, the tape, the taps and the
after-stage run at 48 kHz.

- **ROOM**: a small tank and four early reflections a side (STUDIO, or 1.7x
  apart for CLUB) into the diffusers, whose strength is DIFFUSION. MOD moves
  the lines in its first half; in the second half it also moves the
  pre-delay.
- **HALL**: a large tank (ARENA 1.6x CONCERT), slow-building diffusion,
  later and softer reflections, and MID: a band from 300 Hz to 3 kHz,
  -12 dB to +6 dB.
- **PLATE**: a fast, dense build (strong diffusers) and no reflections;
  SMALL is splashier, LARGE smoother.
- **SPRING**: one, two or three springs, each a line (40-59 ms) through 14
  stretched allpasses (the chirp: the drip), a band-limit and LOW END's
  high-pass in the loop; DWELL drives a soft-clipping preamp before them
  (CLEAN to OVERDRIVE). Springs 1 and 2 are panned left and right, the third
  centred.
- **SWELL**: a hall whose wet (SWELL WET) or dry and its reverb (SWELL DRY)
  rises over RISE TIME after every note: a note after a quiet, or one that
  jumps over what is ringing, starts the rise again.
- **SWIRL**: three long allpasses a side with feedback around them (LENGTH
  sizes them 0.25x to 2.5x, FEEDBACK to 0.88) before the tank, so the reverb
  swells and lasts past DECAY; TONE is a resonant low-pass (470 Hz - 6 kHz).
- **NEBULA**: the largest tank (2.6x) behind five long diffusers a side,
  DIFFUSION from grainy (0) to smooth; past 2:00 MOD speeds its oscillators
  up.
- **VOWEL**: a hall sung through two formant filters a side (AH, OH, OO),
  moving slowly between the two vowels the VOWEL parameter names (RANDOM: a
  new one now and then);
  RESONANCE their Q; MOD the singers' wander; TONE their breath.
- **SHIMMER**: two pitch voices (each two crossfading heads on a 64 ms
  sweep, at 24 kHz) on the tank's input (INPUT), its output fed back (REGEN) or both;
  AMOUNT their level.
- **TAPE**: a multi-head tape echo: 3, 4 or 6 heads, the last at TIME,
  evenly or unevenly spaced; FEEDBACK (the PRE-DELAY knob) from the last
  head (EVEN) or the last two (UNEVEN), through TONE and LOW END in the
  loop; MOD is wow and flutter; DIFFUSION smears the heads and sends more of
  them into a short reverb.
- **NONLINEAR**: 16 taps over TIME in a SHAPE's envelope (SWOOSH, REVERSE
  and RAMP rise, GATE is level and cut off, GAUSS a bell, BOUNCE the bell
  upside down), FEEDBACK around them, DIFFUSION after them, and a late
  reverb (LATE DECAY 0.3-8 s, LATE LEVEL; off at 0); MOD and MOD SPEED move
  the late reverb (the taps stay put: reading 16 taps between frames cost
  too much).
- **REFLECTIONS**: the image sources of a room (100 to 1000 sq ft by DECAY,
  SQUARE, RECT or OBLONG) around the amp at LOCATION X and Y, heard from the
  front: 20 reflections of the first and second order, each as late as its
  path is longer than the direct one, as loud as its path is shorter and the
  walls are hard (TONE), from its side; then a short tail. The dry follows
  LOCATION X across. MOD moves the pre-delay, a chorus against the dry.

**HOLD** sets every feedback to one and the damping and modulation off.
FREEZE also stops the input reaching it: new notes play dry on top. INFINITE
lets them add, and a guard on the tank's peak keeps it from running away.
On NONLINEAR, HOLD holds the late reverb (at LATE LEVEL 9 when it is off);
on TAPE the tape loops.

**SPILLOVER** off: a machine change mutes the wet for 5 ms, clears the
buffers (16 KB a block, 27-90 ms) and brings the new machine in over 10 ms.
On: the buffers are kept and what rings carries into the new machine's
structure (no clear, the same short mute).

## MIDI

DigiHalo takes its CCs (the chart below) that come on the Digitone's auto channel
(MIDI CONFIG > CHANNELS > AUTO CHANNEL), through core-dn1's `ev_midi_cc`,
while its page is open; with SETUP's CC CLOSED on, also while it is closed,
and with MIDI CC on ANY CH on a track's channel too. The Digitone does not
apply a CC DigiHalo takes; every other CC goes on to the Digitone. MIDI
CONFIG > PORT CONFIG > INPUT FROM has to let them in.

A parameter with n steps takes 0 to n-1. CC 20 (a VALUE encoder) is not
taken.

| CC | | CC | |
|---|---|---|---|
| 19 | TYPE 0-11 (ROOM .. REFLECTIONS) | 23 | BOOST 0-60 (-3 .. +3 dB) |
| 17 | DECAY | 22 | PERSIST 0/1 |
| 18 | PRE-DELAY (TAPE, NONLINEAR: FEEDBACK) | 70 | HOLD MODE: 0 FREEZE, 1 INFINITE |
| 15 | MIX | 71 | MIDI CLOCK 0/1 (SYNC) |
| 3 | TONE | 97 | HOLD: off below 64, on from 64 |
| 14 | MOD | 93 | TAP |
| 9 | PARAM 1 | 102 | bypass below 64, on from 64 |
| 16 | PARAM 2 | 80, 82, 81 | footswitch A, B, C: down (below 64) recalls P1, P2, P3 |

Each machine's own parameters (taken always; applied when that machine is
running, as they belong to it):

| Machine | CCs |
|---|---|
| ROOM | 61 LOW END 0-20, 59 SIZE 0-1, 58 DIFFUSION 0-20 |
| HALL | 39 LOW END, 40 SIZE 0-1, 42 MID 0-20 |
| PLATE | 69 LOW END, 68 SIZE 0-1 |
| SPRING | 64 LOW END, 63 DWELL 0-3, 62 SPRINGS 0-2 |
| SWELL | 65 LOW END, 66 RISE 0-22, 67 MODE 0-1 |
| SWIRL | 31 LOW END, 32 LENGTH 0-17, 30 FEEDBACK 0-17 |
| NEBULA | 38 LOW END, 37 DIFFUSION 0-20 |
| VOWEL | 33 VOWEL 0-6, 34 RESONANCE 0-2 |
| SHIMMER | 24 LOW END, 25 SHIFT 1 0-27, 26 SHIFT 2 0-28, 27 AMOUNT 0-18, 28 MODE 0-2 |
| TAPE | 55 LOW END, 57 HEADS 0-2, 54 SPACING 0-1, 56 DIFFUSION 0-20 |
| NONLINEAR | 44 LOW END, 46 SHAPE 0-5, 45 DIFFUSION 0-20, 47 LATE DECAY 0-17, 48 LATE LEVEL 0-18, 43 MOD SPEED 0-17 |
| REFLECTIONS | 52 LOW END, 49 LOCATION X 0-6, 50 LOCATION Y 0-6, 51 SHAPE 0-2 |

Not taken: program changes and bank select (CC 0; core-dn1 passes mods the
CCs only), the expression pedal (100) and its enable (60).

## How it works

The render (`dsp.c`) runs at interrupt level in core-dn1's render, on each
block of 32 frames: the machine, then the after-stage and the mix. Everything
is fixed point in C, the lines holding their samples at half scale. The UI
(`ui.c`) runs in the Digitone's UI task. Both are on the pedal shell
(`pedal.h`, `pedal_dsp.inc`, `pedal_ui.inc`) the GAS pedals share.

- **Memory:** the buffers (1.1 MB: the 2.7 s pre-delay line, the tank's eight
  lines, 18 allpasses, three springs, the shimmer's line) are a region of the
  profile's `bulk` area (0x47900000-0x47B00000), cleared at the first five
  ticks after power-up. The code and its state take 37 KB of the 128 KB
  mods share (.run 34.6 KB, .bss 2.8 KB; compiled for size but the per-frame loops).
- **CPU:** in digikit's emulator (its cycle model), with the factory pattern
  playing and plucks on the inputs: closed, the stock render is 57% of a
  block; open, the synths muted, the stock part is about 17% and the whole
  render with DigiHalo, MOD at the top, is: ROOM 39.1%, HALL 39.1%, PLATE
  34.7%, SPRING 39.2%, SWELL 35.4%, SWIRL 42.4%, NEBULA 36.7%, VOWEL 45.2%
  (the heaviest: two formant filters a side at 48 kHz), SHIMMER 38.6%,
  TAPE 40.7%, NONLINEAR 39.0%, REFLECTIONS 43.8%. DigiHalo's own share is
  so 18-28%. The emulator's timing assumes memory without wait states, so a
  unit runs slower: the shell's guard bypasses the pedal (no trails) if the
  whole render stays over 93% for 0.2 s, and says so in the title.
- **Project data:** the pedal and eight presets are saved with the project
  (tag `DSKY`, the shell's 136 bytes).

## Build

```bash
python -m elekloader.sdk.build mods/digihalo-dn1 --stock Digitone_and_Digitone_Keys_OS1.43.syx
python -m elekloader.sdk.build mods/digihalo-dn1 --stock Digitone_and_Digitone_Keys_OS1.44.syx
```

It names no firmware address, so its 1.44 port is empty. It needs core-dn1
3.2. `gentables.py` makes `tables.h`; `tests.sh` runs the render on a
computer (the shell's `host.c`) and measures it with `host_an.py`.

## Checked

In digikit's emulator, OS 1.43 and 1.44, and with the render's own code
compiled for a computer and fed test signals (`tests.sh`, `host_an.py`):

- **On a unit:** tested on a Digitone mk1 by its author: it loads and works
  as expected. It is still a beta.

- **The firmware.** A build with the released core-dn1 3.2 and DigiHalo
  passes every stage of the emulator's check against stock on 1.43 and
  1.44, with every screen identical.
- **The probe** (1.43): closed, the synths; open, the plucks through the
  pedal and the synths silent; the three pages, SETUP, a knob, a preset
  saved and recalled; T1 tapped (HOLD latched), held (HOLD while held, off on
  release); every machine on trig 1-12 (its flag, its sound, its render
  share above); T3 and T4 stepping the machine; trig 13 and 14 (HOLD latched,
  FREEZE); BYPASS; its CCs on the auto channel (TYPE, the knobs,
  SHIMMER's SHIFT 1 and AMOUNT, BOOST, PERSIST, HOLD MODE, HOLD, MIDI CLOCK,
  bypass) taken and set, another machine's CC taken and ignored, a CC
  outside the chart passed on; NO, and the synths again.
- **The machines** (on a computer, DECAY 64): RT60 against DECAY's
  (ROOM 1.76 s for 2.0, HALL 2.76 for 3.2, PLATE 2.70 for 3.2, SPRING 2.35
  for 2.8, SWELL 2.29, SWIRL 3.11, NEBULA 5.67 for 7.1, SHIMMER 2.91, all
  within about 25% short; at DECAY 0 ROOM 0.24 s, HALL 0.59, NEBULA 1.08,
  SPRING 0.91); every machine at DECAY 127 on a burst decays (no runaway),
  SHIMMER with REGEN and IN+REG at full AMOUNT too; plucks come out within a
  few dB of the dry at MIX 64. FREEZE on every machine holds the level within
  1-4 dB over 6.5 s (NEBULA's and VOWEL's 100 ms windows beat by up to 7 dB:
  a 20 s NEBULA freeze stays at -20 dB, +-1.5); INFINITE with noise pouring in
  stays bounded (the guard). SHIMMER on a 440 Hz sine: +1 octave puts 880 Hz
  at -16 dB, -1 octave 220 Hz at -21 dB, a fifth 660 Hz at -18 dB.
  TAPE's heads land at the last head's time / heads (3, 4, 6, uneven);
  NONLINEAR's six shapes give their envelopes (rising, level and cut, bell,
  upside-down bell); REFLECTIONS' LOCATION X moves the reflections and the
  dry left and right, Y from front (quieter reflections) to back; VOWEL's
  AAHH peaks around 1150/800 Hz, OOOO around 450/325 Hz; the PRE-DELAY and
  its synced divisions; LOW END's shelf, CAB's band, WIDTH, DRY, MIX's law;
  machine changes with SPILLOVER off and on without clicks; PERSIST's
  trails and a clean cut without them; T2's swell; the CPU guard's trip.

## Limits

- Preset names, banks, and program changes: eight presets, by trig key or
  T2.
- Twelve machines' worth of parameters at once: one shared set of knobs,
  set to the machine's starting values when you choose it.
- Assignable PARAM knobs: each machine's two are fixed.
- An expression pedal input, external footswitches, footswitch LEDs.
- An analog dry path and true bypass: the dry is converted, as everything on
  the Digitone is.
- Full-size machines: VOWEL's two formants a side and REFLECTIONS' 20 image
  sources are the most cut down.

## Licence

GPL-2.0-or-later, as the rest of elekloader.
