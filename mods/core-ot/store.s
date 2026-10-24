| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the project's lifecycle and the engine
| task (core-ot 0.6). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The engine task (priority 1, 16 KB of stack) runs the firmware's project
| jobs one at a time from its queue (0x460d17ce): its loop receives a message
| at 0x4008484e and switches on the message's first byte, the job's type, at
| 0x4008485e (0..45; above that, back to the receive). Core takes two places
| in that loop:
|
|   0x4008485e  the switch: type OTS_JOB is core's own (ot_engine_post, store.c),
|               any other is noted as the job now running and goes on to stock
|   0x4008484e  the receive: the job noted has ended (every job goes back here),
|               so ev_project fires for the ones that load or make a project
|
| and the routines those jobs call, which every caller reaches directly (a
| byte search of the image finds only their call operands):
|
|   0x40090504  the load of every bank            ev_banks_load(pre, 0xffff)
|   0x400905d4  a masked bank load                 ev_banks_load(pre, mask)
|   0x400918aa  inside the bank writer, after the project folder is made,
|               before the first bank file          ev_banks_write(1, mask)
|   0x400917c8  the bank writer, after it           ev_banks_write(0, mask)
|   0x4008ee74  the project store (.work -> .strd)  ev_project_store(pre)
|   0x4008f180  the project reload (.strd -> .work) ev_project_reload(pre)
|   0x4000faf0  the current bank into CS1           ev_cs1_bank(1, bank), before
|   0x40025808  the power-up's bank from CS1        ev_cs1_bank(0, bank), after
|   0x4004a9c4  the Part Save's tail                ev_part_saved(part)
|   0x4004a9d0  the Part Clear's entry              ev_part_cleared(part)
|
| These are the sites octabam's STORE module holds for its clients (MIT; the
| census and the displaced bytes are its findings); the code is our own.
| None of them is a byte any mod of irpina/octabam2elemod v1.0 patches.
|
| A wrapper that runs after a routine keeps the routine's caller in a slot of
| its own, so it must not be entered again before the routine returns: the
| engine task is the only caller of the load, write, store and reload
| routines, and the Part routines are the UI task's.
|
| Each stub keeps every register: store.c's ots_event(ev, a, b) does core's
| own part, then calls the event's handlers.

        .section .run, "ax"

        .set    OTS_JOB, 0x42           | core's job type (stock's are 0..45; octabam's BRAIN uses 0x41)

        .set    EV_BANKS_LOAD, 0        | store.c's numbering
        .set    EV_BANKS_WRITE, 1
        .set    EV_PROJECT_STORE, 2
        .set    EV_PROJECT_RELOAD, 3
        .set    EV_CS1_BANK, 4
        .set    EV_PART_SAVED, 5
        .set    EV_PART_CLEARED, 6

        .macro  SAVE4
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        .endm
        .macro  REST4
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        .endm
| CALLEV ev: ots_event(ev, d0, d1). After SAVE4; leaves d0-d1/a0-a1 changed.
        .macro  CALLEV ev
        move.l  %d1, -(%sp)
        move.l  %d0, -(%sp)
        move.l  #\ev, -(%sp)
        jsr     ots_event
        lea     12(%sp), %sp
        .endm

| ---- the engine task's loop ---------------------------------------------------
| 0x4008485e (was: mvz.b (%a2),%d0 ; moveq #45,%d1 ; cmp.l %d0,%d1): a2 = the message.
        .globl  core_job_switch
core_job_switch:
        mvz.b   (%a2), %d0
        cmpi.l  #OTS_JOB, %d0
        bne.s   1f
        SAVE4
        jsr     ots_job_run             | core's queue (store.c)
        REST4
        jmp     JOB_LOOP                | the next message
1:      move.l  %d0, ots_last_job       | the job about to run
        moveq   #45, %d1                | the instructions this replaced: their flags
        cmp.l   %d0, %d1                | for stock's bcs
        jmp     JOB_SWITCH_NEXT

| 0x4008484e (was: pea 0x460d17ce), the receive every job goes back to. d0-d1/a0-a1
| are free here: the receive's call takes them.
        .globl  core_job_loop
core_job_loop:
        tst.l   ots_last_job
        bmi.s   1f
        jsr     ots_job_end             | the job noted has ended (store.c)
1:      pea     ENGINE_Q                | the instruction this replaced
        jmp     JOB_LOOP_NEXT

| ---- the bank loads -----------------------------------------------------------
| 0x40090504 (was: lea -328(%sp),%sp ; movem.l %d2-%d7/%a2-%a6,(%sp)): every bank.
        .globl  core_loadall
core_loadall:
        SAVE4
        moveq   #1, %d0
        move.l  #0xffff, %d1
        CALLEV  EV_BANKS_LOAD
        REST4
        move.l  (%sp)+, ots_ret_loadall
        pea     1f
        lea     -328(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     LOADALL_NEXT
1:      move.l  ots_ret_loadall, -(%sp)
        SAVE4                           | d0: the routine's result, kept
        moveq   #0, %d0
        move.l  #0xffff, %d1
        CALLEV  EV_BANKS_LOAD
        REST4
        rts

| 0x400905d4, the same two instructions: (sp) = return, 4 the bank RAM, the
| mask is the word at 10.
        .globl  core_loadmask
core_loadmask:
        SAVE4
        mvz.w   16+10(%sp), %d1
        move.l  %d1, ots_mask_load
        moveq   #1, %d0
        CALLEV  EV_BANKS_LOAD
        REST4
        move.l  (%sp)+, ots_ret_loadmask
        pea     1f
        lea     -328(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     LOADMASK_NEXT
1:      move.l  ots_ret_loadmask, -(%sp)
        SAVE4
        moveq   #0, %d0
        move.l  ots_mask_load, %d1
        CALLEV  EV_BANKS_LOAD
        REST4
        rts

| ---- the bank writer ----------------------------------------------------------
| 0x400917c8 (was: link %fp,#-324 ; movem.l %d2-%d7/%a2-%a5,(%sp)).
        .globl  core_bankw, core_bankw_mid
core_bankw:
        move.l  (%sp)+, ots_ret_bankw
        pea     1f
        link    %fp, #-324
        movem.l %d2-%d7/%a2-%a5, (%sp)
        jmp     BANKW_NEXT
1:      move.l  ots_ret_bankw, -(%sp)
        SAVE4
        moveq   #0, %d0
        move.l  ots_mask_write, %d1
        clr.l   ots_mask_write
        CALLEV  EV_BANKS_WRITE
        REST4
        rts

| 0x400918aa (was: lea 0x400e21e0,%a2): d6's low word holds the banks it writes.
core_bankw_mid:
        SAVE4
        mvz.w   %d6, %d1
        move.l  %d1, ots_mask_write
        moveq   #1, %d0
        CALLEV  EV_BANKS_WRITE
        REST4
        lea     0x400e21e0, %a2
        jmp     BANKW_MID_NEXT

| ---- the project store and reload ----------------------------------------------
| 0x4008ee74 and 0x4008f180 (was: link %fp,#-560 ; movem.l %d2-%d7/%a2-%a5,(%sp)).
        .globl  core_pstore, core_preload
core_pstore:
        SAVE4
        moveq   #1, %d0
        moveq   #0, %d1
        CALLEV  EV_PROJECT_STORE
        REST4
        move.l  (%sp)+, ots_ret_pstore
        pea     1f
        link    %fp, #-560
        movem.l %d2-%d7/%a2-%a5, (%sp)
        jmp     PSTORE_NEXT
1:      move.l  ots_ret_pstore, -(%sp)
        SAVE4
        moveq   #0, %d0
        moveq   #0, %d1
        CALLEV  EV_PROJECT_STORE
        REST4
        rts

core_preload:
        SAVE4
        moveq   #1, %d0
        moveq   #0, %d1
        CALLEV  EV_PROJECT_RELOAD
        REST4
        move.l  (%sp)+, ots_ret_preload
        pea     1f
        link    %fp, #-560
        movem.l %d2-%d7/%a2-%a5, (%sp)
        jmp     PRELOAD_NEXT
1:      move.l  ots_ret_preload, -(%sp)
        SAVE4
        moveq   #0, %d0
        moveq   #0, %d1
        CALLEV  EV_PROJECT_RELOAD
        REST4
        rts

| ---- the bank and CS1 ---------------------------------------------------------
| 0x4000faf0(bank) (was: move.l %a2,-(%sp) ; move.l %d2,-(%sp) ; move.l #0x8ed80,-(%sp)).
        .globl  core_tocs1, core_fromcs1
core_tocs1:
        SAVE4
        moveq   #1, %d0
        move.l  16+4(%sp), %d1
        CALLEV  EV_CS1_BANK
        REST4
        move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        move.l  #0x8ed80, -(%sp)
        jmp     TOCS1_NEXT

| 0x40025808 (was: jsr 0x4000fbb4), the power-up's call: (sp) = return, 4 the bank.
core_fromcs1:
        move.l  4(%sp), -(%sp)
        jsr     FROMCS1_FN
        addq.l  #4, %sp
        SAVE4                           | d0: the call's result, kept
        moveq   #0, %d0
        move.l  16+4(%sp), %d1
        CALLEV  EV_CS1_BANK
        REST4
        rts

| ---- the Parts ----------------------------------------------------------------
| 0x4004a9c4 (was: movem.l (%sp),%d2-%d4/%a2-%a3 ; lea 20(%sp),%sp ; rts): d4 = the part.
| Stock's Part Clear ends in its Part Save, so a clear is followed by a save.
        .globl  core_partsaved, core_partclear
core_partsaved:
        SAVE4
        move.l  %d4, %d0
        moveq   #0, %d1
        CALLEV  EV_PART_SAVED
        REST4
        movem.l (%sp), %d2-%d4/%a2-%a3
        lea     20(%sp), %sp
        rts

| 0x4004a9d0(part) (was: lea -16(%sp),%sp ; movem.l %d2-%d3/%a2-%a3,(%sp)).
core_partclear:
        SAVE4
        move.l  16+4(%sp), %d0
        moveq   #0, %d1
        CALLEV  EV_PART_CLEARED
        REST4
        lea     -16(%sp), %sp
        movem.l %d2-%d3/%a2-%a3, (%sp)
        jmp     PARTCLEAR_NEXT

        .section .bss
        .balign 4
ots_ret_loadall:  .skip 4
ots_ret_loadmask: .skip 4
ots_ret_bankw:    .skip 4
ots_ret_pstore:   .skip 4
ots_ret_preload:  .skip 4
ots_mask_load:    .skip 4
ots_mask_write:   .skip 4
