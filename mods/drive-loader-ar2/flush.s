| SPDX-License-Identifier: GPL-2.0-or-later
| Drive Loader: the sites' writes and the cache flush (Analog Rytm MKII, OS 1.74).

        .text

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

| dl_apply_site(addr, src, len): one +Drive site's new bytes over the running main OS, with
| interrupts masked (SR 0x2700) from the first byte to the last push, then the data cache's
| lines over it pushed to memory: each line's set, all 4 ways, counted as dl_flush counts
| them (the extra sets alias). dl_flush, after the last site, invalidates the instruction and
| branch caches.
        .globl  dl_apply_site
dl_apply_site:
        lea     -8(%sp), %sp
        movem.l %d2-%d3, (%sp)
        move.l  12(%sp), %a0
        move.l  16(%sp), %a1
        move.l  20(%sp), %d1
        move.w  %sr, %d2
        move.w  #0x2700, %sr
        move.l  %a0, %d0
        add.l   %d1, %d0                | the site's end
        tst.l   %d1
        beq.s   2f
1:      move.b  (%a1)+, (%a0)+
        subq.l  #1, %d1
        bne.s   1b
2:      move.l  12(%sp), %d1
        and.l   #-16, %d1               | the site's first line
3:      cmp.l   %d0, %d1
        bcc.s   4f
        move.l  %d1, %d3
        and.l   #0x1FF0, %d3            | its set, way 0
        move.l  %d3, %a0
        cpushl  %dc, (%a0)
        addq.l  #1, %a0
        cpushl  %dc, (%a0)
        addq.l  #1, %a0
        cpushl  %dc, (%a0)
        addq.l  #1, %a0
        cpushl  %dc, (%a0)
        add.l   #16, %d1
        bra.s   3b
4:      nop                             | the pushes are done
        move.w  %d2, %sr
        movem.l (%sp), %d2-%d3
        lea     8(%sp), %sp
        rts
