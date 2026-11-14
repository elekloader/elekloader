| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the START a track's trig calls (core-ot 0.38).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| In the frame interrupt a track's trig picks its machine type's START from the
| table 0x400d6454 (FLEX/STATIC/PICKUP's binder 0x4000f450, THRU's, NEIGHBOR's)
| and calls it start(trk, arg, flags) at 0x4000d49c (trig-events-ot's veto sits at
| 0x4000d496, after this). ev_track_start runs first: void *f(unsigned trk,
| unsigned type, void *stock) returns the START to call instead, or 0 for the next
| handler (the first nonzero wins; all 0 = stock's). Modwerk's base picks its
| machines' STARTs there (ot_machine_starts); the code is our own.
|
|   0x4000d47e  movea.l (a1,d4.l*4),a0 ; tst.l a0      ev_track_start
        .section .run, "ax"

        .globl  core_track_start
core_track_start:
        movea.l (%a1,%d4.l*4), %a0      | stock's START (what the site replaced)
        tst.l   ev_track_start
        beq.s   9f
        lea     -12(%sp), %sp
        movem.l %d0-%d1/%a1, (%sp)      | the flags in d0 go on to the call
        lea     ev_track_start, %a1
1:      move.l  (%a1)+, %d0
        beq.s   8f
        move.l  %a1, -(%sp)
        move.l  %a0, -(%sp)             | stock's START (kept: a0 is free in C)
        move.l  %a0, -(%sp)             | stock
        move.l  %d4, -(%sp)             | the type
        move.l  %d3, -(%sp)             | the track
        movea.l %d0, %a1
        jsr     (%a1)
        lea     12(%sp), %sp
        movea.l (%sp)+, %a0             | stock's again
        movea.l (%sp)+, %a1
        tst.l   %d0
        beq.s   1b
        movea.l %d0, %a0                | a handler's START
8:      movem.l (%sp), %d0-%d1/%a1
        lea     12(%sp), %sp
9:      tst.l   %a0
        beq.s   7f
        jmp     0x4000d486
7:      jmp     0x4000d4d2
