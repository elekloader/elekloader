| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2: the sequencer's fired site (seq.c), OS 1.56. The tick is the
| exception handler at 0x4007a9b0 (vector 121), 96 calls a quarter note, also
| while stopped; its exit 0x4007badc is core's already (stdmidi.s), where
| core_std_seq_tick calls core_seq_exit first.
        .text
        .globl  core_seq_fired_site
        .align  2

| 0x4007b0e8 in the tick's loop over the 6 tracks, after trig_fire
| (0x4007a1d2) has filled the trig record at 0x42872474 for the track %d2's
| next step and it was copied to the trig queue (0x40077fa0): `tst.l
| 0x42872474`, whose flags the next instruction (beq) takes. Core notes the
| track (%d2) and whether the record holds a trig, then sets the same flags.
core_seq_fired_site:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  %d2,-(%sp)
        jsr     core_seq_fired
        addq.l  #4,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        tst.l   0x42872474
        rts
