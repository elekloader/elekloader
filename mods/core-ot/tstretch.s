| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the time-stretch helper's tracks (core-ot 0.33).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The time-stretch helper is a task of its own (0x40098a5c, created by 0x40098a2c).
| Each pass walks the tracks (d3 = 0-7) and, for a track whose machine stretches,
| works its stretch out from 0x40098b2c on; 0x40098cc8 goes on to the next track.
| ev_tstretch asks first: int f(unsigned trk), nonzero = skip this track this pass
| (the first taker wins). Modwerk's base gates the same place on TSTR OFF; the
| code is our own.
|
|   0x40098b2c  movea.l (a3),a0 ; cmpa.l 188(sp),a0 ; beq.s 0x40098b5e    ev_tstretch
        .section .run, "ax"

        .globl  core_tstretch
core_tstretch:
        tst.l   ev_tstretch
        beq.s   2f
        lea     ev_tstretch, %a1
1:      move.l  (%a1)+, %d0
        beq.s   2f
        move.l  %a1, -(%sp)
        move.l  %d3, -(%sp)             | the track
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        movea.l (%sp)+, %a1
        tst.l   %d0
        beq.s   1b
        jmp     0x40098cc8              | taken: the next track
2:      movea.l (%a3), %a0              | what the site replaced
        cmpa.l  188(%sp), %a0
        beq.s   3f
        jmp     0x40098b34
3:      jmp     0x40098b5e
