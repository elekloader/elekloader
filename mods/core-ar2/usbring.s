| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.19: the USB-MIDI sender's early wrap (usbring.c), OS 1.74.
|
| 0x400029e0, stock's sender (len, msg), from the caller's task with the
| sender's mutex held: `lea -28(%sp),%sp; moveq #1,%d1`. Before stock's own
| code runs, core_usb_wrap_wait waits until the ring holds nothing unsent
| when this call would wrap early (README, "The USB-MIDI sender's wrap").
        .text
        .globl  core_usb_send_site
        .align  2

core_usb_send_site:
        move.l  4(%sp),%d0              | len
        moveq   #0x2f,%d1
        cmp.l   %d1,%d0
        ble.s   1f                      | len <= 0x2f: stock never wraps early
        move.l  0x413db25c,%d0          | the ring's next byte
        cmpi.l  #0xfbf,%d0
        ble.s   1f                      | the index is below the tail
        jsr     core_usb_wrap_wait      | d0, d1, a0, a1 only; stock sets d1 next
1:      lea     -28(%sp),%sp
        moveq   #1,%d1
        jmp     0x400029e6
