| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.11: the sequencer clock's two sites (seq.c), OS 1.74. The clock
| is the exception handler at 0x4009b0b4, one call a tick (96 a quarter
| note, also while stopped).
        .text
        .globl  core_seq_fired_site, core_seq_exit_site
        .align  2

| 0x4009b68a in the clock's loop over the 13 tracks, after trig_fire
| (0x4009aa28) has filled the trig record at 0x42ac1704 for the track %d2's
| next step and it was copied to the trig queue: `tst.l 0x42ac1704`, whose
| flags the next instruction (beq) takes. Core notes the track (%d2) and
| whether the record holds a trig, then sets the same flags.
core_seq_fired_site:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  %d2,-(%sp)
        jsr     core_seq_fired
        addq.l  #4,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        tst.l   0x42ac1704
        rts

| 0x4009c00a, the clock's exit, after it restored the SR it entered with:
| `movem.l (%sp),%d0-%d7/%a0-%a6; lea 76(%sp),%sp`, then its rte. Every
| register is on the stack there, so core's part may use any.
core_seq_exit_site:
        jsr     core_seq_exit
        movem.l (%sp),%d0-%d7/%a0-%a6
        lea     76(%sp),%sp
        rte
