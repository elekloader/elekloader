| SPDX-License-Identifier: GPL-2.0-or-later
| trig-events (Octatrack, OS 1.40C): the machine trig dispatch in the frame interrupt, offered as the
| event ev_track_trig(track), which may veto, and from 0.2 STOP's tail kill as the event
| ev_tail_kill(record, track), which only tells (elekloader/sdk/include/octatrack/trig.h).
| ColdFire V4e; assemble with -mcpu=54455.
|
| The frame interrupt (vector 0x41, level 5; 0x4000aad0) goes through the eight audio tracks,
| d3 = 0..7, in the loop 0x4000d3fc..0x4000d55e. For a track whose flag byte (0x46104d0c + d3) has
| bit 4 set, a trig, it looks up the track's machine in the table at 0x400d6454 (a0) and calls
|
|     0x4000d490  move.l  d0,-(sp)          the flags: bit 5 of the flag byte, | 0xc0
|     0x4000d492  movea.l 172(sp),a1        (the lane's byte pointer, 72 bytes a track)
|     0x4000d496  mvz.b   (a1),d0           \
|     0x4000d498  move.l  d0,-(sp)           > the site: 6 bytes, three whole instructions
|     0x4000d49a  move.l  d3,-(sp)          /
|     0x4000d49c  jsr     (a0)              the machine's trig function (track, byte, flags) -> d0
|     0x4000d49e  ...                       d0 is ORed into the track's frame word; at 0x4000d4ce
|                                           lea 12(sp),sp pops the three arguments
|
| A track with no trig, or a machine with no function in the table, never gets here. The site jumps
| here instead; the three instructions are replayed, so the stack is what the machine's function
| expects. With no handler subscribed the table's first word is 0 and the stub is those three
| instructions, a tst and a jmp. Otherwise every handler gets the track in turn, as the C convention
| has it (d0-d1/a0-a1 free, the result in d0), and the first to return nonzero vetoes the trig: the
| machine's function is not called and d0 is 0, "the function did nothing", the value octabam's
| MUTE MODE has put there on a unit; everything the interrupt does after the call runs as stock, and
| the flag byte's trig bit is cleared at the loop's end as it is for any trig. The registers the
| interrupt has live (d2-d7, a2-a6) are kept; d0, d1, a0 and a1 are put back as stock had them when
| the function is called. Nothing here touches the EMAC.

        .section .run, "ax"

        .globl  tt_site
tt_site:
        .word   0x7191                  | mvz.b (%a1),%d0
        move.l  %d0, -(%sp)
        move.l  %d3, -(%sp)
        tst.l   ev_track_trig           | the table's first entry: nothing subscribed
        bne.s   1f
        jmp     0x4000d49c              | the machine's function, the stack as stock has it
1:      lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)  | what stock has at the call, and a2 for the walk
        lea     ev_track_trig, %a2
2:      move.l  (%a2)+, %d0
        beq.s   3f
        move.l  %d3, -(%sp)             | int f(int track), d3 is the track
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   2b
        movea.l 16(%sp), %a2            | vetoed: the interrupt's a2 back, the call skipped
        lea     20(%sp), %sp
        moveq   #0, %d0                 | the function "did nothing"
        jmp     0x4000d49e
3:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        jmp     0x4000d49c

| ---- the tail kill (0.2) ------------------------------------------------------------------------
| The frame interrupt's record builder (0x40004bd4) builds, for each track d4 = 0..7, the 64-byte
| record (a2) its DSP instances read. STOP, STOP, STOP holds a track: the builder reads the held
| tracks as a mask of bits in the byte at 0x8000184b, and for each held track, here, tests the
| halfword at +56 of the record, the FX2 effect id it was given when this record was last built, against
| stock's three reverbs (ids 20-22), and for them zeroes the halfwords at +24 and +34 and the byte at
| +53, the FX2 SETUP's slot 11, which clears their tails:
|
|     0x40004d08  btst    d4,d2                 d2 = the held mask
|     0x40004d0a  beq.s   0x40004d2e            not held: past the test
|     0x40004d0c  move.w  56(a2),d0             \ the site: 10 bytes, two whole instructions
|     0x40004d10  addi.l  #-20,d0               /
|     0x40004d16  mvz.w   d0,d0 ...             the reverb test
|
| ev_tail_kill(record, track) is told of it for each held track, each frame, before that test; a
| handler may zero its own effect's bytes in the record the same way (it checks +56 first). The
| site jumps here, the two instructions are replayed after the handlers (d0 is loaded by the first,
| so it is free here, as are d1, a0 and a1), and the interrupt goes on at 0x40004d16. Every handler
| runs, in order. With none subscribed the table's first word is 0 and the stub is a tst, the two
| instructions and a jmp. The interrupt's live registers (d2-d7, a2-a6) are kept: a3 walks the
| table and is saved. Nothing here touches the EMAC.

        .globl  tk_site
tk_site:
        tst.l   ev_tail_kill            | the table's first entry: nothing subscribed
        beq.s   2f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_tail_kill, %a3
1:      move.l  (%a3)+, %d0
        beq.s   3f
        move.l  %d4, -(%sp)             | void f(uint8_t *record, unsigned track): the track, pushed first
        move.l  %a2, -(%sp)             | and the record
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
3:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
2:      .word   0x302a, 0x0038          | move.w  56(%a2), %d0
        .word   0x0680, 0xffff, 0xffec  | addi.l  #-20, %d0
        jmp     0x40004d16
