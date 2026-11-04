| SPDX-License-Identifier: GPL-2.0-or-later
| core 5.0 (Digitakt mk1): the firmware's moments as events. ColdFire V4;
| assemble with -mcpu=54455. Beside core.s.
|
| Each event here is an observer list unless it says "may take": its
| handlers get the routine's own arguments (longs, as the stock pushed
| them) and every register comes back as it went. A handler that may take
| returns nonzero to stop the stock: the routine does not run (or the call
| it replaced is skipped). Rates and tasks are given per event: an event
| marked "interrupt" runs inside the audio render or the sequencer's tick,
| where a handler must be short and must not block.
|
| Addresses are 1.53's; each OS's port in mod.json gives its own.

| NOTIFY list, nargs[, veto]: call each handler of `list` with the routine's
| first `nargs` stack arguments (at an entry: 4(sp) is the first). With veto,
| a handler's nonzero return returns from the routine at once.
        .macro  NOTIFY list, nargs, veto=0
        tst.l   \list
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     \list, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        .rept   \nargs
        move.l  (20+4*\nargs)(%sp), -(%sp)
        .endr
        jsr     (%a0)
        lea     (4*\nargs)(%sp), %sp
        .if     \veto
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        rts
        .else
        bra.s   1b
        .endif
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:
        .endm

| CALLV list, value: an observer list called with one long (an immediate or a
| register the macro does not use: not d0-d1/a0-a2); every register kept.
        .macro  CALLV list, val
        tst.l   \list
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     \list, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  \val, -(%sp)
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:
        .endm

        .section .run, "ax"

| ---- storage -------------------------------------------------------------------
| ev_project_saving: f(void *this, int slot, int ctx). A project is about to
| be written to a slot (0-127; 128 the working copy), at the slot writer's
| entry (SLOT_WRITER, 0x4001692a): a SAVE writes slot N, then 128. The
| save job, on the UI side.
        .globl  core_project_saving
core_project_saving:
        NOTIFY  ev_project_saving, 3
        lea     -52(%sp), %sp
        movem.l %d2-%d4/%a2-%fp, (%sp)
        jmp     SLOT_WRITER + 8

| ev_project_saved: f(void *block, int slot, int ok). The slot writer's card
| write returned (0x40016a02): ok is its result (nonzero: written).
        .globl  core_project_saved
core_project_saved:
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        jsr     SLOT_CARD_WRITE
        lea     12(%sp), %sp
        tst.l   ev_project_saved
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_saved, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        moveq   #0, %d1
        move.b  3(%sp), %d1             | ok: the saved d0's low byte
        move.l  %d1, -(%sp)
        move.l  32(%sp), -(%sp)         | slot
        move.l  32(%sp), -(%sp)         | block
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ev_project_loaded: f(void *job, int err). A project load finished (err 0:
| "LOAD OK"), at the load job's completion (LOAD_DONE, 0x4004f054): the
| working project is the loaded one. The UI side.
        .globl  core_project_loaded
core_project_loaded:
        NOTIFY  ev_project_loaded, 2
        link.w  %fp, #-32
        movea.l 8(%fp), %a0
        jmp     LOAD_DONE + 8

| ev_project_new: f(void *project). The working project was made a new one
| (defaults, empty kits and patterns): NEW_PROJECT (0x40084c00) returned. At
| boot when no working copy survives, and NEW PROJECT in a slot.
        .globl  core_project_new
core_project_new:
        move.l  4(%sp), -(%sp)
        bsr.s   7f
        addq.l  #4, %sp
        NOTIFY  ev_project_new, 1
        rts
7:      lea     -20(%sp), %sp
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     NEW_PROJECT + 8

| ev_pattern_op: f(int op, int temp, int part, int what). A kit, pattern or
| sequence is about to be saved (op 0: PATTERN_SAVE, 0x40016b16) or
| reloaded (op 1: PATTERN_RELOAD, 0x40017c16). temp 1: to or from the
| temp area (the working copy); part 0 what 1 the kit, part 1 what 1 the
| pattern, part 1 what 0 the sequence. The UI task, once per op.
        .globl  core_pattern_save, core_pattern_reload
core_pattern_save:
        moveq   #0, %d0
        bsr.s   pattern_op
        lea     -44(%sp), %sp
        movem.l %d2-%d7/%a2-%a4, (%sp)
        jmp     PATTERN_SAVE + 8
core_pattern_reload:
        moveq   #1, %d0
        bsr.s   pattern_op
        link.w  %fp, #-44
        movem.l %d2-%d7/%a2-%a4, (%sp)
        jmp     PATTERN_RELOAD + 8
| d0 = op; from the stubs above: 8(sp) the routine's this, 12 temp, 16 ctx,
| 20 part, 24 what (bytes in longs).
pattern_op:
        tst.l   ev_pattern_op
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_pattern_op, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        moveq   #0, %d1
        move.b  47(%sp), %d1            | what
        move.l  %d1, -(%sp)
        move.b  47(%sp), %d1            | part
        move.l  %d1, -(%sp)
        move.b  43(%sp), %d1            | temp
        move.l  %d1, -(%sp)
        move.l  12(%sp), -(%sp)         | op: the saved d0
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ev_sample_assign: f(const char *name, int slot, int ok). A sample was put in
| a slot of the project's sample list (SAMPLE_ASSIGN, 0x4001da6a, returned;
| slot as asked: below 0 the first free one; ok nonzero when it went in).
| The sampler and the sample browser, on the UI side.
        .globl  core_sample_assign
core_sample_assign:
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        bsr.s   7f
        lea     12(%sp), %sp
        tst.l   ev_sample_assign
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_sample_assign, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        moveq   #0, %d1
        move.b  3(%sp), %d1             | ok: the result's low byte
        move.l  %d1, -(%sp)
        move.l  36(%sp), -(%sp)         | slot
        move.l  36(%sp), -(%sp)         | name
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts
7:      link.w  %fp, #-28
        movem.l %d2-%d4/%a2, (%sp)
        jmp     SAMPLE_ASSIGN + 8
