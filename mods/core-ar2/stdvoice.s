| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.17: the sites of std1's voice events (stdvoice.c), OS 1.74, in
| the audio interrupt's trig intake (0x4011fb66, the loop at 0x40121090:
| %d0 the track, %d1 its voice, %a0 the amp envelopes 0x42fb49b4, %a1 the
| owner table 0x40291138, %a2 the trig flags 0x8000e53c, %a6 the frame).
| Each runs stock's displaced instructions and resumes after them. With no
| handler on either event the call is skipped: two tests per trig or
| release. C may change %d0, %d1, %a0 and %a1, which both sites keep.
        .text
        .globl  core_std_voice_trig_site, core_std_voice_rel_site
        .align  2

| 0x401210ae, a trig (state 1) taking voice %d1 for track %d0, %d7 the
| voice's previous owner (live after): `move.l (0,%a2,%d6.l),%d6; move.b
| %d0,(0,%a1,%d1.l)`, then on at 0x401210b6.
core_std_voice_trig_site:
        move.l  (0,%a2,%d6.l),%d6         | the track's trig flags
        move.b  %d0,(0,%a1,%d1.l)         | the voice's owner
        tst.l   std_voice_start
        bne.s   1f
        tst.l   std_voice_end
        beq.s   9f
1:      lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  %d0,-(%sp)                | track
        move.l  %d1,-(%sp)                | voice
        jsr     core_std_voice_start
        addq.l  #8,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
9:      jmp     0x401210b6

| 0x4012110e, a release (state 2) of voice %d1: `lsl.l #5,%d1; moveq #4,%d7;
| move.l %d7,(0,%a0,%d1.l)`, the amp envelope's state to release, then on
| at 0x40121116 (also a branch target, so not covered).
core_std_voice_rel_site:
        tst.l   std_voice_end
        beq.s   9f
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  %d1,-(%sp)                | voice
        jsr     core_std_voice_end
        addq.l  #4,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
9:      lsl.l   #5,%d1
        moveq   #4,%d7
        move.l  %d7,(0,%a0,%d1.l)
        jmp     0x40121116
