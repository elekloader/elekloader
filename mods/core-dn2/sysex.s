| SPDX-License-Identifier: GPL-2.0-or-later
| ev_sysex: void f(const u8 *message, u32 length, u32 source).
| MIDI task, before stock routing; observers must validate header/length/source.
| Source 2/4 is USB, 8/16 DIN. Stock routing always continues unchanged.
        .section .run, "ax"
        .globl core_sysex
core_sysex:
        lea -32(%sp), %sp
        movem.l %d0-%d3/%a0-%a3, (%sp)
        movea.l %sp, %a2
        lea ev_sysex, %a3
1:      move.l (%a3)+, %d0
        beq.s 2f
        movea.l %d0, %a0
        move.l 8(%a2), -(%sp)
        move.l 4(%a2), -(%sp)
        move.l 20(%a2), -(%sp)
        jsr (%a0)
        lea 12(%sp), %sp
        bra.s 1b
2:      movem.l (%sp), %d0-%d3/%a0-%a3
        lea 32(%sp), %sp
        lea SYSEX_TABLE, %a3
        rts
