| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the edits a mod's own data must follow
| (core-ot 0.8). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| Mods that keep data of their own beside a pattern, a track or a Part (a
| Kit, page-2 locks, a timestretch, a scene mix) each detour the stock routines
| that copy, paste, save, reload and reset those. Octabam's KITS, PLOCKS P2,
| VARISPEED, CXOSC, MIDI SCENES, BATCH BUGFIXES, MIDI PART STATE, BRAIN, BUFFER
| SAVER and STORE detour these places (MIT; the census is theirs, the code is
| our own). Here each is an event:
|
|   0x40026eb0  the pattern copy (pattern -> clipboard)       ev_clip
|   0x40026ef0  the pattern snapshot (pattern -> undo)        ev_clip
|   0x4002b9b0  the pattern store (clipboard or undo -> bank)  ev_clip, may refuse
|   0x40029754  the track copy and snapshot (track -> buffer)  ev_clip
|   0x4002b654  the track store (clipboard or undo -> bank)    ev_clip, may refuse
|   0x4004a908  Part Save's entry                              ev_part_saving(part, 0)
|   0x4004aab4  Part Reload                                    ev_part_reload(how, part)
|   0x400621da  the Part change: before                       ev_part_select
|   0x40062216  the Part change: applied, and done            ev_part_select
|   0x40005638  the Part initialiser's entry                   ev_part_init(1, part)
|   0x40005a38  the Part initialiser's end                     ev_part_init(0, part)
|   0x400909d8  the empty project's RAM, before any load       ev_project_init(job, 0)
|
| None of them is a byte any mod of irpina/octabam2elemod v1.0 patches. All
| but the last run in the sys task (the panel's keys); the Part initialiser
| and the empty project's RAM run in the engine task's project jobs too.
|
| A wrapper that runs after a routine keeps the routine's caller in a slot of
| its own, so it must not be entered again before the routine returns: a
| handler must not call the routine it was called from. Every stub keeps every
| register the stock code relies on.

        .section .run, "ax"

        .set    EV_PART_SAVING, 7       | store.c's numbering (store.s has 0-6)
        .set    EV_PART_RELOAD, 8
        .set    EV_PART_INIT, 9
        .set    EV_PROJECT_INIT, 10

        .set    CL_PCOPY, 0             | edit.c's site numbers
        .set    CL_PSNAP, 1
        .set    CL_PSTORE, 2
        .set    CL_TCOPY, 3
        .set    CL_TSTORE, 4

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
| CLIP site, pre: edit.c's ote_clip(site, pre, args), args the routine's first
| argument (the return address is the long below it). After SAVE4, with the
| routine's return address at 16(sp); leaves d0 = the refusal.
        .macro  CLIP site, pre
        pea     16+4(%sp)
        move.l  #\pre, -(%sp)
        move.l  #\site, -(%sp)
        jsr     ote_clip
        lea     12(%sp), %sp
        .endm

| ---- ev_clip: patterns and tracks through the clipboard and the undo buffer --
| 0x40026eb0(pattern), FUNC+REC's pattern copy (was: move.l 4(sp),d0 ; move.l #0x8ed8,d1).
        .globl  core_clip_pcopy, core_clip_psnap, core_clip_pstore, core_clip_tcopy, core_clip_tstore
core_clip_pcopy:
        SAVE4
        CLIP    CL_PCOPY, 1
        REST4
        move.l  (%sp)+, ote_ret_pcopy
        pea     1f
        move.l  4(%sp), %d0
        move.l  #0x8ed8, %d1
        jmp     CL_PCOPY_NEXT
1:      move.l  ote_ret_pcopy, -(%sp)
        SAVE4
        CLIP    CL_PCOPY, 0
        REST4
        rts

| 0x40026ef0(op, pattern), the snapshot FUNC+STOP and FUNC+PLAY take before they
| change a pattern (was: move.l 8(sp),d0 ; lea 4(sp),a0).
core_clip_psnap:
        SAVE4
        CLIP    CL_PSNAP, 1
        REST4
        move.l  (%sp)+, ote_ret_psnap
        pea     1f
        move.l  8(%sp), %d0
        lea     4(%sp), %a0
        jmp     CL_PSNAP_NEXT
1:      move.l  ote_ret_psnap, -(%sp)
        SAVE4
        CLIP    CL_PSNAP, 0
        REST4
        rts

| 0x4002b9b0(src, pattern), the pattern paste's and undo's store (was: lea -16(sp),sp ;
| movem.l d2-d4/a2,(sp)). It ends in a tail call, whose rts comes back here.
core_clip_pstore:
        SAVE4
        CLIP    CL_PSTORE, 1
        tst.l   %d0
        bne.s   8f
        REST4
        move.l  (%sp)+, ote_ret_pstore
        pea     1f
        lea     -16(%sp), %sp
        movem.l %d2-%d4/%a2, (%sp)
        jmp     CL_PSTORE_NEXT
1:      move.l  ote_ret_pstore, -(%sp)
        SAVE4
        CLIP    CL_PSTORE, 0
        REST4
        rts
| refused (edit.c allows it only from stock's paste and undo): the caller's own
| "nothing done" exit, with the two longs it pushed dropped. d0-d1/a0-a1 are the
| caller's to lose across the call.
8:      lea     16(%sp), %sp
        move.l  (%sp)+, %d1             | the caller's return address
        addq.l  #8, %sp                 | its arguments
        cmpi.l  #CL_PUNDO_RET, %d1
        beq.s   9f
        moveq   #0, %d0                 | the paste: nothing pasted
        rts
9:      jmp     CL_PUNDO_NONE           | the undo: its "nothing to undo" exit

| 0x40029754(dst, pattern, track): a track into the clipboard (the track copy)
| or the undo buffer (the snapshot, which reaches it by a bra) (was: lea -28(sp),sp ;
| movem.l d2-d4/a2-a5,(sp)).
core_clip_tcopy:
        SAVE4
        CLIP    CL_TCOPY, 1
        REST4
        move.l  (%sp)+, ote_ret_tcopy
        pea     1f
        lea     -28(%sp), %sp
        movem.l %d2-%d4/%a2-%a5, (%sp)
        jmp     CL_TCOPY_NEXT
1:      move.l  ote_ret_tcopy, -(%sp)
        SAVE4
        CLIP    CL_TCOPY, 0
        REST4
        rts

| 0x4002b654(src, pattern, track), the track paste's and undo's store (was:
| lea -68(sp),sp ; movem.l d2-d7/a2-a6,(sp)). It ends in a tail call.
core_clip_tstore:
        SAVE4
        CLIP    CL_TSTORE, 1
        tst.l   %d0
        bne.s   8f
        REST4
        move.l  (%sp)+, ote_ret_tstore
        pea     1f
        lea     -68(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     CL_TSTORE_NEXT
1:      move.l  ote_ret_tstore, -(%sp)
        SAVE4
        CLIP    CL_TSTORE, 0
        REST4
        rts
8:      lea     16(%sp), %sp            | refused, as the pattern store's
        move.l  (%sp)+, %d1
        lea     12(%sp), %sp            | the caller's three arguments
        cmpi.l  #CL_TUNDO_RET, %d1
        beq.s   9f
        moveq   #0, %d0
        rts
9:      jmp     CL_TUNDO_NONE

| ---- the Parts ----------------------------------------------------------------
| 0x4004a908(part), Part Save's entry (was: lea -20(sp),sp ; movem.l d2-d4/a2-a3,(sp)).
| SAVE, SAVE ALL (four calls through a2) and Part Clear's tail reach it.
        .globl  core_partsave, core_partreload, core_psel_begin, core_psel_done
        .globl  core_pinit, core_pinit_end, core_projinit
core_partsave:
        SAVE4
        move.l  16+4(%sp), %d0
        moveq   #0, %d1
        CALLEV  EV_PART_SAVING
        REST4
        lea     -20(%sp), %sp
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     PARTSAVE_NEXT

| 0x4004aab4(part) -> d0, Part Reload (was: lea -32(sp),sp ; movem.l d2-d5/a2-a3,(sp)):
| d0 is 0 when the Part has no saved copy ("SAVE PART FIRST!").
core_partreload:
        SAVE4
        moveq   #1, %d0
        move.l  16+4(%sp), %d1
        CALLEV  EV_PART_RELOAD
        REST4
        move.l  (%sp)+, ote_ret_reload
        pea     1f
        lea     -32(%sp), %sp
        movem.l %d2-%d5/%a2-%a3, (%sp)
        jmp     PARTRELOAD_NEXT
1:      move.l  ote_ret_reload, -(%sp)
        SAVE4                           | d0: the routine's result, kept
        tst.l   %d0
        seq     %d0                     | 0 reloaded, 2 nothing to reload
        andi.l  #2, %d0
        move.l  16+4(%sp), %d1
        CALLEV  EV_PART_RELOAD
        REST4
        rts

| 0x400621da (was: jsr 0x40020898): the sys task's Part change (its event 0x14) is
| about to copy the old Part's eight machine bytes to -9(fp). a2 -> the new Part's
| number, 0x80000003 still holds the old. The memcpy returns to the caller itself.
core_psel_begin:
        SAVE4
        clr.l   -(%sp)                  | no machines yet
        mvz.b   (%a2), %d0
        move.l  %d0, -(%sp)             | the new Part
        clr.l   -(%sp)                  | OT_PSEL_BEGIN
        jsr     ote_psel
        lea     12(%sp), %sp
        REST4
        jmp     PSEL_MEMCPY

| 0x40062216 (was: jsr 0x400326a0 ; clr.l -(sp) ; jsr 0x40078850): every track has
| gone to the new Part (0x80000003 and 0x100b14cf hold it), then the open window
| is redrawn and the track's machine page refreshed. The addq at 0x40062224
| drops the long pushed for the refresh.
core_psel_done:
        pea     -9(%fp)                 | the old Part's machine bytes
        clr.l   -(%sp)
        pea     1                       | OT_PSEL_APPLIED
        jsr     ote_psel
        lea     12(%sp), %sp
        jsr     PSEL_REDRAW
        clr.l   -(%sp)
        jsr     PSEL_PAGE
        SAVE4
        pea     -9(%fp)
        clr.l   -(%sp)
        pea     2                       | OT_PSEL_DONE
        jsr     ote_psel
        lea     12(%sp), %sp
        REST4
        jmp     PSEL_NEXT

| 0x40005638(part), the Part initialiser (was: lea -88(sp),sp ; movem.l d2-d7/a2-a6,(sp)):
| a whole Part to its defaults, for the bank init and Part Clear.
core_pinit:
        SAVE4
        moveq   #1, %d0
        move.l  16+4(%sp), %d1
        CALLEV  EV_PART_INIT
        REST4
        lea     -88(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     PINIT_NEXT

| 0x40005a38 (was: move.l a2,92(sp) ; movem.l (sp),d2-d7/a2-a6): a2 = the Part, its
| body done; the routine ends in the Part validator, as a tail call.
core_pinit_end:
        SAVE4
        moveq   #0, %d0
        move.l  %a2, %d1
        CALLEV  EV_PART_INIT
        REST4
        move.l  %a2, 92(%sp)
        movem.l (%sp), %d2-%d7/%a2-%a6
        jmp     PINIT_END_NEXT

| ---- the project ----------------------------------------------------------------
| 0x400909d8, the empty project's RAM (was: move.l a2,-(sp) ; clr.l -(sp) ;
| jsr 0x4000fd34): every bank to empty and the project's defaults, before LOAD
| PROJECT (job 4), a new project (9, 10, 13) and job 12 put anything in them.
core_projinit:
        SAVE4
        move.l  ots_last_job, %d0       | the job running (store.s), -1 none
        moveq   #0, %d1
        CALLEV  EV_PROJECT_INIT
        REST4
        move.l  %a2, -(%sp)
        clr.l   -(%sp)
        jsr     PROJINIT_FN
        jmp     PROJINIT_NEXT

        .section .bss
        .balign 4
ote_ret_pcopy:   .skip 4
ote_ret_psnap:   .skip 4
ote_ret_pstore:  .skip 4
ote_ret_tcopy:   .skip 4
ote_ret_tstore:  .skip 4
ote_ret_reload:  .skip 4
