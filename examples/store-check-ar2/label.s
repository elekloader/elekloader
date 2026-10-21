| SPDX-License-Identifier: GPL-2.0-or-later
| store-check: the SETTINGS row's label callback. The firmware's MenuItem
| calls it as a std::function returning a std::string: the string to build
| is at a0 (the return slot), and d0 returns a0. The Rytm's own labels
| (MIDI CONFIG's at 0x400da074) are this routine with their own text:
| std::string(a0, text, allocator) through STRING_CTOR (mod.json defsym).
        .text
        .globl  sc_label
        .align  2
sc_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        pea     -1(%a6)                 | the allocator, a byte on the frame
        pea     sc_name
        move.l  %a0, %d2
        move.l  %a0, -(%sp)
        jsr     STRING_CTOR
        lea     12(%sp), %sp
        move.l  %d2, %d0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts

sc_name:
        .asciz  "STORE CHECK"
