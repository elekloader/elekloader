| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 1.1: where the project's saves, loads and kit copies call core for
| the mods' project and kit data (store.c). ColdFire V4; assemble with
| -mcpu=54455. OS 1.17. The format and the firmware's routines:
| docs/ADAPTING.md, "Project data, kit data and value rows (core-dt2 1.1, Digitakt II)".
|
| The sites, each a whole instruction the shim runs too:
|   kit save      move.l #0xbacef00c,d1, before the kit serializer (0x400dbdca)
|                 writes the record's end mark (0x400dbee4): a2 the record, a3
|                 the kit's RAM
|   kit load      the same instruction in the kit loader (0x400db898), after the
|                 record's version check (0x400db8be): a2 the kit's RAM, a3 the
|                 record
|   project save  move.l #0xbeefbace,(a3), the file's mark, in the save
|                 (0x400df66c) at 0x400df932: a3 the file; d0 is live after
|   project load  move.l #0xbeefbace,d0, the load's (0x400df95a) check of the
|                 mark at 0x400df99c: a3 the file
|   memcpy        its first 12 bytes (0x40136388), three loads of its
|                 arguments, by jmp: a copy of a kit's size (KIT_SIZE) also
|                 calls core_kit_copied(dst, src); every other copy goes on
|                 into memcpy at once
        .section .run, "ax"
        .globl  core_kit_save, core_kit_load, core_proj_save, core_proj_load, core_memcpy
        .globl  core_store_layout

| The device's layout, for store.c: the kit table, its stride, the bytes of a
| kit record after its end mark (offset, length), the pattern object's
| pointer (the pattern on at +0x138).
core_store_layout:
        .long   FW_KITS, KIT_SIZE, KIT_PAD, KIT_ROOM, PATTERN_OBJ

core_kit_save:
        move.l  %a3, -(%sp)
        move.l  %a2, -(%sp)
        jsr     core_kitdata_save       | (record, kit)
        addq.l  #8, %sp
        move.l  #0xbacef00c, %d1        | the instruction the site replaced
        rts

core_kit_load:
        move.l  %a3, -(%sp)
        move.l  %a2, -(%sp)
        jsr     core_kitdata_load       | (kit, record)
        addq.l  #8, %sp
        move.l  #0xbacef00c, %d1
        rts

core_proj_save:
        move.l  #0xbeefbace, (%a3)      | the instruction the site replaced
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %a3, -(%sp)
        jsr     core_projdata_save      | (file)
        addq.l  #4, %sp
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        rts

core_proj_load:
        move.l  %a3, -(%sp)
        jsr     core_projdata_load      | (file)
        addq.l  #4, %sp
        move.l  #0xbeefbace, %d0
        rts

| memcpy(dst, src, n): the cost of every other copy is the compare and the branch.
core_memcpy:
        move.l  12(%sp), %d0
        cmpi.l  #KIT_SIZE, %d0
        beq.s   1f
2:      movea.l 4(%sp), %a1             | the three instructions the site replaced
        movea.l 8(%sp), %a0
        move.l  12(%sp), %d0
        jmp     MEMCPY + 12
1:      move.l  12(%sp), -(%sp)         | a kit: the copy, then the call
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        bsr.s   2b
        lea     12(%sp), %sp
        move.l  %d0, -(%sp)             | memcpy's result
        move.l  12(%sp), -(%sp)         | src
        move.l  12(%sp), -(%sp)         | dst
        jsr     core_kit_copied
        addq.l  #8, %sp
        move.l  (%sp)+, %d0
        rts
