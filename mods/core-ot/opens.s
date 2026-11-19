| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: what a double-tapped track and a SETUP key open
| (core-ot 0.35). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| int ev_track_double(unsigned trk): the current track's TRACK key tapped twice
| (0x40083ab4, kind 3). Stock opens the machine pool on an audio track and does
| nothing in MIDI mode; nonzero = taken (first taker), stock does nothing.
| int ev_setup_open(unsigned kind): a page's SETUP window is about to open
| (0x40032228(kind), 0-4: SRC/NOTE, LFO, AMP/ARP, FX1/CTRL1, FX2/CTRL2; from FUNC
| + the page key, a page key tapped twice, and the reopen on a page change);
| nonzero = taken, stock opens nothing. Modwerk's MIDI generators open their pages
| from these two places; the code is our own.
|
|   0x40083afc  jsr 0x40033970 (is it MIDI mode?), d2 = the track       ev_track_double
|   0x40032228  move.l 4(sp),d0 ; tst.l 0x400c0c48 (the opener's entry)  ev_setup_open
        .section .run, "ax"

| Walk a table of int f(arg): d0 = the first nonzero answer, or 0. a1 and d1 are free.
        .macro  TAKE table, arg
        moveq   #0, %d0
        lea     \table, %a1
1:      move.l  (%a1)+, %d1
        beq.s   2f
        move.l  %a1, -(%sp)
        move.l  \arg, -(%sp)
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        movea.l (%sp)+, %a1
        tst.l   %d0
        beq.s   1b
2:
        .endm

        .globl  core_track_double, core_setup_open
core_track_double:
        tst.l   ev_track_double
        beq.s   9f
        TAKE    ev_track_double, %d2
        tst.l   %d0
        beq.s   9f
        jmp     0x40083b7e              | taken: stock's end of the double tap
9:      jsr     0x40033970              | what the site replaced
        jmp     0x40083b02

core_setup_open:
        tst.l   ev_setup_open
        beq.s   9f
        TAKE    ev_setup_open, 8(%sp)          | kind: 4(sp), past the pushed a1
        tst.l   %d0
        beq.s   9f
        rts                             | taken: nothing opens
9:      move.l  4(%sp), %d0             | what the site replaced
        tst.l   0x400c0c48
        jmp     0x40032232
