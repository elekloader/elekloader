# USB audio in for the Octatrack

The Octatrack (MKI and MKII), OS 1.40C, takes a stream from a computer
over its own USB port in place of its inputs: UAC2, 44.1 kHz, 24-bit
samples in 4-byte subslots. It is part of the same USB audio device as USB
audio out (`mods/usb-audio-ot`), which it needs, and the host sizes what it
sends from what it reads, so there is no clock to set and no drift.

It needs:
- the Octatrack's core 0.6 (`mods/core-ot`: the `ot_settings` table);
- usb-ot 0.3 (`mods/usb-ot`, usb: the DSP feed);
- usb-audio 0.3 (`mods/usb-audio-ot`: the stream to the computer, which
  this one rides on);
- the DSP bus (`mods/dspbus-ot`, dspbus). **The DSP bus takes SPATIALIZER
  off both FX menus.** That is why this is a mod of its own: usb-audio
  without it keeps SPATIALIZER.

With the settings companion (`mods/settings-ot`) the setting is edited on
the unit; without it, it stays OFF.

## The setting

**USB AUDIO / IN**, in the MODS category of the MAIN MENU, under USB AUDIO's
LAYOUT. YES steps it, an encoder turns it.

| IN | the host's channels | packets (high speed) |
|---|---|---|
| `OFF` (the default) | none: USB audio out only, as without this mod | |
| `AB` | 1-2 in place of inputs A/B; C/D stay the jacks' | 96 bytes |
| `CD` | 1-2 in place of inputs C/D; A/B stay the jacks' | 96 bytes |
| `ABCD` | 1-4 in place of inputs A-D | 192 bytes |

- **It applies at the next power-up**, like LAYOUT: both are early card
  settings, read at main's end, before the USB controller's init composes
  the descriptors.
- **It needs a LAYOUT other than OFF.** The stream to the computer is the
  feedback the computer sizes its packets by; with LAYOUT OFF the unit has
  no USB audio at all.
- **Where the samples go.** Into the inputs' slots of the frame's RX block
  on DSP core 0, before anything reads it: whatever hears an input (its
  level, the tracks, the recorders, the mix, DIR) hears the computer.
- **While the computer's stream is closed** (or the cable is out, or before
  it starts), the inputs are the jacks'.

## How it works

- **USB.** usb-audio describes the stream when this mod's record says it
  is on (`usba_in`, `elekloader/sdk/include/octatrack/usbaudio.h`): a USB
  streaming input terminal and a line output terminal, a third interface
  (AudioStreaming, alt 0 and 1) with EP3 OUT, isochronous and asynchronous,
  and EP3 IN marked as its implicit feedback. It passes the third
  interface's SET_INTERFACE and GET_INTERFACE, a bus reset and a session
  end, and the vendor request 0xC0/0x56 here.
- **EP3 OUT.** Brought up when the computer asks for alt 1, at the port's
  speed: four transfers stay queued (the ChipIdea add-dTD procedure, as
  usb-audio's EP3 IN), and each completed one goes into a ring of 1,024
  frames and is queued again. Before the ring hands frames on it fills a
  cushion of 96 frames (2.2 ms; 160 at full speed, where a packet is 44
  frames); after a dry spell it fills it again, with the jacks in between.
- **To the DSP.** usb-ot's DSP feed (`ev_dsp_feed`) runs once a frame, after
  the frame's stock transfers to the DSP: the ring's next 16 frames go to
  DSP core 0 as one more transfer, in two parts, C/D then A/B, each a flag
  word and, when it is 1, the 16 samples of two channels as two 16-bit words
  each (the host port carries 16 bits a word): 96 words for a pair, 160 for
  four. While the stream is closed four blocks of flags 0 go out (the DSP's
  two banks alternate), then nothing.
- **On the DSP.** `inject.asm` (30 words in SPATIALIZER's place, the DSP
  bus's `ev_dsp_rx`) reads the block at `r6 + $320`, joins each sample from
  its two words with one `insert`, and writes it over the input's slot of
  the RX block. A part whose flag is 0 leaves its inputs alone.

## Counters

A vendor request (bmRequestType 0xC0, bRequest 0x56, wLength 60) returns 15
counters as big-endian longs, in octabam's order: produced, consumed, pkts,
lastn, lastfill, underruns, overruns, reprimes, bad, frames, seconds,
minfill, maxfill, err, partial. octabam's `tools/hw/usb_counters.py --in`
reads them. `frames` counts the feed's calls (every frame), `seconds` the
feeds completed (usb-ot's count). With the setting OFF the request STALLs.

## Conflicts

The USB IO files of irpina/octabam2elemod v1.0 do the same thing their own
way; they are refused beside usb-ot (and beside the DSP bus). Its claims are
`usb:ep3-out` and `dsp:feed`; its setting's identity on the card is the tag
`USBI`, key 1.

## Checked

In octabam's emulator (`ot_emu --usb-host --frame --dsp`, with its
`tools/harness/usb_host.py`). The bench plays an implicit-feedback host:
each OUT packet carries as many frames as the IN packet before it, and
every sample is coded with its channel and frame number. The setting's
default was changed in a copy of the mod for most runs. Those runs:

- **The descriptors**, at high and full speed and as OTHER_SPEED, for AB,
  CD and ABCD, beside layouts ALL and MAIN, and beside USB MIDI:
  - three interfaces under the association, and the AC header's 75 bytes;
  - the two terminals, and the second AS_GENERAL linking 0x13;
  - EP 0x03 at 96 or 192 bytes, bInterval 2 (360 or 720 bytes, bInterval
    1, at full speed);
  - EP 0x83's 0x25.
- **The requests**: GET_INTERFACE 0, then 1 after alt 1; alt 2 STALLs; the
  counters over 0x56 (and usb-audio's over 0x55).
- **The samples on the DSP**, with 8,000 polls at high speed and 2,000 at
  full speed:
  - the six completed RX blocks hold the host's channels on the setting's
    slots, bit-exact and in consecutive frames, and the other slots are
    untouched;
  - the recorder's input ring holds them too;
  - every packet was retired, whole, with no overrun, underrun or re-prime;
  - the cushion stayed at 70-117 frames at high speed and 158-208 at full
    speed;
  - a feed every frame, each completed.
- **Closing and opening**:
  - after alt 0, no feed, both DSP banks' flags clear, the RX blocks the
    jacks' again;
  - alt 1 again restarts the ring;
  - a bus reset with both streams open sets both alternate settings to 0
    and stops the feed, and the stream runs again after the next alt 1.
- **IN OFF, or no add-on**: the configuration is usb-audio 0.2's byte for
  byte, alone and beside USB MIDI, and LAYOUT OFF with IN AB is stock's.

Through the MODS menu: the IN row shows under USB AUDIO after LAYOUT, and
YES sets it (AB, ABCD). After a reboot with battery RAM carried over, the
unit enumerates with the IN interface at 2 or 4 channels and streams with
no error.

In that reboot the emulator's DSP phase was such that the RX blocks behind
the frame head held the jacks' (silent) input, not the host's. So the
samples on the DSP were checked in boots without battery RAM. The DSP tone
example (`examples/dsp-tone-ot`) shows the same with battery RAM carried
over and not without it. It is the emulator's frame phase, not this mod.

Not checked there:
- **The timing on a unit**: the feed's work at level 6 against the frame
  interrupt, and the cost of the extra transfer. The emulator runs the
  frame interrupt and the eDMA in lock-step.
- **The packet buffers in SDRAM, through the uncached alias.** octabam put
  its IN buffers in on-chip SRAM.
- **Real hosts' implicit feedback**, and full speed on any host.

Facts from sambanks/octabam's USB audio in (MIT; the four-channel form is
Bryan T's usbin-test): the transfer chain's state 7 and the transfer that
comes back to it, the host-port transfer and where it lands, the lengths it
may write, the hi/lo words, the RX block's slots, the implicit feedback, the
queue head's Mult, the cushion, the counters. The code is elekloader's own.
