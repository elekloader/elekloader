| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the start-up animation's frames (core-ot 0.37).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The UI task's start-up animation (0x4005596c) loops a frame at a time (d2 = the
| frame, d6 = the screen plane): it clears the plane and draws the logo and its
| text, flushes the screen (0x40055aa2) and steps the LED sweep. ev_boot_frame runs
| at each frame's start: int f(unsigned frame, void *plane), nonzero = the mod drew
| this frame (ot_blit on plane) and stock draws nothing of its own; the flush and
| the LEDs go on. Modwerk's base draws its own start-up art there; the code is our own.
|
|   0x400559c6  pea -1 ; clr.l -(sp) (the frame's first instructions)   ev_boot_frame
        .section .run, "ax"

        .globl  core_boot_frame
core_boot_frame:
        tst.l   ev_boot_frame
        beq.s   9f
        lea     ev_boot_frame, %a1
1:      move.l  (%a1)+, %d0
        beq.s   9f
        move.l  %a1, -(%sp)
        move.l  %d6, -(%sp)             | the plane
        move.l  %d2, -(%sp)             | the frame
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        movea.l (%sp)+, %a1
        tst.l   %d0
        beq.s   1b
        move.l  %d2, %d3                | taken: what stock's drawing leaves for the LEDs
        addi.l  #-192, %d3
        jmp     0x40055aa2              | the flush
9:      pea     -1                      | what the site replaced
        clr.l   -(%sp)
        jmp     0x400559cc
