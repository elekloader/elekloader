| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.8: ev_seq_key's site and core_popup, OS 1.56 (seqkey.c has
| the event's dispatch and the other helpers).
        .text
        .align  2

| ---- ev_seq_key: 0x400c8ba2, the first call of TrackSelectionView's key
| handler 0x400c8b90 (%d2 the KeyEvent, %a2 the view): `jsr 0x4005b6f0`, the
| key code (event +0xc), with the event pushed (6 bytes). Stock leaves that
| long pushed, pushes a second at 0x400c8ba8 and pops both at 0x400c8bb4, so
| the stub returns to 0x400c8ba8 with the same stack; after the call stock
| reads only %d0. The stub makes that call, then
| asks core_seq_key(view, event, key). Taken: the handler's handled exit
| 0x400c8f6a (`moveq #1,%d3`, then `movem.l -0x28(%fp)`, `unlk`, `rts`, so
| what this stub pushed does not matter there). Else stock resumes at
| 0x400c8ba8 with the key code in %d0, as after its own call.
        .globl  core_seq_key_site
core_seq_key_site:
        jsr     0x4005b6f0
        tst.l   ev_seq_key
        beq.s   9f
        move.l  %d0,-(%sp)                | the key, kept for stock
        move.l  %d0,-(%sp)
        move.l  %d2,-(%sp)
        move.l  %a2,-(%sp)
        jsr     core_seq_key
        lea     12(%sp),%sp
        tst.l   %d0
        beq.s   8f
        jmp     0x400c8f6a
8:      move.l  (%sp)+,%d0
9:      jmp     0x400c8ba8

| ---- core_popup(view, text): stock's pop-up on a view, built as stock's
| CLEAR TRACK builds its own: a string from the C text (0x4014c84c, its
| destination in %a0), converted (0x4014c122) to the {object, count} pair
| 0x40061b5c(view, &pair, 0) takes (0: always added), and both counts
| released (0x40144764).
        .globl  core_popup
core_popup:
        link    %fp,#-16
        move.l  %a2,-(%sp)
        move.l  %a3,-(%sp)
        movea.l 8(%fp),%a2
        lea     -8(%fp),%a3
        move.l  12(%fp),-(%sp)
        movea.l %a3,%a0
        jsr     0x4014c84c
        move.l  %a3,-(%sp)
        lea     -16(%fp),%a3
        move.l  %a3,-(%sp)
        jsr     0x4014c122
        clr.l   -(%sp)
        move.l  %a3,-(%sp)
        move.l  %a2,-(%sp)
        jsr     0x40061b5c
        pea     -12(%fp)
        lea     0x40144764,%a3
        jsr     (%a3)
        pea     -4(%fp)
        jsr     (%a3)
        movea.l -20(%fp),%a2
        movea.l -24(%fp),%a3
        unlk    %fp
        rts
