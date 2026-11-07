| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.12: knob looks (looks.c). Two stock lookups, from their first
| instruction:
|
| 0x400ff5c2, uirec(id): `move.l 4(%sp),%d1; cmpi.l #469,%d1`. Stock's body
| touches only %d0 and %d1, so this keeps %a0 and %a1 as it found them.
|
| 0x40006ff0, range(id), with the 12 bytes' place in %a0: `move.l %d2,-(%sp);
| move.l 8(%sp),%d0`. It answers that place in %d0, as stock.

        .text
        .globl  core_uirec_hook, core_range_hook

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
