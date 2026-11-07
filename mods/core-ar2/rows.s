| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.8: the sites of the mod p-lock rows (rows.c). Each is a jmp
| site at a routine's entry: core runs the routine (its displaced
| instructions, then a jump back into it), then its own part (OS 1.74).
        .text
        .globl  core_memcpy, core_row_load, core_rebuild, core_trig_locks
        .globl  core_memset, core_new, core_new_array, core_track_clear, core_step_clear
        .globl  core_trig_list, core_euclid_list, core_lock_paste, core_dump_check
        .align  2

| 0x40098bc0, memcpy (dst, src, n): `movea.l 4(%sp),%a1; movea.l 8(%sp),%a0`.
| A copy of 88 bytes or more takes the mod locks of the step records copied.
core_memcpy:
        moveq   #88,%d0
        cmp.l   12(%sp),%d0
        bls.s   1f
        movea.l 4(%sp),%a1
        movea.l 8(%sp),%a0
        jmp     0x40098bc8
1:      move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     memcpy_body
        lea     12(%sp),%sp
        move.l  %d0,-(%sp)
        move.l  16(%sp),-(%sp)            | n
        move.l  16(%sp),-(%sp)            | src
        move.l  16(%sp),-(%sp)            | dst
        jsr     core_rows_memcpy
        lea     12(%sp),%sp
        move.l  (%sp)+,%d0
        rts
memcpy_body:
        movea.l 4(%sp),%a1
        movea.l 8(%sp),%a0
        jmp     0x40098bc8

| 0x40098c08, memset (dst, c, n): `movea.l 8(%sp),%a0; moveq...`. The mod
| locks in a block of 88 bytes or more go.
core_memset:
        moveq   #88,%d0
        cmp.l   12(%sp),%d0
        bhi.s   1f
        move.l  12(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
1:      movea.l 8(%sp),%a0
        mvz.b   11(%sp),%d0
        jmp     0x40098c10

| 0x40082a40, 0x40082a46, operator new and new[] (n): `jmp 0x400827c4`.
| No mod locks are left in memory given out afresh.
core_new:
core_new_array:
        move.l  4(%sp),-(%sp)
        jsr     0x400827c4
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0,-(%sp)
        move.l  8(%sp),-(%sp)
        move.l  %d0,-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
1:      move.l  %d0,%a0
        rts

| 0x4001197c, a track's clear (data, locks): `lea -16(%sp),%sp; movem.l
| %d2-%d3/%a2-%a3,(%sp)`. Its 64 step records' mod locks go.
core_track_clear:
        move.l  8(%sp),%d0
        beq.s   1f
        pea     64*88
        move.l  %d0,-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
1:      lea     -16(%sp),%sp
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp     0x40011984

| 0x400ac86e, a step's clear (pattern, track, step): `lea -28(%sp),%sp;
| movem.l %d2-%d7/%a2,(%sp)`. Then its mod locks go.
core_step_clear:
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     step_clear_body
        lea     12(%sp),%sp
        move.l  %d0,-(%sp)
        move.l  16(%sp),-(%sp)            | step
        move.l  16(%sp),-(%sp)            | track
        move.l  16(%sp),-(%sp)            | pattern
        jsr     core_rows_step_clear
        lea     12(%sp),%sp
        move.l  (%sp)+,%d0
        rts
step_clear_body:
        lea     -28(%sp),%sp
        movem.l %d2-%d7/%a2,(%sp)
        jmp     0x400ac876

| 0x400c4d62 in trig copy, after its loop over a held step's 42 codes
| (the track record %a3, the step at (%a2), the lock list %d2, the entry
| %d6): `movea.l -84(%fp),%a4; move.l 8(%a4),%d0`. The step's mod locks
| join the list.
core_trig_list:
        move.l  %d6,-(%sp)
        move.l  %d2,-(%sp)
        move.l  (%a2),-(%sp)
        move.l  %a3,-(%sp)
        jsr     core_rows_list
        lea     16(%sp),%sp
        movea.l -84(%fp),%a4
        move.l  8(%a4),%d0
        jmp     0x400c4d6a

| 0x400c3eb0 in Euclid's convert, after its loop (the track record %a2,
| the step %d2, the lock list %d4, the entry 44(%sp)): `move.l %d2,-(%sp);
| move.l #0x400c070a,%d7`.
core_euclid_list:
        pea     44(%sp)
        move.l  %d4,-(%sp)
        move.l  %d2,-(%sp)
        move.l  %a2,-(%sp)
        jsr     core_rows_list
        lea     16(%sp),%sp
        move.l  %d2,-(%sp)
        move.l  #0x400c070a,%d7
        jmp     0x400c3eb8

| 0x400c058c, a lock's set (track record, step, code, value, fine), as a
| paste of a listed step makes it: `lea -12(%sp),%sp; movem.l
| %d2-%d3/%a2,(%sp)`. A code above 41 is a mod lock.
core_lock_paste:
        moveq   #41,%d0
        cmp.l   12(%sp),%d0
        blt.s   1f
        lea     -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp     0x400c0594
1:      move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     core_rows_paste
        lea     16(%sp),%sp
        rts

| 0x40008f98, row load (live lock store, rows): `lea -36(%sp),%sp; movem.l
| %d2-%d6/%a2-%a5,(%sp)`. Then the rows with mod codes become the shadow.
core_row_load:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     row_load_body
        addq.l  #8,%sp
        move.l  %d0,-(%sp)
        move.l  12(%sp),-(%sp)            | rows
        move.l  12(%sp),-(%sp)            | the lock store
        jsr     core_rows_load
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
        rts
row_load_body:
        lea     -36(%sp),%sp
        movem.l %d2-%d6/%a2-%a5,(%sp)
        jmp     0x40008fa0

| 0x40009216, rebuild (rows, live lock store, ...): `lea -48(%sp),%sp;
| movem.l %d2-%d7/%a2-%a6,(%sp)`. Then the shadow's rows go after stock's.
core_rebuild:
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     rebuild_body
        lea     12(%sp),%sp
        move.l  %d0,-(%sp)
        move.l  12(%sp),-(%sp)            | the lock store
        move.l  12(%sp),-(%sp)            | rows
        jsr     core_rows_save
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
        rts
rebuild_body:
        lea     -48(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x4000921e

| 0x4009a954, a trig's locks (track, out, the step's locks, fine bytes):
| `clr.w %d0; lea -32(%sp),%sp`. Then core notes the trig's mod locks.
core_trig_locks:
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     trig_locks_body
        lea     16(%sp),%sp
        move.l  %d0,-(%sp)
        move.l  16(%sp),-(%sp)            | the step's locks
        move.l  12(%sp),-(%sp)            | track
        jsr     core_rows_trig
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
        rts
trig_locks_body:
        clr.w   %d0
        lea     -32(%sp),%sp
        jmp     0x4009a95a

| 0x4000d79c in a received pattern's load, once stock's check has passed
| (the live pattern %d3, the buffer %d2): `move.l %d2,16(%sp); move.l
| %d3,12(%sp)`, then stock's tail call of the load. The buffer's mod rows
| stay only with this build's marker.
core_dump_check:
        move.l  %d2,-(%sp)
        jsr     core_dump_in
        addq.l  #4,%sp
        move.l  %d2,16(%sp)
        move.l  %d3,12(%sp)
        jmp     0x4000d7a4
