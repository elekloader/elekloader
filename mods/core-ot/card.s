| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the card's mount state and USB DISK MODE
| (core-ot 0.41). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The sys task sets the card up through 0x40061648(mode) (start-up unmount 0,
| the ROM filesystem with no card 2, card inserted 1, removed 0) and remounts it
| through 0x40061740 (FILE MANAGER's close, USB DISK MODE's end): ev_card runs
| after each, void f(unsigned state, int result, unsigned mode): state 0 none,
| 1 card, 2 the built-in ROM filesystem (0x460d1cb8); result 0 or -1; mode as
| above, 3 for the remount.
|
| USB DISK MODE does not unmount: once the banks are written (engine job 21's
| reply, 0x4007ebd4) the OS swaps its filesystem table for stubs and hands the
| card to the host; on leaving (every path closes through 0x4007ebf0) the host's
| access ends, the card is remounted and the project reloaded (0x4007ec18).
| ev_usb_disk(on) runs at the two: 1 before the host gets the card (stop touching
| it), 0 after the remount, before the project's reload.
|
|   0x40061734  movem.l (sp),d2/a2-a3 ; lea 12(sp),sp     ev_card (0x40061648's exit)
|   0x40061740  clr.l 0x460d1cb8                         ev_card (the remount)
|   0x4007ebd4  jsr 0x4006d4a8                           ev_usb_disk(1)
|   0x4007ec18  jsr 0x4004abcc                           ev_usb_disk(0)
        .section .run, "ax"

| d0: the result (kept); the mode at 16(sp).
        .globl  core_card_exit
core_card_exit:
        tst.l   ev_card
        beq.s   9f
        move.l  %d0, -(%sp)
        move.l  %d0, -(%sp)             | result
        move.l  16+8(%sp), -(%sp)       | mode
        jsr     core_card_c             | card.c
        addq.l  #8, %sp
        move.l  (%sp)+, %d0
9:      movem.l (%sp), %d2/%a2-%a3
        lea     12(%sp), %sp
        jmp     0x4006173c

| The remount takes no arguments: its body is called, then the event, then back.
        .globl  core_card_remount
core_card_remount:
        clr.l   0x460d1cb8
        jsr     0x40061746
        tst.l   ev_card
        beq.s   9f
        move.l  %d0, -(%sp)
        move.l  %d0, -(%sp)             | result
        pea     3                       | mode: the remount
        jsr     core_card_c
        addq.l  #8, %sp
        move.l  (%sp)+, %d0
9:      rts

        .globl  core_usb_on, core_usb_off
core_usb_on:
        pea     1
        jsr     core_usb_c
        addq.l  #4, %sp
        jsr     0x4006d4a8
        jmp     0x4007ebda
core_usb_off:
        clr.l   -(%sp)
        jsr     core_usb_c
        addq.l  #4, %sp
        jsr     0x4004abcc
        jmp     0x4007ec1e
