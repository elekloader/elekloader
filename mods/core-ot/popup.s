| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: rows mods add to the popup lists (core-ot 0.29).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| Every popup list opens through 0x4006d94c(n, cursor, int *out, labels[], callbacks[])
| (17 callers: the sample editor's SLICE, TRIM and EDIT menus, the file browser, ...;
| choose(title, n, items, cb), 0x4006d57c, 76 callers, opens its lists through it too).
| On YES it closes the list, stores the cursor in *out and calls callbacks[cursor]()
| (0x4006d8f4). Here its entry is ev_popup: handlers see the rows and may add some
| (popup.c); a list nobody changed opens exactly as stock opens it.
|
|   0x4006d94c  the opener's entry (lea -36(sp) ; movem.l)       ev_popup
|
| No byte of irpina/octabam2elemod v1.0 or of Modwerk's base is at this entry (its
| SLICE menu hooks sit in the callers and the select path). Runs in the UI task.

        .section .run, "ax"

        .globl  core_popup
core_popup:
        tst.l   ev_popup
        beq.s   core_popup_body
        move.l  (%sp), -(%sp)           | the caller's return address
        pea     8(%sp)                  | its five arguments
        jsr     core_popup_rows         | -> 0, or a new argument block
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   core_popup_body
        movea.l %d0, %a0
        move.l  16(%a0), -(%sp)         | callbacks
        move.l  12(%a0), -(%sp)         | labels
        move.l  8(%a0), -(%sp)          | out
        move.l  4(%a0), -(%sp)          | cursor
        move.l  (%a0), -(%sp)           | n
        bsr.s   core_popup_body
        lea     20(%sp), %sp
        rts

| Stock's opener: the two instructions the site displaced, then the rest of it.
core_popup_body:
        lea     -36(%sp), %sp
        movem.l %d2-%d7/%a2-%a4, (%sp)
        jmp     0x4006d954
