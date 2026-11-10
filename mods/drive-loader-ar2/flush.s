| SPDX-License-Identifier: GPL-2.0-or-later
| Drive Loader: the boot stub's entry and the cache flush (Analog Rytm MKII, OS 1.74).

        .text

| drive_boot: the site 0x401009e0 in main() (FUN_401006ee), stock `jsr 0x4009da0e.l`.
| It makes the displaced call first, with the stack exactly as the site had it (its own
| return address taken off and kept), then runs the boot linker (boot.c).
        .globl  drive_boot
drive_boot:
        move.l  (%sp)+, dl_boot_ret
        jsr     0x4009da0e
        move.l  %d0, -(%sp)
        jsr     dl_boot
        move.l  (%sp)+, %d0
        move.l  dl_boot_ret, -(%sp)
        rts

| dl_flush: push every data-cache line to memory, then invalidate the instruction and
| branch caches. On the ColdFire V4, CPUSHL takes a set and way in An (bits 1..0 the way,
| from bit 4 the set), not an address, so a loop over the area's addresses would push only
| way 0 of each set. 512 sets x 4 ways covers a data cache up to 32 KB (the MCF5441x's is
| 8 KB: the extra sets alias, harmless). Then CACR 0xA40CE100: MAIN's own value
| (0xA50CE100, set at 0x400008e4) without DCINVA, so no dirty line is ever dropped.
        .globl  dl_flush
dl_flush:
        move.l  #0, %a0
        move.l  #512 * 4, %d1
1:      cpushl  %dc, (%a0)
        addq.l  #1, %a0
        move.l  %a0, %d0
        and.l   #3, %d0
        bne.s   2f
        lea     12(%a0), %a0            | way 3 done: the next set (+16, ways back at 0)
2:      subq.l  #1, %d1
        bne.s   1b
        move.l  #0xA40CE100, %d0
        movec   %d0, %cacr
        nop
        rts

        .bss
        .align  4
dl_boot_ret:
        .skip   4
