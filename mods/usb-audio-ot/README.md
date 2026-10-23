# USB audio for the Octatrack

The Octatrack (MKI and MKII), OS 1.40C, as a USB audio input on its own USB
port: UAC2, 44.1 kHz, 24-bit samples in 4-byte subslots, beside mass
storage. A computer records the unit's tracks, MAIN or CUE over the cable,
with no audio interface between.

It is a USB function: it needs `mods/usb-ot` (usb 0.2) and the Octatrack's
core 0.6 (`mods/core-ot`: `ev_frame_end` from 0.4, the `ot_settings` table
from 0.6). It combines with USB MIDI (`mods/usb-midi-ot`) in the same
build. With the settings companion (`mods/settings-ot`) in the build the
layout is edited on the unit; without it the layout stays the default.

## The layout is a setting

What the stream carries is one setting, **USB AUDIO / LAYOUT**, in the MODS
category of the MAIN MENU (PROJ, DOWN four times, RIGHT). YES steps it, an
encoder turns it. 0.1 had a mod for each layout; now one mod has them all.

| LAYOUT | high speed | full speed |
|---|---|---|
| `OFF` | no USB audio: usb-ot adds no interfaces | |
| `MAIN` | 2 channels: MAIN L/R | MAIN |
| `M+C` (MAIN+CUE) | 4 channels: MAIN L/R, CUE L/R | MAIN |
| `MSTR` (MASTER) | 2 channels: track 8 L/R | track 8 |
| `TRKS` (TRACKS) | 16 channels: tracks 1-8 L/R | the tracks' stereo sum |
| `ALL` (TRACKS+MAIN+CUE, the default) | 20 channels: tracks 1-8 L/R on 1-16, MAIN on 17-18, CUE on 19-20 | the tracks' stereo sum |

The menu's value column shows four characters, hence the short names.

- **It applies at the next power-up.** The configuration descriptors are
  composed once, at the USB controller's init, and a host reads them when it
  enumerates the unit. A change in MODS shows in the descriptors after the
  next boot.
- **Why early.** The setting is an early card setting (`OT_SET_CARD |
  OT_SET_EARLY`). The card's copy (`/ELEKLOADER/card.work`, the `SETC`
  block, for every project) is read after the project loads, which is after
  the controller's init. The early copy is kept in battery RAM (`SETE`) and
  settings-ot sets the variable from it at `ev_boot_done`, main's end, which
  comes before the init (in ot_emu by about 440,000 instructions). usb-ot's
  `enabled()` call at the init reads it there.
- **OFF.** The function is in the build but adds nothing: no interfaces, no
  association, no endpoint. Alone, the unit enumerates as stock (the
  configuration byte for byte, class 0); beside USB MIDI, MIDI keeps its
  interfaces 1 and 2.
- **Without settings-ot** the variable keeps its default, ALL.

The layouts, the taps and what each carries:

- **Tracks** are taken after each track's effects and before its level, the
  crossfader, MAIN's volume and the master effects. Track 8 is the master
  track's output with MASTER TRACK on.
- **MAIN and CUE** are the words the main and cue outputs get, so they
  include all of those.
- With tracks in the stream, MAIN and CUE are written one block (16 samples)
  behind the tracks they were mixed from, so they line up with them. With
  MASTER TRACK on, the firmware's CUE leads its MAIN by two blocks; CUE is
  written two blocks later to line up with MAIN. MAIN itself then sits two
  blocks behind the tracks.

## What it does

- **The interfaces.** A UAC2 AudioControl interface (a fixed 44.1 kHz clock,
  an input terminal, a USB streaming output terminal) and an AudioStreaming
  interface (alt 0 idle, alt 1 streaming), under an interface association
  (class 1, subclass 0, protocol 0x20). usb-ot numbers them after mass
  storage and USB MIDI's: interfaces 3 and 4 beside USB MIDI, 1 and 2
  without it.
- **The endpoint.** EP3 IN, isochronous, asynchronous. At high speed a
  packet every 250 us (bInterval 2), 11 or 12 frames, at most 12 frames
  (960 bytes with 20 channels); at full speed one every 1 ms, 44 or 45
  stereo frames (at most 360 bytes).
- **The clock.** It answers the clock source's sample-rate CUR (44100) and
  RANGE (one subrange, 44100), its validity CUR (valid), and a SET CUR of
  44100, which some hosts send (another rate STALLs).
- **The stream.** SET_INTERFACE alt 1 on the AudioStreaming interface starts
  it, alt 0 stops it, and so does a bus reset or a pulled cable. While no
  host has asked for it nothing runs.
- **The producer** runs on core-ot's `ev_frame_end`, once each frame
  interrupt (16 frames): it reads the frame's taps into a ring of 512 frames.
  Then it tops up a queue of four transfers from the ring.
- **The rate.** The unit's clock and the host's differ. A servo sends a
  little more or less than 11.025 (or 44.1) frames a packet so that the ring
  holds 64 frames.
- **The first poll sets the cushion.** A host starts polling some time
  after SET_INTERFACE (about 460 frames on macOS). The queue waits for that
  first poll; then the ring is set 64 frames deep, and the frames produced
  in between are skipped once (`anchor` in the counters).
- **The cost.** The ring and the packet buffers are sized for 20 channels in
  every layout: about 45 KB of `.bss` (8 of the reserve's 6 KB pages). 0.1's
  smaller layouts took less (MAIN alone about 6 KB). With OFF the producer
  still runs once a frame interrupt and returns at once.

## Counters

A vendor request (bmRequestType 0xC0, bRequest 0x55, wLength 60) returns 15
counters as big-endian longs, in octabam's order: consumed, acc, overruns,
underruns, lastn, lastfill, lastbank, bankdup, lastsamp, srcjump, reprimes,
minfill, maxfill, anchor, produced. octabam's `tools/harness/usb_host.py
counters` and `tools/hw/usb_counters.py` read them. lastsamp and srcjump
stay 0 here. An underrun is a packet the ring could not fill while nothing
else was queued; an overrun is the ring lapping a host that stopped reading.
With OFF the request is stock's: it STALLs.

## Conflicts

The USB AUDIO OUT and USB IO files of irpina/octabam2elemod v1.0, and every
other v1.0 USB file, are refused beside usb-ot. Use these with usb-ot
instead. Another mod's `ot_settings` entries and menu are settings-ot's; the
setting's identity on the card is the tag `USBA`, key 1.

## Checked

In octabam's emulator (`ot_emu --mkii --usb-host --frame`, with its
`tools/harness/usb_host.py`). The layout is set on the unit and read after a
reboot, as a user would:

1. A first boot from an empty card. The MODS menu is opened (PROJ, DOWN x4,
   RIGHT) and YES is pressed until LAYOUT shows the chosen value. 2 s later
   `card.work` holds the `SETC` block and battery RAM holds the `SETE`
   entry for `USBA`.
2. A second boot from that run's `--card-out`, with battery RAM carried over
   (`ot_emu` keeps none between runs: a copy of it loads 0x100f859c-0x10100000,
   core's claims and the stock settings block after them, once the RTOS is
   installed. The stock main clears all of SRAM when the settings block at
   0x100fff00 does not check out, which is what the emulator's zeroed SRAM
   looks like).
3. The bench enumerates the unit and runs octabam's audio checks, adapted
   to these images, for each layout alone and with USB MIDI (MAIN, M+C,
   MSTR, TRKS, ALL, each alone and beside MIDI; OFF alone and beside MIDI):
   - the descriptors: the composite class, the interfaces and the
     association, EP 0x83's type, size and interval, the channel count and
     format, OTHER_SPEED with the other speed's endpoint;
   - the clock's CUR, RANGE and validity; SET CUR 44100 ACKed and 48000
     stalled; GET_INTERFACE; SET_INTERFACE alt 1 bringing EP3 up and alt 0
     stopping it;
   - packets of 10 to 12 frames at high speed, and of 43 to 46 at full speed,
     none empty after the first ten; every subslot's low byte zero;
   - with the read-back arena and MAIN/CUE poked with words that name their
     source, each channel carries its own tap only; at full speed the pair is
     the tracks' sum (or track 8, or MAIN);
   - a host whose first poll comes 600 frames after alt 1 finds the cushion
     at 64 frames; 400 polls on, the fill stays within 64 of it with no
     underrun;
   - with USB MIDI: MIDI in and out on EP2 at both speeds, and while the
     stream runs;
   - no uninitialised queue head primed, and no EP0 stall but the ones asked
     for.
   With OFF: the configuration is stock's byte for byte (alone), or MIDI's
   interfaces 1 and 2 with no association for audio, no EP 0x83, and the
   vendor counters request STALLs.
4. Two controls. A card with `card.work` saying MAIN but no battery RAM
   enumerates with ALL (20 channels): the card's copy arrives after the
   descriptors are composed. Battery RAM saying MAIN with an empty card
   enumerates with MAIN (2 channels): the early copy is enough.
5. The order, as instruction counts in the second boot: `ev_boot_done` at
   42,491,825, the variable set to its default at 42,492,053 and to the early
   value (MAIN) at 42,492,405, the controller's init at 42,928,893.

The emulator runs the frame interrupt and the eDMA in lock-step, so it
cannot see the producer's timing against the eDMA's bank swap.

Facts from sambanks/octabam's USB audio modules (MIT; built on
markandrus/octemu's USB audio, MAIN and CUE by Bryan T): the taps, the
alignment of MAIN and CUE, the packet sizes, the servo, the queue and the
first poll's cushion, the clock replies. The code is elekloader's own.
