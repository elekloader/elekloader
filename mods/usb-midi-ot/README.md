# USB MIDI for the Octatrack

Class-compliant USB MIDI on the Octatrack's own USB port (MKI and MKII),
OS 1.40C: one MIDI port in and one out, beside mass storage. It mirrors the
DIN ports: what comes in over USB goes where DIN MIDI IN goes, and what the
Octatrack sends on DIN MIDI OUT it also sends over USB.

It is a USB function: it needs `mods/usb-ot` (usb 0.1) and the Octatrack's
core 0.4 (`mods/core-ot`, for its MIDI-out events). It combines with other
USB functions in the same build.

## What it does

- **The interfaces.** An AudioControl interface and a MIDIStreaming
  interface under an interface association (class 1), after mass storage:
  interfaces 1 and 2. EP2 bulk both ways, 512 bytes at high speed and 64 at
  full speed. Embedded jacks 1 (in) and 3 (out).
- **In.** Each USB-MIDI event packet's bytes go into the firmware's MIDI
  receive FIFO, the one the DIN port's interrupt fills, so the firmware
  handles them as DIN MIDI: notes, CCs, program changes, clock and
  transport. Before each event it makes room in the 32-byte FIFO (it runs
  the firmware's framer once when the FIFO is full), so a burst is not
  dropped. Each USB clock (0xF8) gets the timestamp the DIN interrupt gives
  it, which the firmware's clock handler measures the tempo from.
- **Out.** It subscribes to core-ot's `ev_midi_out` and `ev_midi_out_rt`:
  each message and realtime byte the firmware sends is queued and sent over
  EP2 IN, one transfer at a time, through the firmware's own USB-MIDI
  encoder. The queue holds 256 bytes; what does not fit while the host is not
  reading is dropped (`usbmidi_drops` counts it).
- **Replugs and resets.** A bus reset or a pulled cable takes EP2 down and
  drops what was queued for the old session; the next SET_CONFIGURATION
  brings it up again.

The stock firmware carries a dormant USB-MIDI half: EP2's bring-up, its
receive prime and its transmit encoder. This calls it, as markandrus's
octemu first did. What sambanks/octabam's `usb` module found (MIT) is in it
too: the 512-byte receive at high speed, the FIFO kept from overflowing, the
flush before EP2 comes up again, transmit stopped at a reset, and the clock
timestamp. The code is elekloader's own.

## Conflicts

The USB MIDI file of irpina/octabam2elemod v1.0 (`octabam-usb-midi`) and the
other v1.0 USB files are refused beside usb-ot. Use this one with usb-ot
instead.

## Checked

In octabam's emulator (`ot_emu --usb-host` with its
`tools/harness/usb_host.py`), at high and full speed: enumeration with the
composite device class and 132-byte configurations (OTHER_SPEED too), mass
storage still answering, GET_INTERFACE and SET_INTERFACE on interface 2
(alt 1 stalls), CLEAR_FEATURE(HALT) on EP 0x82; channel messages and a
clock in reach the firmware's MIDI FIFO (314 bytes, including a 400-byte
burst of 100 notes in one transfer); the firmware's `midi_send` and its
realtime sender reach EP2 IN (60 notes back to back, each once, in order);
no uninitialised queue head primed.
