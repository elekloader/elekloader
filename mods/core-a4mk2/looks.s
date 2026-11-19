| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.6: knob looks (looks.c), core-ar2 0.13's looks.s with the
| A4's sites (OS 1.56). Two stock lookups, from their first instruction:
|
| 0x400f6232, uirec(id): `move.l 4(%sp),%d1; cmpi.l #0x127,%d1` (10 bytes:
| the jmp's 6 end inside the cmpi). Stock's body touches only %d0 and %d1,
| so this keeps %a0 and %a1 as it found them.
|
| 0x40007798, range(id), with the 12 bytes' place in %a0: `move.l %d2,-(%sp);
| move.l 8(%sp),%d1` (the Rytm's loads %d0). It answers that place in %d0,
| as the Rytm's does.
|
| 0x4009eaaa, a knob's graphic (set, id, value, ...), vtable +0x84 of the
| parameter sets (the Rytm's +88): `lea -16(%sp),%sp; movem.l %d2-%d5,(%sp)`.
| The value argument becomes core_knob_dial(id, value) before stock's body
| runs.

        .text
        .globl  core_uirec_hook, core_range_hook, core_graphic_hook

        .align  2
core_uirec_hook:
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        move.l  12(%sp),-(%sp)            | id
        jsr     core_uirec
        addq.l  #4,%sp
        move.l  (%sp)+,%a0
        move.l  (%sp)+,%a1
        rts

        .align  2
core_range_hook:
        move.l  %a0,-(%sp)
        move.l  8(%sp),-(%sp)             | id
        move.l  %a0,-(%sp)                | out
        jsr     core_param_range
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
        rts

        .align  2
core_graphic_hook:
        move.l  12(%sp),-(%sp)            | value
        move.l  12(%sp),-(%sp)            | id
        jsr     core_knob_dial
        addq.l  #8,%sp
        move.l  %d0,12(%sp)
        lea     -16(%sp),%sp
        movem.l %d2-%d5,(%sp)
        jmp     0x4009eab2
