| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2: the sites of the mod folders in the +Drive browser (drive.c),
| OS 1.74. A function's site is its first whole instructions, which its
| _stock body re-emits before jumping back in; a pointer is answered in
| %d0 and %a0, as either may be read.
        .text
        .globl  core_drive_readdir_site, core_drive_readdir_stock
        .globl  core_drive_resolve_site, core_drive_resolve_stock
        .globl  core_drive_exists_stock, core_drive_size_stock, core_drive_count_stock
        .globl  core_drive_load_stock, core_drive_check_stock, core_drive_mkdir_stock
        .globl  core_drive_path_site, core_drive_link_site
        .globl  core_drive_ref_site, core_drive_mkdir_site
        .align  2

| 0x4007c0b4 readdir(handle, record): drive.c with the caller's address
core_drive_readdir_site:
        move.l  (%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     core_drive_readdir
        lea     12(%sp),%sp
        rts
core_drive_readdir_stock:
        lea     -24(%sp),%sp
        movem.l %d2-%d5/%a2-%a3,(%sp)
        jmp     0x4007c0bc

| 0x40080504 resolve(id) -> a node
core_drive_resolve_site:
        move.l  4(%sp),-(%sp)
        jsr     core_drive_resolve
        addq.l  #4,%sp
        move.l  %d0,%a0
        rts
core_drive_resolve_stock:
        lea     -16(%sp),%sp
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp     0x4008050c

| 0x400807d2 exists(id)
core_drive_exists_stock:
        move.l  %d2,-(%sp)
        move.l  8(%sp),%d1
        jmp     0x400807d8

| 0x40080910 size(id)
core_drive_size_stock:
        lea     -32(%sp),%sp
        movem.l %d2-%d5/%a2,(%sp)
        jmp     0x40080918

| 0x400808b6 the node's +4 value(id); the tst sets the flags stock's beq reads
core_drive_count_stock:
        move.l  %a2,-(%sp)
        move.l  %d2,-(%sp)
        tst.l   0x41843c70
        jmp     0x400808c0

| 0x400b719a load(manager, &ref, slot)
core_drive_load_stock:
        link.w  %fp,#-28
        movem.l %d2-%d4/%a2,(%sp)
        jmp     0x400b71a2

| 0x40080060 a reference's check(&ref, out)
core_drive_check_stock:
        lea     -24(%sp),%sp
        movem.l %d2/%a2-%fp,(%sp)
        jmp     0x40080068

| 0x4007c1dc mkdir(path)
core_drive_mkdir_stock:
        link.w  %fp,#-24
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp     0x4007c1e4

| 0x4007c640 id -> path (id, ..): none (0) for a mod entry
core_drive_path_site:
        move.l  4(%sp),%d0
        andi.l  #0xFF000000,%d0
        cmpi.l  #0x7E000000,%d0
        bne.s   1f
        moveq   #0,%d0
        move.l  %d0,%a0
        rts
1:      lea     -324(%sp),%sp
        movem.l %d2-%d7/%a2-%fp,(%sp)
        jmp     0x4007c648

| 0x4007b41c a new entry in a folder (parent, name, ..) -> 0, -1: none
| named as a mod folder in the root (id 2); the stack is stock's as it came
core_drive_link_site:
        moveq   #2,%d0
        cmp.l   4(%sp),%d0
        bne.s   1f
        move.l  8(%sp),-(%sp)
        jsr     core_drive_named
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        moveq   #-1,%d0
        rts
1:      lea     -56(%sp),%sp
        movem.l %d2-%d7/%a2-%fp,(%sp)
        jmp     0x4007b424

| 0x400ea820, LOAD TO PROJ's job: jsr 0x4007e698 (model, index), the
| 16-byte reference returned through %a0
core_drive_ref_site:
        move.l  %a0,-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_drive_ref
        lea     12(%sp),%sp
        move.l  (%sp)+,%a0
        tst.l   %d0
        beq.s   1f
        move.l  %a0,%d0
        rts
1:      jmp     0x4007e698

| 0x400ea6b2, CREATE DIR: jsr 0x4007c1dc (path). %a4 is the browser's
| invoker, its +4 the folder's path: "" (a mod folder's, which has none)
| is refused, else stock's mkdir (through core_drive_mkdir)
core_drive_mkdir_site:
        move.l  4(%a4),%d0
        beq.s   1f
        move.l  %d0,%a0
        tst.b   (%a0)
        bne.s   2f
1:      moveq   #-1,%d0
        rts
2:      jmp     0x4007c1dc
