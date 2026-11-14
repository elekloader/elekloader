# DSP loader for the Octatrack

DSP code at run time (Octatrack MKI and MKII, OS 1.40C). A mod that isn't
flashed, a TELOS mod from the card (`mods/telos-ot`), hands its DSP code to
`ot_dsp_load` (`octatrack/dspload.h`). From the next audio frames on, the
code runs on DSP core 0 as an `ev_dsp_rx` handler (`mods/dspbus-ot`). It
needs the DSP bus.

The design is sambanks/octabam's demand loader (MIT,
`docs/contributing/DEMAND_LOADER.md`; its copier and its host transfer ran
on a unit). The code here is cut down to one kind of message.

## How it works

- **Where the code goes.** The DSPs' shared window (`0x30000..0x3ffff`, one
  memory for both cores, P, X and Y aliased). The slots are four pairs of
  words at `x:$33da3`; the code goes in the clear gap after them,
  `P:0x33dab..0x33fff`. That is 597 words for every loaded mod together,
  above the highest word any stock effect writes there with its default
  knobs (octabam's measure).
- **On the ColdFire** (`pump.c`):
  - `ot_dsp_load` places a blob in the gap, adds the gap address to the
    words that hold an address in the code, and stages it as halfwords with
    a sum;
  - on `ev_frame_begin`, only on frames whose ping word (`0x800000e0`) is 0,
    the pump sends 100 halfwords a frame to core 1's `X:0x2901..` with the
    stock host write's register sequence, then the doorbell `0xa55a` to
    `X:0x2900`;
  - every block goes three times, 30 quiet frames apart;
  - core 0 is never written (octabam: a transfer there stalls its chain).
- **On core 1** (`copier.asm`, the `ev_dsp_b` table at payload B's frame
  head, `P:0x76`):
  - when the doorbell is set, the copier clears the slot's BOUND word,
    copies the code into the window, and checks the sum;
  - only then does it write the entry and BOUND;
  - a repeat of what a slot already holds is consumed without a copy.
- **On core 0** (`run.asm`, an `ev_dsp_rx` entry, order 90): each bound
  slot's entry is called with `jsr`.

## What it takes

| what | where | how |
|---|---|---|
| core 1's SPATIALIZER, P:0x868-0x96c (261 words; 105 are the copier) | payload B | claimed as `dsp:harvest:SPATIALIZER-B` (the profile's second DSP area); its dispatch words X:0x21a and X:0x23a point at core 1's null stubs, P:0x588 and P:0x589, as dspbus-ot does on core 0 |
| payload B's frame head, P:0x76 (`move r6,x:>$207`) | site | `jsr >ev_dsp_b`, a DSP table that starts with the displaced instruction |
| core 1's X:0x2900..0x2db1 (staging) and X:0x28f0..0x28f3 (kept registers) | | unused by stock (octabam measured `X:0x2840..0x3f7e` untouched) |
| the window gap and its slots | | `dsp:window:gap`: one owner |

SPATIALIZER is already off both FX menus with the DSP bus. With this mod,
a project that has it on tracks 1-4 runs core 1's null stub there.

## Checked

In octabam's emulator (`ot_emu --dsp`, 300 frames), with TELOS loading
`research/telos-ot/dsptone` from the card:
- the pump sent 3 chunks and 3 doorbells on 150 quiet frames;
- slot 0 is bound on both cores, and the window holds the tone's 10 words,
  its loop's end address moved to `0x33db3`;
- core 0's input block (`X:0x8100`) holds the tone's ramp in input A's
  slot. Without the mod, the block holds zeros.

`tests/test_octatrack.py`, `test_the_dsp_loader`, reads the sites from the
stock payload. Not run on a unit.
