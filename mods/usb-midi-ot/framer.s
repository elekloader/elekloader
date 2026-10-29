| SPDX-License-Identifier: GPL-2.0-or-later
| USB MIDI for the Octatrack: one pass of the firmware's MIDI framer
| (0x40092bf4, the handler of INTC source 36, which ends in rte), entered as
| its interrupt would be: an exception frame (format 4, vector 0x64, the
| current SR) over the return address, so its rte comes back here. It pops
| one byte of the 32-byte MIDI FIFO, parses it and posts what it completes.
| The framer runs at the USB interrupt's level, so it cannot run inside it
| on its own. C-callable: void usbmidi_run_framer(void).
| The eight instructions are those of octabam's usbmidi_rx_run_framer (sambanks/octabam,
| modules/usb/usbmidi_rx.s, MIT, (c) 2026 Sam Banks): the framer's rte leaves no other way in.
        .section .run, "ax"
        .globl  usbmidi_run_framer
usbmidi_run_framer:
        move.w  %sr, %d0
        mvz.w   %d0, %d0
        ori.l   #0x41900000, %d0
        lea     1f, %a0
        move.l  %a0, -(%sp)
        move.l  %d0, -(%sp)
        jmp     0x40092bf4
1:      rts
