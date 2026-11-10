| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.5: the sites of mod parameters on MIDI (midi.c), OS 1.56. The
| three parameter sets (synth, FX, CV) share both routines.
        .text
        .globl  core_nrpn_site, core_cc_site
        .align  2

| 0x4009ff56, a set's NRPN in (set, nrpn, value, track, flag): `lea
| -20(%sp),%sp; movem.l %d2-%d5/%a2,(%sp)`. A mod parameter's NRPN (MSB 0,
| LSB 0x70..0x7f) is core's; any other goes on to stock.
core_nrpn_site:
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        jsr     core_nrpn_in
        lea     20(%sp),%sp
        tst.l   %d0
        beq.s   1f
        moveq   #1,%d0
        rts
1:      lea     -20(%sp),%sp
        movem.l %d2-%d5/%a2,(%sp)
        jmp     0x4009ff5e

| 0x400a0b62, a set's CC in (set, cc, value, track, flag): `link.w %fp,#-40;
| movem.l %d2-%d7/%a2-%a4,(%sp)`. A mod parameter's CC is core's.
core_cc_site:
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        jsr     core_cc_in
        lea     20(%sp),%sp
        tst.l   %d0
        beq.s   1f
        moveq   #1,%d0
        rts
1:      link.w  %fp,#-40
        movem.l %d2-%d7/%a2-%a4,(%sp)
        jmp     0x400a0b6a
