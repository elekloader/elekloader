| SPDX-License-Identifier: GPL-2.0-or-later
| TELOS for the Octatrack (OS 1.40C): the loader's memory and its assembly helpers.
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| drive_area (2 MB): the mods linked from the card at boot (research/telos-ot/LOADING.md).
| The build descriptor names it (desc.py, "shadow"), so the link and `elekloader check` know it.
| drive_staging (256 KB): one .elemod file at a time. telos_heap (768 KB): the link's working
| memory, a bump heap reset at each link. All three are .bss in core's reserve.

        .text

| dl_flush: push every data-cache line to memory, then invalidate the instruction and branch
| caches (the Rytm's dl_flush: the same V4 core and the same MAIN CACR, 0xA50CE100 at
| 0x4001fc44). CPUSHL takes a set and way in An, not an address: 512 sets x 4 ways.
        .globl  dl_flush
dl_flush:
        move.l  #0, %a0
        move.l  #512 * 4, %d1
1:      cpushl  %dc, (%a0)
        addq.l  #1, %a0
        move.l  %a0, %d0
        and.l   #3, %d0
        bne.s   2f
        lea     12(%a0), %a0
2:      subq.l  #1, %d1
        bne.s   1b
        move.l  #0xA40CE100, %d0
        movec   %d0, %cacr
        nop
        rts

| u32 telos_irq_off(void): mask every interrupt, return the old SR.
        .globl  telos_irq_off
telos_irq_off:
        moveq   #0, %d0
        move.w  %sr, %d0
        move.w  #0x2700, %sr
        rts

| void telos_irq_restore(u32 sr)
        .globl  telos_irq_restore
telos_irq_restore:
        move.l  4(%sp), %d0
        move.w  %d0, %sr
        rts

| u32 telos_sp(void): the caller's stack pointer.
        .globl  telos_sp
telos_sp:
        move.l  %sp, %d0
        addq.l  #4, %d0
        rts

        .bss
        .align  16
        .globl  drive_area
drive_area:
        .skip   0x200000
        .globl  drive_area_size
        .set    drive_area_size, 0x200000
        .align  16
        .globl  drive_staging
drive_staging:
        .skip   0x40000
        .globl  drive_staging_size
        .set    drive_staging_size, 0x40000
        .align  16
        .globl  telos_heap
telos_heap:
        .skip   0xC0000
        .globl  telos_heap_size
        .set    telos_heap_size, 0xC0000
        .align  16
        .globl  telos_stockdata
telos_stockdata:
        .skip   0x29400                     | 0x400b9000..0x400e2324, a copy before the OS runs
