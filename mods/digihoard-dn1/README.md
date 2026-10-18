# DigiHoard

An echo collector for the Digitone mk1's audio inputs: a delay with a
memory. Everything you play goes into a 3-minute memory, always, and the
echo plays from it: SCAN reaches back into the memory (by itself, now and
then, or where you put it), SPREAD adds a second echo further back, six
modifiers bend the echoes after the delay, inside its feedback or round the
whole memory, and COLLECT turns the memory into an overdubbing song
sketchpad. With LOOP, CLEAR, RAMP and bounce and MIDI CCs. DigiHoard is an
elekloader mod on fxkit (core-dn1 4.1).

Its MIDI CCs follow the Chase Bliss Habit's published CC chart, so a controller set up for that pedal works with it too. DigiHoard is not affiliated with Chase Bliss and uses none of its code.

DIGIHOARD is an entry in core-dn1's Mod Menu (hold a track key). While it is the
active effect, DigiHoard owns the audio engine, as DigiSwarm and DigiPrism do
(fxkit's `core_audio` owner): the synths are silent, the Digitone's own
effects are off, and what you hear is the inputs through the pedal. NO turns
it off and gives the audio and the screen back. The sequencer keeps running, so PLAY, STOP
and TEMPO still work and SIZE can follow the Digitone's tempo. Opening the
page steps the pedal on.

## 0.2: on fxkit

From 0.2 DigiHoard is an effect on fxkit ([../fxkit-dn1](../fxkit-dn1)), as every
effect in the build is: one at a time is active. Its Mod Menu entry makes it
the active effect and opens its page; while it is active it owns the audio,
page or no page, and opening another effect's entry hands that one the audio.

| key | on DigiHoard's page |
|---|---|
| SYN1, SYN2, FLTR | its three pages (page 1, 2 and 3) |
| AMP | the MIXER: IN L, IN R (the inputs' levels, as the stock mixer's: 32 unity, up to +24 dB), L PAN, R PAN (where each input sits), BAL, VOL, WDTH (the output's width) |
| LFO | LFO 1, pressed again LFO 2: the stock LFO page (its knobs, WAVE and SPH as one wave, a turned knob's value under it), at the stock rates; turning DEST opens its list (DigiHoard's controls and the mixer's), YES picks, NO closes |
| RECORD | LOCK mode: hold a trig and turn a control, and that step has a p-lock of it (the title bar shows the 16 steps, filled where DigiHoard has a lock); FUNC + a trig clears the step; PAGE the next 16 steps. RECORD again (or NO) leaves it |
| MIDI | the stock UI, for the MIDI tracks and the rest, with DigiHoard playing on; its Mod Menu entry brings the page back |
| NO | DigiHoard off: the synths again |

The p-locks are the pattern's: they are kept in one synth track's lock table
(SETUP's LOCK TRACK, T4 unless you pick another), saved and copied with the
pattern, and play on their steps; that track should play nothing of its own
(no notes, no arpeggiator) in a pattern with DigiHoard's locks, or its trigs go astray. 0.1's presets are gone: the
project keeps DigiHoard's controls, mixer and LFOs (tag `DHAB`, 52 bytes).

## How it works

```
INPUT -> MEMORY (3 minutes) -> primary head (SCAN)    -> ECHO (SIZE, REPEATS, MOD) -> left
                            -> secondary head (SPREAD) -> ECHO                      -> right
         ^ COLLECT: the memory overdubs itself        ^ FEED: the echoes go back into the memory
```

- **The memory** records the inputs (as mono) all the time the pedal is on.
  Bypassing dumps it, unless COLLECT or ALWAYS is on:
  with COLLECT bypassing pauses it, with ALWAYS it keeps recording while
  bypassed. The left column shows how full it is (a bar from 0 to 3
  minutes) and where the heads play in it (marks from the bottom, now, to
  the top, 3 minutes ago).
- **SCAN** places the primary head. AUTO (the default): the knob sets how
  often the head wanders off into the past and how far it may go; it plays
  there for a few periods of SIZE (STAY) and comes back. At the top it is
  always away. MANUAL: the knob picks the moment, from now (left) to 3
  minutes ago (right), like a pre-delay. Either way the distance is a whole
  number of SIZEs, so what comes back stays in time, and a head travelling
  through the memory scrubs: a cloudy scanning sound (GLIDE sets
  how fast; a long way it jumps, crossfaded, and scrubs the last quarter
  second). Nothing comes back from where the memory is still empty.
- **SPREAD** adds the secondary head, behind the primary by the knob's
  distance (off at the left, 50 ms to 3 minutes, in quarters of SIZE). Short,
  it makes multi-tap patterns; long, it plays what you did a minute ago
  beside what you play now. The primary echo moves left and the secondary
  right as SPREAD comes in (WIDTH sets how far).
- **The echo**: SIZE is the time between echoes (50 ms to 2.5 s free; with
  the clock on, the Digitone's tempo divided, 1/32 to a bar); a change jumps
  to the new time with a short crossfade, without the pitch bending.
  REPEATS is the feedback, holding at the top (an endless build-up, kept
  under full scale). VOLUME is the echoes' level and MIX the balance: up to
  the middle the dry stays whole and the echoes come in; past it the dry
  goes, to fully wet at the top.
- **MODE** (IN, OUT, FEED): OUT applies the modifier to
  what the echo plays, the same on every echo; IN puts it inside the
  feedback, so it accumulates echo by echo; FEED sends the echoes back into
  the memory, so REPEATS is the whole loop's feedback, everything you do is
  recorded, and SPREAD turns into a classic multi-tap.

## The modifiers

Bank A or B and number 1-3 (or trig 1-6); MOD is the
amount, neutral at noon, with a different behaviour each side.

| | Left of noon | Right of noon |
|---|---|---|
| A1 STEPPED SPEED | 1/2x, then reverse at 1/2x, 1x, 2x, 4x | faster: 3/2x, 2x, 3x, 4x (fifths and octaves) |
| A2 STABILITY | wow, flutter and a closing low-pass: worn tape | the same, with tape hiss |
| A3 TRIMMER | cuts the end of each echo, in eighths of SIZE | cuts the start |
| B1 SMOOTH SPEED | slower, through a stop, to reverse 2x | faster, to 2x (detune near noon) |
| B2 FILTER | a resonant low-pass, 8 kHz down to 125 Hz | a resonant high-pass, 40 Hz up to 2 kHz |
| B3 DROPPER | random drops, more and shorter as MOD grows | a pattern of drops, the same in every echo |

In IN and FEED the filter's resonance can pile up into oscillation; it
stays under full scale.

## The page

Drawn like the Digitone's own parameter pages: the tempo in the box at the
top left, the page in the title bar (or the state: Loop, Scan, Memory
cleared, Wipe, Bypassed: kept or recording), the memory and the heads on the
left with the modifier's bank and number, the output's meter, and eight
controls. While you turn a knob the title bar shows its value (SIZE as a
division or in ms, SCAN and SPREAD as where their heads are, in seconds
back, MOD as the speed for A1).
LEFT and RIGHT, or PAGE, move between the pages; FUNC held shows page 2;
pushing a knob sets it back to its default.

| Page | Knobs A-D | Knobs E-H |
|---|---|---|
| 1 Hoard | VOL (the echoes' level), RPTS (REPEATS), SIZE, MOD | SPRD (SPREAD), SCAN, RAMP (its speed), MIX |
| 2 Secondary | WIDE (the echoes' placement), KEEP (how much COLLECT keeps of the old), MDRY (the memory dry's level), OUT | TONE (darkens the feedback), GLID (how fast the heads travel), STAY (how long AUTO stays away), INPT (the level into the memory) |
| 3 Switches | BANK (A, OFF, B), NUM (1-3), MODE (IN, OUT, FEED), SCAN (AUTO, MAN) | MEM (NORM, CLCT, ALWS, C+A), LOOP (MOM, LTCH), RAMP (which knob: OFF, RPTS, SIZE, MOD, SPRD, SCAN), SHAP |

SETUP (YES) adds two rows after the shell's: MEM DRY (SCANNING: the heads
are heard directly while they move; OFF; ON) and WIPE (the echo muted, the
memory alone).

**RAMP** moves the knob page 3 names. SHAP: UP and DOWN ramp once, from the
bottom or the top to where the knob is, each time the pedal steps on; BNC+
bounces between the knob and the top, BNC- between the knob and the bottom,
RAND jumps to random places between the knob and the top, SQR alternates
the two. RAMP is the speed: a cycle of 50 s to 0.2 s.

| Function | On DigiHoard |
|---|---|
| Tap tempo | T1 tapped: the tempo (SIZE follows it through its division; or trig 15) |
| Scan, as SCAN at its top | T1 held |
| Bypass | T2 tapped (or trig 16) |
| LOOP (with LATCH it stays) | T2 held; with LOOP on LTCH a hold turns it on and the next off |
| CLEAR the memory | T3 (the memory and the echoes) |
| The modifier | page 3, trig 1-3 (A1-A3), 4-6 (B1-B3), 7 (off); T4 tapped: the modifiers off and back |
| IN, OUT, FEED | page 3, trig 8-10 |
| MANUAL, COLLECT, ALWAYS, LATCH | page 3, trig 11-14 |
| WIPE, MEMORY DRY | SETUP; T4 held: WIPE while held |
| Fully wet | MIX at the top |
| Trails | SETUP's BYPASS: TRAILS or CUT |
| Ramping | page 3's RAMP and SHAP, the RAMP knob |
| Presets | none from 0.2: the project keeps the pedal, and p-locks (RECORD) follow the pattern |

## MIDI

DigiHoard takes its MIDI CCs (the chart below) that come on the
Digitone's auto channel (MIDI CONFIG > CHANNELS > AUTO CHANNEL), through
core-dn1's `ev_midi_cc`, while its page is open; with SETUP's CC CLOSED on,
also while it is closed, and with MIDI CC on ANY CH on a track's channel
too. The Digitone does not apply a CC DigiHoard takes; every other CC goes
on to the Digitone. MIDI CONFIG > PORT CONFIG > INPUT FROM has to let them
in.

| CC | | CC | |
|---|---|---|---|
| 14 | VOLUME | 21 | MOD NUMBER: 1, 2, 3 |
| 15 | REPEATS | 22 | MOD BANK: 1 A, 2 OFF, 3 B |
| 16 | SIZE | 23 | MODE: 1 IN, 2 OUT, 3 FEED |
| 17 | MOD | 24 | LOOP (right hold): 0 off, 1-127 on |
| 18 | SPREAD | 25 | SCAN (left hold): 0 off, 1-127 on |
| 19 | SCAN | 26 | CLEAR (both held): any value |
| 20 | RAMP | 51 | MIDI clock: 0 ignore (CLOCK FREE), 1-127 listen (DIGITONE) |
| 93 | TAP | 100 | EXPRESSION: the ramped knob's place between its two ends |
| 102 | 0 bypass, 1-127 on | 27, 28, 29 | DigiHoard's own: MIX; SCAN AUTO (0-63) or MANUAL; COLLECT (from 64) |

For 21-23 the values 0-3 are the three positions; larger values sweep the
three in thirds. Program changes (presets) are not mapped:
core-dn1 passes mods the CCs only.

## How it is built

The render (`dsp.c`) runs at interrupt level in core-dn1's render, on each
block of 32 frames: the memory's writing, the two heads, the two echo lines
and the modifier, the placement, MIX, OUTPUT LEVEL and a soft clip.
Everything is fixed point in C. The UI (`ui.c`) runs in the Digitone's UI
task, on the pedal shell the GAS pedals share.

- **Memory:** the region 0x46200000-0x47300000 of the Digitone profile's
  `bulk` area (17 MB): the two echo lines (2.7 s each, 512 KB) and the
  memory (3 minutes of 16-bit mono at 48 kHz, 17.28 MB). Only the lines are
  cleared when the Digitone starts (two UI ticks); the memory is never
  cleared: the render counts how much of it it has written since the start
  or the last CLEAR and plays silence beyond that, so CLEAR is instant. The
  code and its state take 24.6 KB of the 128 KB mods share (the render is
  compiled for size, and its knob curves are worked out, not tabled).
- **CPU:** in digikit's emulator, with the factory pattern playing. With the
  synths muted the Digitone's own render is about 17% of each block, and
  the whole render with DigiHoard open is 30% at the defaults (DigiHoard
  about 13%), and 38% at the heaviest settings tried (B1's two speed grains
  in IN, SPREAD, SCAN on AUTO at its top with the heads travelling and the
  memory dry heard: DigiHoard about 21%, 41% for the whole render at its
  worst block); A2 and B2 in the same setting 38%. The render is compiled
  for size. The emulator's timing tables assume memory without wait
  states, so a unit runs slower: the shell's guard times the whole render
  on DMA timer 0 every block, and if the unit stays over 93% for 0.2 s it
  bypasses the pedal at once and says so in the title ("CPU full:
  bypassed"); BYPASS turns it back on.
- **Project data:** DigiHoard's controls, its latches and SETUP, with fxkit's mixer and LFOs
  (`fx_pack`), are saved with the project (tag `DHAB`, 52 bytes; mods share 480 there,
  and the seven effects, GAIN and fxkit take about 470). The p-locks are the
  pattern's. The memory is not saved: the Digitone's +Drive has no files for mods.

## Build

With the SDK and a stock OS file:

```bash
python -m elekloader.sdk.build mods/digihoard-dn1 --stock Digitone_and_Digitone_Keys_OS1.43.syx   # out/digihoard-0.2.elemod
python -m elekloader.sdk.build mods/digihoard-dn1 --stock Digitone_and_Digitone_Keys_OS1.44.syx   # out/digihoard-0.2-os1.44.elemod
```

It names no firmware address, so its 1.44 port is empty. It needs fxkit and core-dn1
4.1 (`"requires": ["core", "fxkit"]`, `"resources": {"core": "4.1"}`) and an elekloader with the profile's
`bulk` area. `gentables.py` makes `tables.h`.

## Checked

In digikit's emulator, OS 1.43 and 1.44, and with the render's own code
compiled for a computer and fed test signals (`tests.sh`):

- **On a unit:** tested on a Digitone mk1 by its author: it loads and works
  as expected. It is still a beta.

- **The firmware.** A build with the released core-dn1 3.2 and DigiHoard
  passes every stage of the emulator's check against stock on 1.43 and
  1.44, with every screen identical.
- **The page, the keys, MIDI** (1.43): opened from the Mod Menu, the
  plucks on the inputs come through and the synths are silent; after NO the
  synths and the stock render again. The three pages, SETUP, a knob turn, a
  preset saved and recalled, T1-T4 tapped and held (T2 held: LOOP; T1 held:
  SCAN at its top; T3: memory cleared), the trig keys' modifiers and
  modes, BYPASS, and every CC in the chart above, taken on the auto channel
  and set as the chart says, while a CC outside it goes on to the Digitone.
- **The sound** (the render on a computer): the first echo at SIZE (359 ms
  free, 375 ms for 1/8D at 120 BPM) and the repeats falling by REPEATS;
  REPEATS and FEED at the top, and the filter's resonance in IN, bounded
  under full scale; STEPPED SPEED's 2x, reverse and 1/2x and SMOOTH
  SPEED's 2x and noon measured at 880, 440 and 221 Hz from 440 Hz, and
  each modifier at both ends in OUT and IN; SPREAD's second echo a quarter
  SIZE's step behind, on the right, and far back; SCAN MANUAL's echo 1.08 s
  (three SIZEs) late, and a burst heard again 177 s later from nearly 3
  minutes back; COLLECT bringing a burst back 180 s later, and nothing
  without it; ALWAYS keeping what came in while bypassed, NORM dumping it;
  LOOP holding the echo steady, and draining when released; CLEAR silencing
  at once; WIPE; the trails and their cut; every RAMP shape and the
  expression CC; no clicks when SIZE changes, the heads jump or the
  modifier switches (the largest step at a SIZE change 478 against the test
  sine's own 474); the CPU guard.

## Not done (yet)

- **SIZE up to a minute.** The 3-minute memory fills the region, so the echo
  lines hold 2.7 s and SIZE stops at 2.5 s. Longer echoes come from the
  memory itself: FEED with SCAN on MANUAL makes a loop as long as the
  distance back plus SIZE, up to 3 minutes.
- **Several knobs ramped at once**, a polarity switch for the ramp, CV and an
  expression pedal on any knob: here RAMP moves one knob (the expression CC
  takes the ramp's place), and the level keeps its own knob instead of
  turning into RAMP's speed.
- **122 presets and program changes**, a preset switch, a series option
  for fully wet, and the trails' finer points (re-engaging while the trails ring out
  always clears them here, except with COLLECT or ALWAYS).
- **The exact curves.** The modifiers' ranges, the speed steps (noon is 1x
  here, not a stop), AUTO's odds and how far it reaches, and SPREAD's steps
  are worked out from written descriptions, not measured.

## Licence

GPL-2.0-or-later, as the rest of elekloader ([LICENSE](../../LICENSE)).
