| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.11, remote fields (items.c): the kit init's first instructions,
| then a jump back.
        .text
        .globl  core_kit_init, core_kit_init_body
        .align  2

| 0x400117cc, a kit's init (kit): `lea -20(%sp),%sp; movem.l
| %d2/%a2-%a5,(%sp)`. A kit browser's CLEAR (0x400a6cdc) and a new kit: the
| kit's mirrors and its sounds' go to zeros, as one KIT event.
core_kit_init:
        pea     1
        jsr     core_items_nest
        addq.l  #4,%sp
        move.l  4(%sp),-(%sp)
        jsr     core_kit_init_body
        addq.l  #4,%sp
        pea     -1
        jsr     core_items_nest
        addq.l  #4,%sp
        move.l  4(%sp),-(%sp)
        pea     2                       | K_KIT
        jsr     core_items_init
        addq.l  #8,%sp
        rts
core_kit_init_body:
        lea     -20(%sp),%sp
        movem.l %d2/%a2-%a5,(%sp)
        jmp     0x400117d4
