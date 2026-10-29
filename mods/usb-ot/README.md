# USB for the Octatrack

The Octatrack's USB companion (MKI and MKII), OS 1.40C. Mods add USB
functions to the unit's USB port with it: USB MIDI (`mods/usb-midi-ot`),
USB audio (`mods/usb-audio-ot`, its layout a setting, with audio in from
`mods/usb-audio-in-ot`) and others to come.
Several functions combine in one build:
each is a mod of its own. It needs the Octatrack's core (`mods/core-ot`,
0.3.1 or newer).

The stock firmware's USB device stack has one function, mass storage
(interface 0, EP1). This mod owns the places in that stack once, for every
function, the way core-ot owns the hook bus. On its own it changes nothing
the unit does: every reply is stock's, byte for byte.

## What it does

- **The functions.** A function contributes a pointer to its
  `struct ot_usb_function` to the `ot_usb_functions` table
  (`elekloader/sdk/include/octatrack/usb.h`). At the controller's init,
  usb-ot numbers their interfaces after the mass-storage one, in table
  order.
- **A function can be off for a boot (0.2).** At the controller's init,
  before the composition, usb-ot calls a function's `enabled()` (the record's
  last field, ABI 2). With 0 the function is left out: it gets no
  interfaces (the numbers go to the next function), no association, no
  descriptors, and none of the requests and completions. Without it, or with
  an ABI 1 record, a function is always on. A function reads a setting
  there: an early one (settings-ot) is set at `ev_boot_done`, before the
  controller's init.
- **The descriptors, composed at boot.** It writes four configuration
  descriptors: full speed, high speed, and each as OTHER_SPEED. Each is
  stock's header and mass-storage interface, then each function's interface
  association (when it asks for one) and its own descriptors. With an
  association in the build, the device descriptor's class becomes EF/02/01.
  GET_DESCRIPTOR serves usb-ot's copies through the uncached alias, with
  their real lengths (stock fixes 32).
- **The requests.** SET_INTERFACE, GET_INTERFACE, CLEAR_FEATURE(HALT) and
  the class and vendor requests stock does not answer go to the function
  whose interface or endpoint they name. Stock answers the rest.
- **The endpoints.** At SET_CONFIGURATION each function brings its
  endpoints up at the speed the port came up at. At a bus reset or a session
  end (the cable pulled) each takes them down. Endpoint completions go to
  the function that owns the endpoint, from the USB interrupt.
- **The DSP feed (0.3).** It also holds the last state of the frame
  interrupt's transfer chain to the DSP (state 7, 0x40004bc0, in eDMA
  channel 0's interrupt), and runs the event `ev_dsp_feed` there once a
  frame. A handler may send DSP core 0 one more block, up to 160 words,
  with `ot_dsp_feed`; it lands in the frame's bank at + $320, where a DSP bus
  handler reads it, and the frame interrupt is unmasked once it has. With no
  handler, or none that sends, state 7 is stock's. USB audio in is the first
  user; the feed is not USB's (docs/ADAPTING.md, "USB functions on the
  Octatrack", has the details).
- **Two stock fixes** that only matter with a function in the build: the
  controller's bus priority on the SDRAM and SRAM slaves goes to USB (its
  16-byte RX FIFO loses packet tails under a busy project otherwise, as
  sambanks/octabam measured), and an EP0 reply that crosses a 4 KB page is
  sent whole.
  - **The bus priority is set whenever any function is in the build**, USB
    MIDI alone included, though the measurement was of the receive path. It
    changes how the card's DMA and USB share the bus. octabam's
    FAILURE_MODES.md records one bank file written with a 64-byte burst
    missing on an image that had its USB crossbar module (the cause is
    inferred, one incident, not reproduced). Modwerk's USB MIDI and audio out
    do not set it and ran on a unit. Nothing here has been run on a unit
    with it set or unset to tell; it is left as it was.

## Writing a function

```json
"contribute": [{"to": "ot_usb_functions", "order": 20, "data": "00000000",
                "relocs": [[0, "abs32", "sym:my_function", 0]]}],
"requires": ["core", "usb"],
"resources": {"names": ["usb:ep3-in"]}
```

Claim each endpoint by name (`usb:ep2`, `usb:ep3-in`, `usb:ep3-out`): two
functions that want the same one are refused together. EP0 is usb-ot's and
EP1 is mass storage's. Memory the controller reads or writes (transfer
descriptors, buffers, replies) must be reached through
`OT_USB_UNCACHED(p)`. docs/ADAPTING.md, "USB functions on the Octatrack",
has the struct and the exports.

## The sites

18 places in the stock USB stack (0x4001d4b2-0x4001e952), and from 0.3 the
transfer chain's state 7 (0x40004bc0), listed in mod.json, each whole
instructions; `tests/test_octatrack.py` checks them against the stock image.
Its claim is `usb:stack`; a mod that feeds the DSP claims `dsp:feed`.

## Conflicts

Each USB file of irpina/octabam2elemod v1.0 (`octabam-usb-*`: USB MIDI, the
USB audio and USB IO mods, the crossbar) patches one of these places too, so
the linker refuses usb-ot beside any of them (the USB IO ones patch state 7
too). A build takes either those files or usb-ot and its functions.

## Checked

In octabam's emulator (`ot_emu --usb-host` with its
`tools/harness/usb_host.py`): usb-ot alone enumerates as stock at both
speeds, with every descriptor reply (device, configuration, OTHER_SPEED)
byte-identical to stock's, mass storage answering INQUIRY, and no EP0
stall.

A function whose `enabled()` returns 0 (USB audio with its layout OFF, set
in the MODS menu and read after a reboot) leaves the build as a build
without it: alone, usb-ot's configuration is stock's byte for byte; beside
USB MIDI (an ABI 1 record, always on) MIDI keeps its interfaces 1 and 2, with
no association or endpoint for audio.

With USB audio in (mods/usb-audio-in-ot, which has the details), the feed
runs once a frame while the host's stream is open, every feed completes
before the next frame interrupt, and with the stream closed state 7 runs as
stock's again after the closing blocks.

Facts from sambanks/octabam's `usb` module and markandrus/octemu's USB MIDI
(both MIT), and its USB audio in (state 7 and the transfer); the code is
elekloader's own.
