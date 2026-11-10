# look-check (Analog Rytm MKII, core-ar2 0.13)

A test for core-ar2 0.13's knob looks (`core_knob_looks`, docs/ADAPTING.md
"Knob looks (core-ar2 0.13, Analog Rytm MKII)").

23 LOOK CHECK pages: 1..12 after the LFO key's stock page, 13..23 after the
AMP key's. Each has four pairs of knobs, A|B, C|D, E|F and G|H: on the left
a stock id L, on the right a knob id of the mod's own whose entry names L.
This mod's `core_params` answers both knobs of a pair with the same value
and asks stock to write L's text for both, and its `core_param_locks` names
the own knob with L's label and name, so the two cells of a pair are the
same pixel for pixel when core gives the look. The pairs take one stock id
of each of the 87 different UI records a stock 1.74 boot builds (`looks.h`):
every knob display stock has. The last page adds a knob whose record comes
from a `ui` callback (a copy of 457's) and one with COMP MIX's look and a
range of 0..12, whose dial core spans over 0..12: its stock partner shows
the value moved onto 0..127, so the pair is the same when core does.

While it is in, its `core_params` entry for 1..468 also answers the 87
stock ids on stock's own pages. It is a test, not for a project you keep.

## The pages

Which knob shows what. The own ids are the ones a fresh lockfile gives
(555 up, in the order of `looks.h`); a build whose lockfile already holds
other mods' knobs may have them elsewhere (its lockfile says). Written by
`page_table.py` from `looks.h` and mods/core-ar2/knob-looks-1.74.md.

| page | key | pair | stock id | own id | look (row of knob-looks-1.74.md), name (label) |
|---|---|---|---|---|---|
| 1 | LFO | A/B | 1 | 555 | 1, Error (ERR) |
| 1 | LFO | C/D | 29 | 556 | 2, Note (NOT) |
| 1 | LFO | E/F | 30 | 557 | 3, Velocity (VEL) |
| 1 | LFO | G/H | 31 | 558 | 4, Length (LEN) |
| 2 | LFO | A/B | 32 | 559 | 5, Micro Timing (uTM) |
| 2 | LFO | C/D | 33 | 560 | 6, Trig Condition (TRC) |
| 2 | LFO | E/F | 34 | 561 | 7, Synthesis Trig (SYN) |
| 2 | LFO | G/H | 38 | 562 | 8, Probability (PRB) |
| 3 | LFO | A/B | 39 | 563 | 9, Tune (TUN) |
| 3 | LFO | C/D | 41 | 564 | 10, Sample Slot (SMP) |
| 3 | LFO | E/F | 42 | 565 | 11, Bit Reduction (BR) |
| 3 | LFO | G/H | 43 | 566 | 12, Start (STA) |
| 4 | LFO | A/B | 45 | 567 | 13, Loop (LOP) |
| 4 | LFO | C/D | 46 | 568 | 14, Level (LEV) |
| 4 | LFO | E/F | 47 | 569 | 15, Frequency (FRQ) |
| 4 | LFO | G/H | 48 | 570 | 16, Resonance (RES) |
| 5 | LFO | A/B | 49 | 571 | 17, Filter Type (TYP) |
| 5 | LFO | C/D | 54 | 572 | 18, Env. Depth (ENV) |
| 5 | LFO | E/F | 57 | 573 | 19, Hold Time (HLD) |
| 5 | LFO | G/H | 58 | 574 | 20, Decay Time (DEC) |
| 6 | LFO | A/B | 60 | 575 | 21, Delay Send (DEL) |
| 6 | LFO | C/D | 62 | 576 | 22, Pan (PAN) |
| 6 | LFO | E/F | 64 | 577 | 23, Speed (SPD) |
| 6 | LFO | G/H | 65 | 578 | 24, Multiplier (MUL) |
| 7 | LFO | A/B | 66 | 579 | 25, Fade In/Out (FAD) |
| 7 | LFO | C/D | 67 | 580 | 26, Destination (DST) |
| 7 | LFO | E/F | 68 | 581 | 27, Waveform (WAV) |
| 7 | LFO | G/H | 69 | 582 | 28, Start Phase (SPH) |
| 8 | LFO | A/B | 70 | 583 | 29, Trig Mode (MOD) |
| 8 | LFO | C/D | 71 | 584 | 30, Depth (DEP) |
| 8 | LFO | E/F | 72 | 585 | 31, Delay Time (TIM) |
| 8 | LFO | G/H | 73 | 586 | 32, Pingpong (X) |
| 9 | LFO | A/B | 75 | 587 | 33, Feedback (FDB) |
| 9 | LFO | C/D | 76 | 588 | 34, HP Filter (HPF) |
| 9 | LFO | E/F | 78 | 589 | 35, Reverb Send (REV) |
| 9 | LFO | G/H | 80 | 590 | 36, Delay Overdr. (DOV) |
| 10 | LFO | A/B | 81 | 591 | 37, Del Dist/Comp (DEL) |
| 10 | LFO | C/D | 83 | 592 | 38, Decay Time (DEC) |
| 10 | LFO | E/F | 84 | 593 | 39, Shelving Freq (FRQ) |
| 10 | LFO | G/H | 95 | 594 | 40, Attack (ms) (ATK) |
| 11 | LFO | A/B | 96 | 595 | 41, Release (s) (REL) |
| 11 | LFO | C/D | 97 | 596 | 42, Sidechain EQ (SEQ) |
| 11 | LFO | E/F | 98 | 597 | 43, Comp. Ratio (RAT) |
| 11 | LFO | G/H | 100 | 598 | 44, Tune (TUN) |
| 12 | LFO | A/B | 106 | 599 | 45, Waveform (WAV) |
| 12 | LFO | C/D | 115 | 600 | 46, Transient (TRA) |
| 12 | LFO | E/F | 126 | 601 | 47, Detune (DET) |
| 12 | LFO | G/H | 159 | 602 | 48, Snap (SNP) |
| 13 | AMP | A/B | 190 | 603 | 49, Level (LEV) |
| 13 | AMP | C/D | 191 | 604 | 50, FM Amount (FMA) |
| 13 | AMP | E/F | 207 | 605 | 51, Mod Type (TYP) |
| 13 | AMP | G/H | 224 | 606 | 52, Osc Reset (RST) |
| 14 | AMP | A/B | 243 | 607 | 53, Sweep Depth (SWD) |
| 14 | AMP | C/D | 244 | 608 | 54, Decay (DEC) |
| 14 | AMP | E/F | 251 | 609 | 55, Repeat Count (NUM) |
| 14 | AMP | G/H | 252 | 610 | 56, Polarity (POL) |
| 15 | AMP | A/B | 267 | 611 | 57, Waveform (WAV) |
| 15 | AMP | C/D | 282 | 612 | 58, Cymbal Type (TYP) |
| 15 | AMP | E/F | 289 | 613 | 59, Osc 2 Detune (DET) |
| 15 | AMP | G/H | 290 | 614 | 60, Osc Config (CFG) |
| 16 | AMP | A/B | 291 | 615 | 61, Osc 2 Decay (DEC) |
| 16 | AMP | C/D | 294 | 616 | 62, Tune (TUN) |
| 16 | AMP | E/F | 295 | 617 | 63, Waveform (WAV) |
| 16 | AMP | G/H | 296 | 618 | 64, Speed (SPD) |
| 17 | AMP | A/B | 298 | 619 | 65, Offset2 (OF2) |
| 17 | AMP | C/D | 322 | 620 | 66, Osc 2 Detune (DET) |
| 17 | AMP | E/F | 323 | 621 | 67, Waveform 1 (WAV1) |
| 17 | AMP | G/H | 324 | 622 | 68, Waveform 2 (WAV2) |
| 18 | AMP | A/B | 327 | 623 | 69, Osc1 Freq (OSC1) |
| 18 | AMP | C/D | 328 | 624 | 70, Decay Time (DEC) |
| 18 | AMP | E/F | 382 | 625 | 71, Tune (TUN) |
| 18 | AMP | G/H | 401 | 626 | 72, Snap (SNP) |
| 19 | AMP | A/B | 403 | 627 | 73, Noise Level (NOL) |
| 19 | AMP | C/D | 406 | 628 | 74, Pulse Level (PLV) |
| 19 | AMP | E/F | 408 | 629 | 75, BD_TUNE1 (TU1) |
| 19 | AMP | G/H | 414 | 630 | 76, BD_FMOD_EN (FM) |
| 20 | AMP | A/B | 420 | 631 | 77, SAW1 SAW2 SINE2 (WAV) |
| 20 | AMP | C/D | 431 | 632 | 78, BT_DECAY (DEC) |
| 20 | AMP | E/F | 434 | 633 | 79, BT_BIT (BIT) |
| 20 | AMP | G/H | 444 | 634 | 80, HH_SELECT (SEL) |
| 21 | AMP | A/B | 450 | 635 | 81, CY_SELECT (SEL) |
| 21 | AMP | C/D | 457 | 636 | 82, CB_OSC_SELECT (SEL) |
| 21 | AMP | E/F | 461 | 637 | 83, Pulse Gen 1 (PL1) |
| 21 | AMP | G/H | 463 | 638 | 84, Euclidean Mode (EUC) |
| 22 | AMP | A/B | 464 | 639 | 85, Rotation Gen 1 (RO1) |
| 22 | AMP | C/D | 467 | 640 | 86, Bool Operator (OP) |
| 22 | AMP | E/F | 468 | 641 | 87, Track Length (LEN) |
| 22 | AMP | G/H | 457 | 642 | ui callback: a copy of 457's record (CB_OSC_SELECT (SEL)) |
| 23 | AMP | A/B | 94 | 643 | COMP MIX's look with min 0, max 12 (stock partner shows the value on 0..127) |

## The record

After the magic `LKC1` (the symbol `lk`): own knobs, the first own id,
ticks, then for each own knob what core's two lookups answer, read at the
first tick: `{int16 look, int16 ok, u32 uirec, u32 want, int32 range[3],
int32 want_range[2]}`; ok is 1 when uirec and the range are look's (the
callback's record, the entry's range).

`looks.h`, `pages.c` and `mod.json` are written by `gen_looks.py` from a dump
of a stock 1.74 boot's RAM 0x419a4000..0x419ae800 (analog-emu's dtpeek after
booting to the main screen); the dump is derived from the stock OS, so it is
not in the repository.
