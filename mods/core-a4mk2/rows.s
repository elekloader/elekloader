| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.5: the sites of the mod p-locks (rows.c). Most are jmp sites at
| a routine's entry: core runs the routine (its displaced instructions, then
| a jump back into it), then its own part. Analog Four MKII OS 1.56.
        .text
        .globl  core_memcpy, core_memset, core_bzero, core_new, core_new_array
        .globl  core_store_init, core_track_clear, core_step_clear
        .globl  core_row_unpack, core_row_pack, core_row_update
        .globl  core_trig_list, core_lock_paste, core_fire_list
        .globl  core_accept, core_accept_note, core_voice_apply, core_dump_mark, core_dump_check
        .align  2

        .equ    STEP, 0xe4                      | a step record

| 0x40077fa0, memcpy (dst, src, n): `movea.l 4(%sp),%a1; movea.l 8(%sp),%a0`.
| A copy of a step record or more takes the mod locks of the step records
| copied. Also called from the audio interrupt (core_rows_memcpy finds
| nothing there quickly: no step record lives in SRAM).
core_memcpy:
        move.l  #STEP,%d0
        cmp.l   12(%sp),%d0
        bls.s   1f
        movea.l 4(%sp),%a1
        movea.l 8(%sp),%a0
        jmp     0x40077fa8
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
        jmp     0x40077fa8

| 0x40077fe8, memset (dst, c, n): `movea.l 8(%sp),%a0; mvz.b 11(%sp),%d0`.
| The mod locks in a block of a step record or more go.
core_memset:
        move.l  #STEP,%d0
        cmp.l   12(%sp),%d0
        bhi.s   1f
        move.l  12(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
1:      movea.l 8(%sp),%a0
        mvz.b   11(%sp),%d0
        jmp     0x40077ff0

| 0x400778e0, bzero (dst, n): `movea.l 4(%sp),%a0; move.l 8(%sp),%d0`.
core_bzero:
        move.l  #STEP,%d0
        cmp.l   8(%sp),%d0
        bhi.s   1f
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
1:      movea.l 4(%sp),%a0
        move.l  8(%sp),%d0
        jmp     0x400778e8

| 0x400653b8, 0x400653be, operator new and new[] (n): `jmp 0x4006513c`. No
| mod locks are left in a block of a step record or more given out afresh.
core_new:
core_new_array:
        move.l  4(%sp),-(%sp)
        jsr     0x4006513c
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        move.l  #STEP,%d1
        cmp.l   4(%sp),%d1
        bhi.s   1f
        move.l  %d0,-(%sp)
        move.l  8(%sp),-(%sp)
        move.l  %d0,-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
1:      move.l  %d0,%a0
        rts

| 0x40015d10, the lock store's init (store): `move.l %d3,-(%sp); move.l
| %d2,-(%sp); move.l 12(%sp),%d0`, then a beq on that move. The six tracks'
| mod locks go first.
core_store_init:
        move.l  4(%sp),%d0
        beq.s   1f
        pea     6*0x3970
        move.l  %d0,-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
1:      move.l  %d3,-(%sp)
        move.l  %d2,-(%sp)
        move.l  12(%sp),%d0
        jmp     0x40015d18

| 0x40015c2e, a track's init (track record, its lock store): `lea -16(%sp),
| %sp; movem.l %d2-%d3/%a2-%a3,(%sp)`. Its 64 step records' mod locks go.
core_track_clear:
        move.l  8(%sp),%d0
        beq.s   1f
        pea     64*STEP
        move.l  %d0,-(%sp)
        jsr     core_rows_drop
        addq.l  #8,%sp
1:      lea     -16(%sp),%sp
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp     0x40015c36

| 0x400a35ac, a step's locks' clear (model, track, step): `lea -28(%sp),%sp;
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
        move.l  16(%sp),-(%sp)            | model
        jsr     core_rows_step_clear
        lea     12(%sp),%sp
        move.l  (%sp)+,%d0
        rts
step_clear_body:
        lea     -28(%sp),%sp
        movem.l %d2-%d7/%a2,(%sp)
        jmp     0x400a35b4

| 0x40009dbc, the row unpack (lock store, rows): `lea -32(%sp),%sp; movem.l
| %d2-%d5/%a2-%a5,(%sp)`. Then the rows with mod codes become the ledger's.
core_row_unpack:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     unpack_body
        addq.l  #8,%sp
        move.l  %d0,-(%sp)
        move.l  12(%sp),-(%sp)            | rows
        move.l  12(%sp),-(%sp)            | the lock store
        jsr     core_rows_load
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
        rts
unpack_body:
        lea     -32(%sp),%sp
        movem.l %d2-%d5/%a2-%a5,(%sp)
        jmp     0x40009dc4

| 0x4000a02c, the full packer (rows, lock store): `lea -48(%sp),%sp; movem.l
| %d2-%d7/%a2-%a6,(%sp)`. Then the ledger's rows go after stock's.
core_row_pack:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     pack_body
        addq.l  #8,%sp
        move.l  %d0,-(%sp)
        move.l  12(%sp),-(%sp)            | the lock store
        move.l  12(%sp),-(%sp)            | rows
        jsr     core_rows_save
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
        rts
pack_body:
        lea     -48(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x4000a034

| 0x4000a1e6, the per-column update (rows, lock store, column, track): `lea
| -28(%sp),%sp; movem.l %d2-%d7/%a2,(%sp)`. Stock runs with the mod rows
| out (it appends at the count of used rows), then they come back. Where
| stock rebuilds instead, it jumps into the full packer, which places them.
core_row_update:
        move.l  4(%sp),-(%sp)
        jsr     core_rows_save_before
        addq.l  #4,%sp
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     update_body
        lea     16(%sp),%sp
        move.l  %d0,-(%sp)
        move.l  12(%sp),-(%sp)            | the lock store
        move.l  12(%sp),-(%sp)            | rows
        jsr     core_rows_save
        addq.l  #8,%sp
        move.l  (%sp)+,%d0
        rts
update_body:
        lea     -28(%sp),%sp
        movem.l %d2-%d7/%a2,(%sp)
        jmp     0x4000a1ee

| 0x400b905a in trig copy, after its loop over a held step's 0x70 columns
| (the wrapper %a3, the step at (%a2), the step's lock list %d2, the entry
| %d5): `movea.l -80(%fp),%a0; move.l 8(%a0),%d0`. The step's mod locks join
| the list.
core_trig_list:
        move.l  %d5,-(%sp)
        move.l  %d2,-(%sp)
        move.l  (%a2),-(%sp)
        move.l  %a3,-(%sp)
        jsr     core_rows_list
        lea     16(%sp),%sp
        movea.l -80(%fp),%a0
        move.l  8(%a0),%d0
        jmp     0x400b9062

| 0x400b51f6, the wrapper's lock set (wrapper, step, column, value, fine),
| as trig paste makes it: `lea -12(%sp),%sp; movem.l %d2-%d3/%a2,(%sp)`. A
| column of 0x70 or more is a mod lock.
core_lock_paste:
        moveq   #0x6f,%d0
        cmp.l   12(%sp),%d0
        blt.s   1f
        lea     -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp     0x400b51fe
1:      move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     core_rows_paste
        lea     16(%sp),%sp
        rts

| 0x40079fbe, a trig's lock list (track, list, the step record, deltas):
| `lea -64(%sp),%sp; movem.l %d2-%d7/%a2-%a6,(%sp)`. Then core lists the
| step's mod locks for the same event.
core_fire_list:
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     fire_body
        lea     16(%sp),%sp
        move.l  %d0,-(%sp)
        move.l  20(%sp),-(%sp)            | deltas
        move.l  20(%sp),-(%sp)            | the step record
        move.l  20(%sp),-(%sp)            | list
        move.l  20(%sp),-(%sp)            | track
        jsr     core_rows_fire
        lea     16(%sp),%sp
        move.l  (%sp)+,%d0
        rts
fire_body:
        lea     -64(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x40079fc6

| 0x40101d58 in the audio interrupt's event accept, after the event's lock
| list was copied to the voice's (jsr site; the event at 0x80006178 + %d7,
| its list pointer at +0x3c): `lea 0x80006178,%a0`. The copy's movem took
| d1..d4, so the voice's list is %a0 less what was copied: 16 bytes for each
| of the count in the list's first word, once at least. d0, d1, a0 and a1
| are free here.
core_accept:
        move.l  %d7,%d0
        add.l   #0x800061b4,%d0
        move.l  %d0,%a1
        move.l  (%a1),%a1                 | the event's list
        move.l  (%a1),%d0                 | its count
        moveq   #1,%d1
        cmp.l   %d1,%d0
        bge.s   1f
        move.l  %d1,%d0
1:      lsl.l   #4,%d0
        move.l  %a0,%d1
        sub.l   %d0,%d1                   | the voice's list
        move.l  %a1,-(%sp)
        move.l  %d1,-(%sp)
        jsr     core_rows_accept
        addq.l  #8,%sp
        lea     0x80006178,%a0
        rts

| 0x40101fb2, the note path's accept (index %d3): `move.l #0x390,%d0`. d0,
| d1, a0 and a1 are free here.
core_accept_note:
        move.l  %d3,-(%sp)
        jsr     core_rows_accept_note
        addq.l  #4,%sp
        move.l  #0x390,%d0
        jmp     0x40101fb8

| 0x401025ac in the audio interrupt's apply, where channel %a5's list goes
| to voice %d2 (one of the voices the allocator gave its note): `movea.l
| -0x84(%a6),%a0; moveq #2,%d5`. d0, d1, a0 and a1 are set again before
| stock reads them. From 0.7 also std1's ev_voice_start (voice.c).
core_voice_apply:
        move.l  %a5,-(%sp)
        move.l  %d2,-(%sp)
        jsr     core_rows_voice
        addq.l  #8,%sp
        move.l  %a5,-(%sp)                | track (a channel per track)
        move.l  %d2,-(%sp)                | voice
        jsr     core_std_voice_start      | ev_voice_start (voice.c)
        addq.l  #8,%sp
        movea.l -0x84(%a6),%a0
        moveq   #2,%d5
        rts

| 0x4006b492 in the SysEx handler, where a pattern dump's buffer (%d4) has
| the working pattern packed or a slot copied: `move.l %d7,-(%sp); move.l
| #0x32ae,%d5`. The marker for its mod rows goes in first.
core_dump_mark:
        move.l  %d4,-(%sp)
        jsr     core_dump_out
        addq.l  #4,%sp
        move.l  %d7,-(%sp)
        move.l  #0x32ae,%d5
        jmp     0x4006b49a

| 0x40011d44, a received pattern's unpack (live, buffer, 0): `move.l %d3,
| -(%sp); move.l %d2,-(%sp); move.l 12(%sp),%d3`. The buffer's mod rows
| stay only with this build's marker.
core_dump_check:
        move.l  8(%sp),-(%sp)
        jsr     core_dump_in
        addq.l  #4,%sp
        move.l  %d3,-(%sp)
        move.l  %d2,-(%sp)
        move.l  12(%sp),%d3
        jmp     0x40011d4c

| 0x4002327c, a page view's "can the knob's id be locked?" (view, id), the
| id's record's flag: `move.l 8(%sp),-(%sp); jsr 0x400f6232`. Stock's records
| end at 0x126 and a higher id reads record 0's; a mod parameter's knob of the
| current track's kind (lock.c) can be.
        .globl  core_view_lockable
core_view_lockable:
        move.l  8(%sp),%d0
        cmp.l   #0x126,%d0
        bls.s   1f
        move.l  %d0,-(%sp)
        jsr     core_lock_knob
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        moveq   #1,%d0
        rts
1:      move.l  8(%sp),-(%sp)
        jsr     0x400f6232
        jmp     0x40023286
