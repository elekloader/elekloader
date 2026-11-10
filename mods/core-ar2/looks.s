| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.13: knob looks (looks.c). Two stock lookups, from their first
| instruction:
|
| 0x400ff5c2, uirec(id): `move.l 4(%sp),%d1; cmpi.l #469,%d1`. Stock's body
| touches only %d0 and %d1, so this keeps %a0 and %a1 as it found them.
|
| 0x40006ff0, range(id), with the 12 bytes' place in %a0: `move.l %d2,-(%sp);
| move.l 8(%sp),%d0`. It answers that place in %d0, as stock.
|
| 0x400a85e8, a knob's graphic (set, id, value, ...), vtable +88 of the
| knob sets: `lea -16(%sp),%sp; movem.l %d2-%d5,(%sp)`. The value argument
| becomes core_knob_dial(id, value) before stock's body runs (stock's body
| rewrites its arguments too).

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
        jmp     0x400a85f0
