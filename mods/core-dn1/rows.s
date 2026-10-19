| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 4.1.1: the SETTINGS rows' storage (Digitone mk1). ColdFire V4;
| assemble with -mcpu=54455. ../core/settings.s's core_additem calls these
| in place of operator new and Menu::addItem when ROW_POOL is set.
|
|   MENU_ADD    Menu::addItem(menu, item): the item's index, a shared_ptr
|               control block from the heap, push_back on the menu's vector
|   VEC_INSERT  the vector's insert when it is full (it grows the vector)
|   SP_RELEASE  shared_ptr's release(&control block)
|
| ---- Why: the first boot and the heap ------------------------------------
| The Digitone's heap is a buddy allocator. Every block a mod's row took
| from it at boot (a 0x54-byte MenuItem, which is a 128-byte block, and a
| 16-byte control block) stayed taken, and so changed which blocks are free
| for the rest of the boot. On a first boot the firmware puts the factory
| project on the +Drive, and its UI task draws the progress screen with
| small allocations every frame: with some heaps those split and merge a
| large block on every malloc and free. That UI task already takes most of
| the CPU, so the project's format upgrade (0x400103ec, a lower-priority
| task, 14 steps of 2.6 MB) starved, and the boot stayed on "FACTORY
| PROJECT >> +DRIVE" for good. In digikit's emulator a second row did that
| on 1.44, and so did 0x64 bytes allocated with no row at all.
|
| So the rows take no heap: a MenuItem and its control block live in
| row_pool, and the control block's dispose and destroy do nothing, so the
| menu dropping a row (when it is cleared or rebuilt) frees nothing. The
| menu's vector holds the shared_ptr as it holds the stock rows' (the
| SETTINGS menu's has 10 rows and room for 16, so up to six more do not
| grow it). The heap is then as the stock OS leaves it, however many rows
| the mods add. A slot is free again once its control block's count is 0;
| beyond ROWS rows the heap serves, as before 4.1.1.

        .equ    ROWS, 8                 | rows from the pool
        .equ    ITEM_SZ, 0x54           | a MenuItem (ITEM_CTOR)
        .equ    CB_SZ, 16               | its control block: vtable, uses, weak, the item
        .equ    SLOT_SZ, ITEM_SZ + CB_SZ

        .section .run, "ax"

| core_row_new(size) -> d0: storage for a MenuItem: a free slot (its
| control block's use count 0), taken by setting the count to 1, which
| core_row_add then completes. Without one, operator new(size).
        .globl  core_row_new
core_row_new:
        lea     row_pool, %a0
        moveq   #ROWS, %d1
1:      tst.l   ITEM_SZ + 4(%a0)
        beq.s   2f
        lea     SLOT_SZ(%a0), %a0
        subq.l  #1, %d1
        bne.s   1b
        jmp     OP_NEW                  | the pool is full: the heap
2:      moveq   #1, %d0
        move.l  %d0, ITEM_SZ + 4(%a0)
        move.l  %a0, %d0
        rts

| core_row_add(menu, item): Menu::addItem for an item in the pool, with its
| slot's control block in place of the one MENU_ADD would allocate. A heap
| item goes to MENU_ADD.
        .globl  core_row_add
core_row_add:
        movea.l 8(%sp), %a0             | the item
        cmpa.l  #row_pool, %a0
        bcs.s   1f
        cmpa.l  #row_pool_end, %a0
        bcs.s   2f
1:      jmp     MENU_ADD
2:      link    %a6, #-8
        move.l  %a2, -(%sp)
        movea.l 8(%a6), %a2             | the menu: +4 the vector's begin, +8 end, +12 capacity
        move.l  8(%a2), %d0
        sub.l   4(%a2), %d0
        asr.l   #3, %d0
        move.l  %d0, 8(%a0)             | the item's index, as MENU_ADD sets it
        lea     ITEM_SZ(%a0), %a1       | its control block
        move.l  #row_cb, (%a1)
        moveq   #1, %d0
        move.l  %d0, 4(%a1)             | uses
        move.l  %d0, 8(%a1)             | weak references (1 for the uses)
        move.l  %a0, 12(%a1)            | the item
        move.l  %a0, -8(%a6)            | the shared_ptr: the item, its control block
        move.l  %a1, -4(%a6)
        movea.l 8(%a2), %a0
        cmpa.l  12(%a2), %a0
        beq.s   3f
        move.l  -8(%a6), (%a0)          | room: push_back
        move.l  %a1, 4(%a0)
        addq.l  #8, 8(%a2)
        bra.s   4f
3:      pea     -8(%a6)                 | full: the stock insert grows the vector
        pea     4(%a2)
        jsr     VEC_INSERT
        pea     -4(%a6)                 | then the release MENU_ADD does after it
        jsr     SP_RELEASE
        lea     12(%sp), %sp
4:      movea.l -12(%a6), %a2
        unlk    %a6
        rts

| The control block's vtable (_Sp_counted_base's, from its destructors):
| the destructors, dispose and destroy do nothing; get_deleter has none.
        .balign 4
row_cb:
        .long   row_nop, row_nop, row_nop, row_nop, row_none
row_nop:
        rts
row_none:
        moveq   #0, %d0
        rts

| ============================ .bss ========================================
        .section .bss
        .balign 4
row_pool:
        .skip   ROWS * SLOT_SZ
row_pool_end:
