| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.15: the USB input's gain, held by a mod (docs/ADAPTING.md,
| "USB input gain (core-ar2 0.15, Analog Rytm MKII)";
| elekloader/sdk/include/analog-rytm-mk2/usbin.h). Every address is OS
| 1.74's; nothing of stock's code or tables is copied.
|
| The audio interrupt (0x4011fb66) fetches 32 stereo frames of USB input
| and copies them into the input block, each sample shifted right by a
| step it looks up from AUDIO ROUTING's USB TO MAIN [dB] (its index at
| 0x8000ee7c, 0 the most cut .. 3 none, 6 dB apart). Both of its copy loops
| use the one shift, loaded once from that index: `move.l 0x8000ee7c,%d0`
| at 0x4011fc98 is the site. Core gives the interrupt the held step in
| %d0 while a mod holds one, and stock's setting otherwise; the global
| setting is never written. The next instruction sets the flags again, so
| only %d0 changes.
|
| The steps a mod may hold are stock's own four: "no cut" (3) is what
| stock does with the setting at its top, so holding it never mixes the
| input in louder than stock can.
|
| core_usb_in_gain(who, step) runs in the UI task; the interrupt reads
| one long, so it sees either the old step or the new one.

        .equ    USB_TO_MAIN, 0x8000ee7c

        .section .run, "ax"
        .globl  core_usb_in_site, core_usb_in_gain, core_usb_in_step, core_usb_in_holder
        .align  2

| 0x4011fc98, in the audio interrupt: stock's `move.l USB_TO_MAIN,%d0`.
core_usb_in_site:
        move.l  usb_in_held, %d0        | the step + 1, 0 while nobody holds it
        beq.s   1f
        subq.l  #1, %d0
        rts
1:      move.l  USB_TO_MAIN, %d0
        rts

| int32_t core_usb_in_gain(const void *who, int32_t step)
| step 0..3 holds it for who, -1 lets who's hold go. 0 done, -1 refused:
| another holds it, who is 0, or step is out of range.
core_usb_in_gain:
        move.l  4(%sp), %d1             | who
        beq.s   9f
        move.l  usb_in_holder, %d0
        beq.s   1f                      | nobody holds it
        cmp.l   %d0, %d1
        bne.s   9f                      | someone else does
1:      move.l  8(%sp), %d0             | step
        bmi.s   5f                      | a release
        moveq   #3, %d1
        cmp.l   %d1, %d0
        bgt.s   9f
        move.l  4(%sp), %d1
        move.l  %d1, usb_in_holder
        addq.l  #1, %d0
        move.l  %d0, usb_in_held
        moveq   #0, %d0
        rts
5:      moveq   #-1, %d1
        cmp.l   %d1, %d0
        bne.s   9f
        clr.l   usb_in_held             | the setting first, then the holder
        clr.l   usb_in_holder
        moveq   #0, %d0
        rts
9:      moveq   #-1, %d0
        rts

| int32_t core_usb_in_step(void): the step in force, held or stock's.
core_usb_in_step:
        jmp     core_usb_in_site

| const void *core_usb_in_holder(void): who holds it, 0 for nobody. A
| pointer comes back in %a0 (gcc's m68k ABI), and in %d0 as well.
core_usb_in_holder:
        move.l  usb_in_holder, %d0
        movea.l %d0, %a0
        rts

        .bss
        .align  2
usb_in_held:
        .space  4
usb_in_holder:
        .space  4
