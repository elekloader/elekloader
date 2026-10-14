# DigiHedra

A three-voice rhythmic pitch shifter for the Digitone mk1's audio inputs,
after the [Meris Hedra](https://meris.us/product/hedra-pedal/): three
pitched voices from two octaves down to two octaves up, chromatic or in a
key and scale from the note a pitch detector hears in what you play, with
MICROTUNE, GLIDE and pitch CORRECTION; each voice behind its own division of
a delay in time with the Digitone's tempo, in the Hedra's four delay modes
(PITCH FEEDBACK, SERIES, DUAL + CROSS, DUAL), with rhythm patterns, HALF
SPEED, SWELL, FREEZE, presets and the Hedra's MIDI CCs. DigiHedra is an
elekloader mod for core-dn1 3.2. It is not affiliated with Meris and uses
none of its code; the effect is rebuilt from the Hedra's public manual and
its MIDI chart.

DIGIHEDRA is an entry in core-dn1's Mod Menu (hold a track key). While its
page is open, DigiHedra owns the audio engine, as DigiCosm and DigiChroma do
(core-dn1 3.2's `core_audio`): the synths are silent, the Digitone's own
effects are off, and what you hear is the inputs through the pedal. NO gives
the audio and the screen back. The sequencer keeps running, so PLAY, STOP and
TEMPO still work and the delay can follow the Digitone's tempo. BYPASS
(trig 16) passes the inputs through dry, with the delay's tails.

## The page

DigiHedra's page is drawn like the Digitone's own parameter pages: the tempo
in the box at the top left, the page in the title bar (or the state: Freeze,
Pitch off, Swell), and on the left the note the detector hears (with a bar
under it while it hears one), the three voices (a square each: filled when
pitched, hollow while T1 has the pitch off, a dot when the voice is OFF), F,
S or H for FREEZE, SWELL and HALF SPEED, B while bypassed, and the output's
meter. While you turn a knob the title bar shows its value: PITCH as an
interval (+7ST, -OCT, or in a scale +3RD, -5TH, +2OCT), KEY as a note, TIME
as the division and its milliseconds. LEFT and RIGHT, or PAGE, move between
the three pages; holding FUNC shows page 2. Pushing a knob sets it back to
its default.

| Page | Knobs A-D | Knobs E-H |
|---|---|---|
| 1 Hedra (the Hedra's knobs) | PIT1, PIT2, PIT3 (each voice's interval), MIX | TIME (the delay), FDBK (feedback, the Hedra's Alt + MIX), KEY, MTUN (MICROTUNE) |
| 2 Alt (the Hedra's Alt functions, and more) | TDV1, TDV2, TDV3 (each voice's time division), GLID (GLIDE) | LEV1, LEV2, LEV3 (each voice's level), OUT (output level) |
| 3 Settings | MODE (PFB, SER, DX, DUAL), SCAL (the scale), RHYT (a rhythm pattern, or MAN: the TDV knobs), CORR (pitch correction: NONE, LOOS, STRC) | ALGO (CLAS, SMTH), HALF (half speed), SMTH (pitch smoothing), SPRD (WIDE, NARR, MONO) |

| Hedra | DigiHedra |
|---|---|
| PITCH 1-3: -2 octaves .. +2 octaves, OFF at the minimum, unison in the middle | PIT1-3, the same bands as the Hedra's chart (chromatic, seven-note and pentatonic charts) |
| KEY (maximum: chromatic); Alt: the scale | KEY (the Hedra's bands, the top CHROMATIC); SCAL on page 3; trig 1-12 set the key C .. B, trig 13 chromatic, trig 14 the next scale |
| MICRO TUNE | MTUN: voice 1 up to 30 cents sharp, voice 2 as flat, voice 3 half of it |
| Alt + MICRO TUNE: correction (lower half), glide (upper half) | CORR on page 3 and GLID on page 2 (CC 23 does both, as the Hedra's) |
| MIX; Alt: feedback | MIX; FDBK |
| Alt + PITCH 1-3: time divisions | TDV1-3 on page 2, the Hedra's divisions: OFF, a micro range (3-42 ms), 1/6, 1/4, 1/3, 3/8, 1/2, 5/8, 2/3, 3/4, 5/6, 7/8, 1 of the time |
| TAP footswitch: the tempo (in quarters) | trig 15, TAP; or the Digitone's tempo (SETUP's CLOCK); TIME picks the delay as a division of it |
| Hold TAP: a momentary swell | T2 held; T2 tapped latches it (and CC 31) |
| Alt + TAP: half speed | T3, or HALF on page 3 |
| The mode button: four delay modes | MODE on page 3 |
| BYPASS; Alt + BYPASS: pitch knob smoothing | trig 16; SMTH on page 3 |
| 16 presets | 8: in SETUP a trig key 1-8 saves the pedal there; FUNC + a trig key 1-8 recalls it |
| Expression pedal, MIDI notes setting the voices | not here (see Not yet) |

## Track keys

- **T1 PITCH**: tap to turn the voices' intervals off (they repeat the input
  at unison, so the delay goes on as a plain rhythmic delay) or back on;
  hold for a moment's change.
- **T2 SWELL**: tap to latch the swell, hold for a moment's: each new note
  starts from silence and fades in over 0.4 s, as the Hedra's held TAP.
- **T3 HALF SPEED**: tap for half speed: the times double (up to 1 s) and the
  repeats darken.
- **T4 FREEZE**: tap to latch, hold for a moment: the delay goes round as it
  is (no new sound in, nothing fades), the dry carrying on over it.

## The voices

Each voice is a pitch shifter on a line of the input (both inputs summed):
two (ALGO CLAS, 25 ms grains, the lighter) or three (SMTH, 50 ms grains, the
smoother) overlapping heads with Hann windows read the line at the voice's
ratio and start a new grain while their window is closed. While the detector
hears a pitch, each grain starts a whole number of its periods from the one
before, so held notes come out without the comb and warble of a plain
shifter; on chords and noise the heads simply overlap, as a plain shifter's
do. A voice up reads behind the present so its grain never passes it (on
average half a grain times the ratio less one: with SMTH 25 ms an octave up,
38 ms two octaves up, where the grains are shorter; half that with CLAS); a voice down starts each grain at the present (on
average 12 ms behind an octave down with SMTH); unison has no latency.
Voice 1 is on the left, voice 2 in the centre, voice 3 on the right (SPRD
WIDE; NARR closer, MONO all centred).

**Diatonic.** With KEY on a note, the PITCH knobs count steps of the scale
(+3RD is two steps up, +OCT seven, or five in a pentatonic scale), from the
note the detector hears: a fixed-point YIN on the voices' input decimated to
12 kHz (a 16 ms frame, its lags spread over 32 blocks: a new pitch every
22 ms, 62 Hz to 2 kHz), with a hysteresis of 0.6 semitones before it calls a
new note. The interval is the scale's from that note, so a +3RD over A in C
major is a minor third (C) and over C a major one (E). A note outside the
scale keeps its offset from the scale note under it. CORRECTION pulls the
voices onto the scale's exact pitches when you play out of tune: NONE keeps
your intonation, LOOS halves the error, STRC removes it. The scales: Major,
Minor, Melodic Minor, Harmonic Minor, Double Harmonic, Lydian Pentatonic and
Minor Pentatonic. Diatonic harmonies need one note at a time, as on the
Hedra; for chords and pads use CHROMATIC (KEY at the top).

**GLIDE** slides each voice to a new interval, from at once (0) to a time
constant of 2 s; with SMTH (pitch smoothing) on, changes take 25 ms at
least, so turning a PITCH knob sweeps; off, they step.

## The delay

TIME is the main delay: with CLOCK on DIGITONE (or TAP) a division of the
tempo (1/16, 1/8T, 1/8, 1/8D, 1/4, 1/4D, 1/2), halved until it fits, as the
Hedra fits its tapped tempo (520 ms in the series modes, 260 ms in the dual
ones; HALF SPEED doubles both); on FREE, 20 to 520 ms. Each voice sits
behind its own division of TIME (TDV1-3, or the RHYTHM pattern), and the
repeats come every TIME after it. Turning TIME or a division bends the
repeats as a tape's would (half a frame a frame at most).

| MODE | |
|---|---|
| PFB (series + pitch feedback) | one line; the repeats go back through the pitch shifters, so each climbs (or falls) by the voices' intervals again: +7 makes a stack of fifths. A repeat comes every TIME plus its voice's division |
| SER (series) | one line repeating the three voices every TIME, centred, unpitched |
| DX (dual + cross feedback) | a line a side, 260 ms at most; each side's repeats go to the other (ping-pong) |
| DUAL | a line a side, each repeating its own side |

| RHYTHM | the three voices' divisions of TIME |
|---|---|
| MAN | TDV1-3 (page 2) |
| NONE | all at once |
| STR8 | 1/4, 1/2, 3/4 |
| TRIP | 1/3, 2/3, 1 |
| DOTD | 3/8, 3/4, 1 |
| SWNG | 1/3, 1/2, 5/6 |
| GALP | 1/6, 1/3, 1/2 |
| CASC | 1/4, 5/8, 1 |

FDBK goes to 98% at the top; the repeats darken (7 kHz, 3.5 kHz at half
speed) and a soft limit in the loop keeps them bounded. In PFB the feedback
is shared between the voices that are on.

## MIDI

DigiHedra takes the Hedra's CCs (its manual's chart) that come on the
Digitone's auto channel (MIDI CONFIG > CHANNELS > AUTO CHANNEL), through
core-dn1's `ev_midi_cc`, while its page is open; with SETUP's CC CLOSED on,
also while it is closed, and with MIDI CC on ANY CH on a track's channel too.
The Digitone does not apply a CC DigiHedra takes; every other CC goes on to
the Digitone. MIDI CONFIG > PORT CONFIG > INPUT FROM has to let them in.

| CC | | CC | |
|---|---|---|---|
| 9 | HALF SPEED: off below 64, on from 64 | 22 | SCALE: Major 0-11, Minor 12-37, Melodic 38-58, Harmonic 59-78, Double Harmonic 79-97, Lydian Pentatonic 98-119, Minor Pentatonic 120-127 |
| 14 | bypassed below 64, on from 64 | 23 | CORRECTION (0-63: NONE, LOOSE, STRICT by thirds; GLIDE off) and GLIDE (64-127) |
| 15 | TIME (the Hedra's tempo) | 24 | FEEDBACK |
| 16 | KEY (the Hedra's bands; 120-127 chromatic) | 25-27 | TIME DIVISION 1-3 |
| 17 | MICROTUNE | 28 | TAP (at 127) |
| 18 | MIX | 29 | MODE: PITCH FEEDBACK 0-31, SERIES 32-63, DUAL + CROSS 64-95, DUAL 96-127 |
| 19-21 | PITCH 1-3 | 30 | PITCH SMOOTHING: off below 64 |
| | | 31 | SWELL: off below 64 |

The Hedra's CC 4 (its expression pedal) and program changes (its presets)
are not mapped: there is no expression morph here, and core-dn1 passes mods
the CCs only.

## How it works

The render (`dsp.c`) runs at interrupt level in core-dn1's render, on each
block of 32 frames: the voices' input (the inputs, with PFB's repeats, and
SWELL's gain) onto its line, the detector's next lags, the three voices,
the delay as MODE has it, MIX against the dry, OUTPUT LEVEL and a soft clip.
Everything is fixed point in C: samples are 16-bit with 6 dB of headroom,
and the lines hold them at half scale. The UI (`ui.c`) runs in the
Digitone's UI task.

- **Memory:** the voices' line (1.4 s mono), the delay's (1.4 s stereo) and the
  detector's 4 KB: 388 KB of DigiHedra's 896 KB region in the profile's `bulk` area, cleared
  in two ticks the first time; the code and its state take 25.6 KB of
  the 128 KB mods share.
- **CPU:** in digikit's emulator, with plucks on the inputs: see Checked.
  The detector costs the same in every mode (it also aligns the grains);
  SMTH costs a third more than CLAS. A guard times the whole render on DMA
  timer 0 every block, and if the unit stays over 93% for 0.2 s it bypasses
  the pedal at once (no trails) and says so in the title ("CPU full:
  bypassed"); BYPASS turns it back on.
- **Project data:** the pedal (the knobs, page 2, the settings, the latches
  and SETUP) and the eight presets are saved with the project (tag `DHED`,
  136 bytes). A preset keeps the page 1 knobs and the page 3 settings.

## Build

With the SDK and a stock OS file:

```bash
python -m elekloader.sdk.build mods/digihedra-dn1 --stock Digitone_and_Digitone_Keys_OS1.43.syx   # out/digihedra-0.1.elemod
python -m elekloader.sdk.build mods/digihedra-dn1 --stock Digitone_and_Digitone_Keys_OS1.44.syx   # out/digihedra-0.1-os1.44.elemod
```

It names no firmware address, so its 1.44 port is empty. It needs core-dn1
3.2. `gentables.py` makes `tables.h` (the Hedra's PITCH, KEY and time
division charts are there as tables).

## Checked

In digikit's emulator, OS 1.43 and 1.44, and with the render's own code
compiled for a computer and fed test signals (`tests.sh`):

- **On a unit:** tested on a Digitone mk1 by its author: it loads and works
  as expected. It is still a beta.

- **The firmware.** A build with the released core-dn1 3.2 and DigiHedra
  passes every stage of the emulator's check against stock on 1.43 and
  1.44, with every screen identical.
- **The page and the keys** (1.43): the Mod Menu entry, the three pages,
  SETUP, a knob turned and its value in the title, a preset saved as P5 and
  recalled, T1-T4 tapped (each latch on: PITCH off, SWELL, HALF SPEED,
  FREEZE, the left column showing them) and tapped again (off), T4 held (a
  moment's FREEZE), trig 3 (KEY D), trig 13 (CHROMATIC), trig 14 (the next
  scale), page 3's MODE turned to PITCH FEEDBACK, BYPASS, NO (the synths
  back, the owner off), as screenshots and as the state DigiHedra keeps.
- **MIDI** (1.43): each kind of CC in the chart on the auto channel (PITCH,
  KEY, MIX, FEEDBACK, a TIME DIVISION, SCALE's bands, CC 23's CORRECTION
  and GLIDE halves, MODE's bands, HALF SPEED, SWELL, PITCH SMOOTHING,
  bypass and on) set DigiHedra and stopped at core's site; a CC outside the
  chart went on.
- **Pitch** (a 440 Hz sine, all wet, by zero crossings): chromatic +7
  659.2 Hz (659.3), -12 219.8 (220.0), +24 1760.5 (1760.0), -24 109.8
  (110.0), +19 1318.8 (1318.5), unison 440.0, MICROTUNE at the top 670.8
  (+7 and 30 cents: 670.8). Diatonic, KEY C major, +3RD: A in 523.0 (C,
  523.3), E in 392.0 (G, 392.0), B in 587.0 (D, 587.3); -5TH from A 293.5
  (D, 293.7); KEY A minor, melodic and harmonic minor and minor pentatonic
  from A 522.8 (C); double harmonic, E +2ND 349.0 (F, 349.2); lydian
  pentatonic, C '+3' 369.8 (F#, 370.0). CORRECTION with a 445 Hz input
  (20 cents sharp), C major +3RD: NONE 529.2 (529.2), LOOSE 526.0, STRICT
  523.0 (523.3). The detected note reads A4 = 57.01 semitones over C0.
- **Level**: every interval from -2 to +2 octaves, both algorithms, holds
  the input's level within 0.1 dB from 50 ms on (a steady sine).
- **GLIDE**, pitch smoothing on and off (no step above a sine's own), T1's
  PITCH off (440 out, latched and held).
- **The delay** (an impulse; the repeats' times): SERIES, a voice on 1/2 of
  a 500 ms TIME, at 250 ms and then every 500 ms on both sides; three voices
  on 1/4, 1/2, 3/4 at 125, 250 and 375 ms, left, centre and right; DUAL every
  250 ms on its side; DUAL + CROSS alternating sides every 250 ms; PITCH
  FEEDBACK every TIME plus the division; HALF SPEED twice the times; FREE
  TIME; the micro division (21 ms); each RHYTHM pattern's three times.
- **PITCH FEEDBACK** (a pluck at 220 Hz, +7): repeats at 330, 494, 740,
  1110 and 1670 Hz, both algorithms.
- **Feedback at the top** in every mode, with plucks for 10 s: bounded
  (-22 to -14 dBFS rms, no growth). FREEZE (latched, held, in PFB and DUAL +
  CROSS) holds the level, then decays when let go. SWELL, latched and held;
  SPREAD; chords, noise; BYPASS with and without TRAILS; the CPU guard's
  trip (95% sustained).
- **CPU** on the emulator's cycle clock: with plucks on the inputs and the synths muted
  (the Digitone's own render about 17%), the whole render 43.4% of a block
  at the defaults (three voices, SMTH, SERIES, chromatic), 43.9% in a key,
  44.5% at the heaviest tried (PITCH FEEDBACK, SMTH, three voices, a key,
  feedback up; its worst block 47.7%), so DigiHedra itself takes about 27%.
  Closed, the stock render again.

## Not yet

- MIDI notes setting the voices (the Hedra tunes its voices to notes played
  on a keyboard): core-dn1 passes mods the CCs only.
- The expression pedal's morph between two settings, and the Hedra's 16
  presets (the project has room for eight).
- Which scale "Double Harmonic" is: the manual's chart names it without its
  notes; DigiHedra uses the double harmonic major (1, b2, 3, 4, 5, b6, 7).
- The Hedra's own pitch shifting and its exact delay routing are not
  published; PFB's repeat time (TIME plus the voice's division) is this
  mod's reading of "repeats recycled through the pitch shifters".

## Licence

GPL-2.0-or-later, as the rest of elekloader ([LICENSE](../../LICENSE)).
