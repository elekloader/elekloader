# USB for the Octatrack

The Octatrack's USB companion (MKI and MKII), OS 1.40C. Mods add USB
functions to the unit's USB port with it: USB MIDI (`mods/usb-midi-ot`),
and USB audio and others to come. Several functions combine in one build:
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
- **Two stock fixes** that only matter with a function in the build: the
  controller's bus priority on the SDRAM and SRAM slaves goes to USB (its
  16-byte RX FIFO loses packet tails under a busy project otherwise, as
  sambanks/octabam measured), and an EP0 reply that crosses a 4 KB page is
  sent whole.

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

18 places in the stock USB stack (0x4001d4b2-0x4001e952, listed in
mod.json), each whole instructions; `tests/test_octatrack.py` checks them
against the stock image. Its claim is `usb:stack`.

## Conflicts

Each USB file of irpina/octabam2elemod v1.0 (`octabam-usb-*`: USB MIDI, the
USB audio and USB IO mods, the crossbar) patches one of these places too, so
the linker refuses usb-ot beside any of them. A build takes either those
files or usb-ot and its functions.

## Checked

In octabam's emulator (`ot_emu --usb-host` with its
`tools/harness/usb_host.py`): usb-ot alone enumerates as stock at both
speeds, with every descriptor reply (device, configuration, OTHER_SPEED)
byte-identical to stock's, mass storage answering INQUIRY, and no EP0
stall.

Facts from sambanks/octabam's `usb` module and markandrus/octemu's USB MIDI
(both MIT); the code is elekloader's own.
