# USB audio for the Octatrack

The Octatrack (MKI and MKII), OS 1.40C, as a USB audio input on its own USB
port: UAC2, 44.1 kHz, 24-bit samples in 4-byte subslots, beside mass
storage. A computer records the unit's tracks, MAIN or CUE over the cable,
with no audio interface between.

It is a USB function: it needs `mods/usb-ot` (usb 0.1) and the Octatrack's
core 0.4 (`mods/core-ot`, for `ev_frame_end`). It combines with USB MIDI
(`mods/usb-midi-ot`) in the same build.

## The layouts

One source, five mods; a build takes one of them (they share EP3 IN, so the
linker refuses two together):

| mod folder | id | high speed | full speed |
|---|---|---|---|
| `usb-audio-ot` | `usb-audio` | 20 channels: tracks 1-8 L/R on 1-16, MAIN on 17-18, CUE on 19-20 | the tracks' stereo sum |
| `usb-audio-tracks-ot` | `usb-audio-tracks` | 16 channels: tracks 1-8 L/R | the tracks' stereo sum |
| `usb-audio-main-cue-ot` | `usb-audio-main-cue` | 4 channels: MAIN L/R, CUE L/R | MAIN |
| `usb-audio-main-ot` | `usb-audio-main` | 2 channels: MAIN | MAIN |
| `usb-audio-master-ot` | `usb-audio-master` | 2 channels: track 8 | track 8 |

The other four folders hold only a `mod.json`: they build
`usb-audio-ot/usbaudio.c` with another `USBA_LAYOUT`.

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

## Counters

A vendor request (bmRequestType 0xC0, bRequest 0x55, wLength 60) returns 15
counters as big-endian longs, in octabam's order: consumed, acc, overruns,
underruns, lastn, lastfill, lastbank, bankdup, lastsamp, srcjump, reprimes,
minfill, maxfill, anchor, produced. octabam's `tools/harness/usb_host.py
counters` and `tools/hw/usb_counters.py` read them. lastsamp and srcjump
stay 0 here. An underrun is a packet the ring could not fill while nothing
else was queued; an overrun is the ring lapping a host that stopped reading.

## Conflicts

The USB AUDIO OUT and USB IO files of irpina/octabam2elemod v1.0, and every
other v1.0 USB file, are refused beside usb-ot. Use these with usb-ot
instead. The five layouts are refused with each other (`usb:ep3-in`).

## Checked

In octabam's emulator (`ot_emu --usb-host --frame` with its
`tools/harness/usb_host.py`), each layout alone and with USB MIDI, the
checks of octabam's `tools/verify/verify_usb.py` adapted to these images:

- enumeration at high and full speed: the composite device class, the
  interfaces and the association, EP 0x83's type, size and interval, the
  channel count and format, OTHER_SPEED with the other speed's endpoint;
- the clock's CUR, RANGE and validity; SET CUR 44100 ACKed and 48000
  stalled; GET_INTERFACE; SET_INTERFACE alt 1 bringing EP3 up and alt 0
  stopping it;
- packets of 10 to 12 frames at high speed, and of 43 to 46 at full speed,
  none empty after the first ten; every subslot's low byte zero;
- with the read-back arena and MAIN/CUE poked with words that name their
  source, each channel carries its own tap only; at full speed the pair is
  the tracks' sum (or track 8, or MAIN);
- a host whose first poll comes 600 frames after alt 1 finds the cushion at
  64 frames; 400 polls on, the fill stays within 64 of it with no underrun;
- with USB MIDI: MIDI in and out on EP2 at both speeds, and while the stream
  runs;
- no uninitialised queue head primed, and no EP0 stall but the ones asked
  for.

The emulator runs the frame interrupt and the eDMA in lock-step, so it
cannot see the producer's timing against the eDMA's bank swap.

Facts from sambanks/octabam's USB audio modules (MIT; built on
markandrus/octemu's USB audio, MAIN and CUE by Bryan T): the taps, the
alignment of MAIN and CUE, the packet sizes, the servo, the queue and the
first poll's cushion, the clock replies. The code is elekloader's own.
