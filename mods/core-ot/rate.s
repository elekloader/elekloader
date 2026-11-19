| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: a track's playback increment (core-ot 0.30).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The increment builder 0x40004008(track, ping, start, end) runs in the frame
| interrupt for STATIC, FLEX and PICKUP tracks. Either it works the increment out
| afresh (PTCH and RATE applied; stored at 36(a3), 0x40004104) or it reuses the
| stored one (0x40004072, `move.l 36(a3),d0 ; bra`). Both paths meet at 0x40004108
| with d0 = the increment (Q26, 0x04000000 = unity), which goes on to the DSP's
| voice command. ev_play_increment sees each track's increment there:
|
|   0x40004072  the reuse path: marks this frame as reused        (ev_play_increment)
|   0x40004108  both paths: handlers change the increment        ev_play_increment
|
| core keeps each track's stock increment, so on a reused frame handlers get
| stock's value again, not the one they returned; what they return is stored at
| 36(a3) and sent. The 8 bytes between, 0x40004100, are octabam REPITCH's (v1.0,
| in the shop) and Modwerk's ev_play_increment site: both stay free. The idea of
| the event is Modwerk's; the code is our own.

        .section .run, "ax"

        .globl  core_inc_reuse, core_inc
core_inc_reuse:
        move.l  36(%a3), %d0            | what the site replaced
        move.l  %d1, -(%sp)
        moveq   #1, %d1
        move.b  %d1, core_inc_reused
        move.l  (%sp)+, %d1
        jmp     0x40004108              | on to the meeting point (core_inc)

| 0x40004108 (was: movea.l 0x80001c80,a4). The track is 52(sp) of the builder's frame.
core_inc:
        tst.l   ev_play_increment
        beq.s   9f
        lea     -12(%sp), %sp
        movem.l %d1/%a0-%a1, (%sp)
        moveq   #0, %d1
        move.b  core_inc_reused, %d1
        clr.b   core_inc_reused
        move.l  %d1, -(%sp)             | reused
        move.l  %d0, -(%sp)             | the increment
        move.l  12 + 8 + 52(%sp), -(%sp)        | the track
        jsr     core_play_increment
        lea     12(%sp), %sp
        move.l  %d0, 36(%a3)            | stored, as the fresh path stores it
        movem.l (%sp), %d1/%a0-%a1
        lea     12(%sp), %sp
        movea.l 0x80001c80, %a4
        jmp     0x4000410e
9:      clr.b   core_inc_reused
        movea.l 0x80001c80, %a4
        jmp     0x4000410e

        .section .bss
core_inc_reused: .skip 2
