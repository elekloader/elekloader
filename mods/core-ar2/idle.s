| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.16: the silent-voice skip's site (idle.c has the rules).
|
| 0x40121208, in the render's slot loop (0x40121152): `movea.l
| (0,%a5,%d4.l),%a1; tst.l %a1`, before stock's `beq.b 0x40121248`, which
| skips the slot's machine call when %a1 is 0. Live here: %d0 the slot's
| owner track, %d2 the slot, %d4 the slot x 4, %a2 the slot's parameter
| pointer (0x419ca0f0 + %d4), %a5 the machine pointers (0x419ca110), %d7 the
| tracks' flag bytes (0x8000ea18), %a4 the tracks' pitches (0x80006388).
| %d1, %d6 and %a0 are free (stock reloads them). The site returns %a1 and
| its flags: 0 takes stock's skip, the pointer runs the call.
|
| Off (core_voice_skip_want 0, the boot's value, or core_voice_skip_hold
| above 0) the site is stock's two instructions and a few tests: the slot's
| watch is forgotten, a slot that was skipped gets its call again from this
| block, and its digital state still goes back to the boot's at its next
| SYN trig (core_voice_off).
|
| On, the busy path, a voice whose machine gain is above 2^-19 of full scale
| (|gain| >= 0x1000 in either of the slot's gain words, 0x80007000 + 8 x
| slot), is a dozen instructions: it forgets the slot's watch and returns
| the pointer.
        .equ    GAINS, 0x80007000
        .equ    TRACK_TRIG, 0x8000e508
        .equ    FRAME, 0x800063c0       | the control frame, 0x3d8 bytes = 246 longs

        .text
        .globl  core_voice_site, core_voice_skip_hold_add, core_voice_skip_hold_release
        .globl  core_frame_save, core_frame_diff
        .align  2
core_voice_site:
        movea.l (0,%a5,%d4.l),%a1
        tst.b   core_voice_skip_want
        beq.s   7f
        tst.l   core_voice_skip_hold
        bne.s   7f
        tst.l   %a1
        beq.s   9f                      | no machine yet: stock's skip
        lea     GAINS,%a0
        move.l  (0,%a0,%d4.l*2),%d1     | the gain now
        addi.l  #0xfff,%d1
        cmpi.l  #0x1ffe,%d1
        bhi.s   8f                      | |gain| >= 0x1000: sounding
        move.l  (4,%a0,%d4.l*2),%d1     | the gain it ramps to
        addi.l  #0xfff,%d1
        cmpi.l  #0x1ffe,%d1
        bls.s   1f
8:      lea     core_voice_state,%a0
        clr.l   (0,%a0,%d4.l*2)         | watch again from the start
        clr.l   (4,%a0,%d4.l*2)
9:      tst.l   %a1
        rts
1:      move.l  %d0,-(%sp)              | the owner track, for the call after
        move.l  %a1,-(%sp)
        move.l  %d0,-(%sp)
        move.l  %d2,-(%sp)
        jsr     core_voice_quiet        | (slot, owner, pointer): 0 or pointer
        lea     12(%sp),%sp
        movea.l %d0,%a1
        move.l  (%sp)+,%d0
        tst.l   %a1
        rts

| off: stock, the watch forgotten, a due slot's reset at its owner's trig
7:      lea     core_voice_state,%a0
        clr.l   (0,%a0,%d4.l*2)
        clr.l   (4,%a0,%d4.l*2)
        tst.b   core_voice_due
        beq.s   9b
        lea     TRACK_TRIG,%a0
        tst.l   (0,%a0,%d0.l*4)
        beq.s   9b
        move.l  %d0,-(%sp)
        move.l  %a1,-(%sp)
        move.l  %d0,-(%sp)
        move.l  %d2,-(%sp)
        jsr     core_voice_off          | (slot, owner)
        lea     8(%sp),%sp
        movea.l (%sp)+,%a1
        move.l  (%sp)+,%d0
        tst.l   %a1
        rts

| a mod that needs stock's exact render (one that reads a voice's machine
| state, or measures stock's load) holds the skip off while it does; one
| instruction each, so any context may call them
core_voice_skip_hold_add:
        addq.l  #1, core_voice_skip_hold
        rts
core_voice_skip_hold_release:
        subq.l  #1, core_voice_skip_hold
        rts

| the checked call's frame copy and compare (idle.c)
|
| core_frame_save(dst): the frame's 246 longs to dst, 41 x 6 by movem
core_frame_save:
        movea.l 4(%sp),%a1
        lea     -24(%sp),%sp
        movem.l %d2-%d7,(%sp)
        lea     FRAME,%a0
        moveq   #41,%d0
1:      movem.l (%a0),%d2-%d7
        movem.l %d2-%d7,(%a1)
        lea     24(%a0),%a0
        lea     24(%a1),%a1
        subq.l  #1,%d0
        bne.s   1b
        movem.l (%sp),%d2-%d7
        lea     24(%sp),%sp
        rts

| core_frame_diff(before): bit c set when a long of 8c..8c+7 differs
| between the frame and before (chunk 30 holds longs 240..245)
core_frame_diff:
        movea.l 4(%sp),%a1
        lea     -12(%sp),%sp
        movem.l %d2-%d4,(%sp)
        lea     FRAME,%a0
        clr.l   %d0                     | the mask
        clr.l   %d3                     | the chunk
        moveq   #30,%d4
1:      clr.l   %d1
        .rept   8
        move.l  (%a0)+,%d2
        sub.l   (%a1)+,%d2
        or.l    %d2,%d1
        .endr
        tst.l   %d1
        beq.s   2f
        bset    %d3,%d0
2:      addq.l  #1,%d3
        cmp.l   %d4,%d3
        bne.s   1b
        clr.l   %d1
        .rept   6
        move.l  (%a0)+,%d2
        sub.l   (%a1)+,%d2
        or.l    %d2,%d1
        .endr
        tst.l   %d1
        beq.s   3f
        bset    %d3,%d0
3:      movem.l (%sp),%d2-%d4
        lea     12(%sp),%sp
        rts

        .bss
        .align  2
        .globl  core_voice_skip_hold, core_voice_skip_want
core_voice_skip_hold:
        .space  4
core_voice_skip_want:
        .space  1
