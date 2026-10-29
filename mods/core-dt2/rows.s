| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 1.1 and core-dn2 0.3: value rows (core_addvalue, rows.c). ColdFire
| V4; assemble with -mcpu=54455.
|
| core_addrow(menu, row, payload): core_additem's MenuItem (settings.s), with
| one payload in all four std::function objects: each callback finds it as
| the first word of the storage it is given.
        .text
        .globl  core_addrow, core_value_label
core_addrow:
        link    %a6, #-64
        lea     -12(%sp), %sp
        movem.l %d2/%a2-%a3, (%sp)
        movea.l 8(%a6), %a2             | the menu
        movea.l 12(%a6), %a3            | the row's four callbacks
        lea     -64(%a6), %a0
        moveq   #3, %d2
1:      move.l  16(%a6), (%a0)          | label, select, draw, change: the payload
        clr.l   4(%a0)
        move.l  #FN_MGR, %d0
        move.l  %d0, 8(%a0)
        move.l  (%a3)+, %d0
        move.l  %d0, 12(%a0)
        lea     16(%a0), %a0
        subq.l  #1, %d2
        bpl.s   1b
        pea     0x54.w
        jsr     OP_NEW
        addq.l  #4, %sp
        move.l  %d0, %d2
        pea     8.w
        pea     -1.w
        pea     -16(%a6)
        pea     -32(%a6)
        pea     -48(%a6)
        pea     -64(%a6)
        move.l  %d2, -(%sp)
        jsr     ITEM_CTOR
        lea     28(%sp), %sp
        move.l  %d2, -(%sp)
        move.l  %a2, -(%sp)
        jsr     MENU_ADD
        addq.l  #8, %sp
        movem.l -76(%a6), %d2/%a2-%a3
        unlk    %a6
        rts

| A value row's label: the MenuItem calls it as a std::function returning a
| std::string, built at a0 (the return slot), d0 returning a0; its storage at
| 4(sp) holds the payload, a cell whose first word is the core_value_t, whose
| first word is the label.
core_value_label:
        link    %a6, #-4
        move.l  %d2, -(%sp)
        move.l  %a0, %d2
        movea.l 8(%a6), %a1             | the storage
        movea.l (%a1), %a1              | the cell
        movea.l (%a1), %a1              | the row
        pea     -1(%a6)                 | the allocator, a byte on the frame
        move.l  (%a1), -(%sp)           | its label
        move.l  %d2, -(%sp)
        jsr     FW_STR_NEW
        lea     12(%sp), %sp
        move.l  %d2, %d0
        move.l  -8(%a6), %d2
        unlk    %a6
        rts
