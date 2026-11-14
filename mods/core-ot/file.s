| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the card file layer's calls (core-ot 0.34).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| Stock's file layer: open(f, path, mode, buf, n) 0x40016864, read(f, buf, n)
| 0x40016564, write(f, buf, n) 0x400166b8, close(f) 0x4001677c (17, 57, 56 and 21
| callers; core's store.c calls them too). ev_file sees each call before it runs
| (file.c): void f(unsigned op, void *f, const void *a, uintptr_t b), op 0 open
| (a = path, b = mode), 1 read and 2 write (a = buffer, b = length), 3 close.
| Modwerk's base journals the same four calls; the code is our own.
|
|   0x40016864  open's entry  (lea -12(sp) ; movem.l)
|   0x40016564  read's entry  (lea -16(sp) ; movem.l)
|   0x400166b8  write's entry (lea -16(sp) ; movem.l)
|   0x4001677c  close's entry (move.l a2,-(sp) ; move.l d2,-(sp) ; movea.l 12(sp),a2)
        .section .run, "ax"

| A call's entry: d0-d1/a0-a1 are free there (the C convention), its arguments at 4(sp).
        .macro  FILEEV op, b
        tst.l   ev_file
        beq.s   1f
        move.l  \b(%sp), -(%sp)        | b: mode or length
        move.l  12(%sp), -(%sp)         | a: path or buffer (8(sp) before the push)
        move.l  12(%sp), -(%sp)         | f (4(sp) before the two pushes)
        pea     \op
        jsr     core_file
        lea     16(%sp), %sp
1:
        .endm

        .globl  core_file_open, core_file_read, core_file_write, core_file_close
core_file_open:
        FILEEV  0, 12
        lea     -12(%sp), %sp
        movem.l %d2/%a2-%a3, (%sp)
        jmp     0x4001686c
core_file_read:
        FILEEV  1, 12
        lea     -16(%sp), %sp
        movem.l %d2/%a2-%a4, (%sp)
        jmp     0x4001656c
core_file_write:
        FILEEV  2, 12
        lea     -16(%sp), %sp
        movem.l %d2/%a2-%a4, (%sp)
        jmp     0x400166c0
core_file_close:
        FILEEV  3, 4
        move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        movea.l 12(%sp), %a2
        jmp     0x40016784
