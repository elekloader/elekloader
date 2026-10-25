| SPDX-License-Identifier: GPL-2.0-or-later
| machines-ot for the Octatrack, OS 1.40C: the places in the firmware that ask
| the machines (machines.c). ColdFire V4 (MCF5445x); assemble with
| -mcpu=54455.
|
| 0.2 adds the tick (0x4005221e), the chooser's level (0x400788c4) and the slot
| assigner's commit (0x40079520).
|
| A machine's track stays a FLEX track; the places below that read a track's
| machine byte read it as the machine's row (5 on) instead, the places that
| bound the rows at stock's five bound them at 5 + n, and the readers of
| stock's name and page tables read grown copies (otm_names, otm_pages). Each
| stub replays what its site displaced, then goes back to stock. A stub that
| calls C keeps the registers the code after it still needs (C keeps
| d2-d7/a2-a6 itself).
|
| No site is a byte octabam2elemod v1.0's mods write, SYNTH MACHINE's
| included: its renderer entry (0x400d6438) and resolver tail (0x40031ece)
| stay its own, and the companion chains behind both (the FLEX renderer at
| 0x40004008 and the resolver's case 0 at 0x40031e74).

        .section .run, "ax"

        .set    PB_SEL, 0x460d5c30      | SRC SETUP's chosen row (a long)
        .set    MIRROR_TYPES, 0x100a4ef0 | the Parts' battery-backed machine bytes
        .set    LIST, 0x460e7386        | the machine chooser's list

| INIT: the tables are complete (machines.c's otm_init has run); every register kept.
        .macro  INIT
        tst.b   otm_ready
        bne.s   9f
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        jsr     otm_init
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
9:
        .endm

| TYPE fn: d0 = fn(the byte at (a0) sign-extended, a0); d1/a0/a1 kept.
        .macro  TYPE fn
        lea     -12(%sp), %sp
        movem.l %d1/%a0-%a1, (%sp)
        mvs.b   (%a0), %d0
        move.l  %a0, -(%sp)
        move.l  %d0, -(%sp)
        jsr     \fn
        addq.l  #8, %sp
        movem.l (%sp), %d1/%a0-%a1
        lea     12(%sp), %sp
        .endm

| ---- the Part validator: int validate(Part *) ---------------------------------
| 0x40002318 jmp otm_validate (lea -96(%sp),%sp ; movem.l %d2-%d7/%a2-%fp,(%sp)):
| the site jumps into machines.c with the caller's stack; this is stock's body.
        .globl  otm_stock_validate
otm_stock_validate:
        lea     -96(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     0x40002320

| ---- the FLEX renderer: int render(track, ping, start, end) -------------------
| 0x40004008 jmp otm_render (lea -48(%sp),%sp ; movem.l ...): STATIC's, FLEX's
| and PICKUP's renderer, from the kind table (SYNTH MACHINE's own renderer
| calls it too). This is stock's.
        .globl  ot_stock_render
ot_stock_render:
        lea     -48(%sp), %sp
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     0x40004010

| ---- the page resolver, case 0 (the track's PLAYBACK page) --------------------
| 0x40031e74 jmp (mvs.b %d5,%d0 ; lea 0x400d5f38,%a0 ; bra.s 0x40031ece): a0 is
| the track's machine byte, d5 that byte. An owned track's page -> the
| resolver's exit with it in d0; else stock's load from the grown table. d1
| (the part) and a1 (the bank) are kept for the hooks at the load (SYNTH
| MACHINE's reads them there).
        .globl  otm_resolve
otm_resolve:
        lea     -8(%sp), %sp
        movem.l %d1/%a1, (%sp)
        move.l  %a0, -(%sp)
        jsr     otm_page
        addq.l  #4, %sp
        movem.l (%sp), %d1/%a1
        lea     8(%sp), %sp
        tst.l   %d0
        beq.s   1f
        jmp     0x40031ed6
1:      mvs.b   %d5, %d0
        lea     otm_pages, %a0
        jmp     0x40031ece

| ---- name(row) -----------------------------------------------------------------
| 0x400334d8 jmp (move.l %d2,-(%sp) ; move.l 8(%sp),%d1): rows 5 on are the
| machines' names (the chooser draws each row's name with it).
        .globl  otm_name_fn
otm_name_fn:
        move.l  4(%sp), %d0
        moveq   #5, %d1
        cmp.l   %d1, %d0
        bcs.s   1f
        move.l  %d0, -(%sp)
        jsr     otm_name
        addq.l  #4, %sp
        rts
1:      move.l  %d2, -(%sp)
        move.l  8(%sp), %d1
        jmp     0x400334de

| ---- names by machine byte -----------------------------------------------------
| 0x4003d718 and 0x4004c36a jmp (lea 0x400a78c8,%a0), before stock's
| move.l (%a0,%d0.l*4),%d1: d0 is the machine byte (0..4), a0 its address.
| -> d0 the row, d1 its name, a0 the grown table, past stock's load.
        .macro  NAMES resume
        move.l  %a1, -(%sp)             | the code after reads a1
        move.l  %a0, -(%sp)
        move.l  %d0, -(%sp)
        jsr     otm_type
        addq.l  #8, %sp
        movea.l (%sp)+, %a1
        lea     otm_names, %a0
        move.l  (%a0,%d0.l*4), %d1
        jmp     \resume
        .endm
        .globl  otm_name_a, otm_name_b
otm_name_a:
        NAMES   0x4003d722
otm_name_b:
        NAMES   0x4004c374

| ---- SRC SETUP's machine list --------------------------------------------------
| 0x4003c928 keep2: lea otm_names,%a5 (was 0x400a78c8). 0x4003c950 jmp
| (moveq #4,%d1 ; cmp.l %d2,%d1 ; bcs.s 0x4003c95a ; move.l (%a5,%d2.l*4),%d0):
| a row past the last keeps d0, stock's "no name".
        .globl  otm_setup_name
otm_setup_name:
        INIT
        move.l  otm_last, %d1
        cmp.l   %d2, %d1
        bcs.s   1f
        move.l  (%a5,%d2.l*4), %d0
1:      jmp     0x4003c95a

| 0x4003c980 jmp (mvs.b (%a0),%d0 ; lea 24(%sp),%sp): the track's row.
        .globl  otm_setup_row
otm_setup_row:
        TYPE    otm_type
        lea     24(%sp), %sp
        jmp     0x4003c986

| 0x4003a52e jmp, 0x4003cd98 jmp (the row's column, row * 6): a machine's row
| edits and draws FLEX's bytes.
        .globl  otm_setup_edit, otm_setup_draw
otm_setup_edit:
        moveq   #5, %d0
        cmp.l   %d0, %d2
        bcs.s   1f
        moveq   #1, %d2
1:      move.l  %d2, %d0
        lsl.l   #3, %d0
        add.l   %d2, %d2
        sub.l   %d2, %d0
        jmp     0x4003a536
otm_setup_draw:
        moveq   #5, %d7
        cmp.l   %d7, %d6
        bcs.s   1f
        moveq   #1, %d6
1:      move.l  %d6, %d7
        lsl.l   #3, %d7
        add.l   %d6, %d6
        sub.l   %d6, %d7
        jmp     0x4003cda0

| 0x400585dc jmp (move.b (%a0),%d3 ; mvs.b %d3,%d4 ; pea 0x400bb704): opening
| SRC SETUP: d3/d4 the track's row (0x400585ea's page load is the grown table's).
        .globl  otm_setup_open
otm_setup_open:
        TYPE    otm_type
        move.b  %d0, %d3
        move.l  %d0, %d4
        pea     0x400bb704
        jmp     0x400585e6

| 0x400585fa jmp (pea 5 ; pea 5): the list's rows, 5 + n (5 shown at once).
        .globl  otm_setup_rows
otm_setup_rows:
        INIT
        move.l  otm_total, -(%sp)
        move.l  #5, -(%sp)
        jmp     0x40058602

| ---- the commits: the chosen row becomes the track's machine -------------------
| 0x4005a616 and 0x4005a850 jmp (move.l 0x460d5c30,%d1), before stock stores d1
| in the track's machine byte and its battery copy: a1 is the bank, d0
| part * 6322, d2 the track, a0 the track's machine byte. d1 = otm_assign's
| byte, or the byte the track has when it refuses.
        .macro  SRC_COMMIT resume
        lea     -12(%sp), %sp
        movem.l %d0/%a0-%a1, (%sp)
        move.l  PB_SEL, -(%sp)
        move.l  %d2, -(%sp)
        move.l  %a1, %d1
        add.l   %d0, %d1
        addi.l  #0x8ed80, %d1
        move.l  %d1, -(%sp)
        jsr     otm_assign
        lea     12(%sp), %sp
        move.l  %d0, %d1
        movem.l (%sp), %d0/%a0-%a1
        lea     12(%sp), %sp
        tst.l   %d1
        bpl.s   1f
        mvs.b   (%a0), %d1
1:      jmp     \resume
        .endm
        .globl  otm_src_commit, otm_src_commit2
otm_src_commit:
        SRC_COMMIT 0x4005a61c
otm_src_commit2:
        SRC_COMMIT 0x4005a856

| 0x4007981e jmp (move.b %d4,(%a0) ; add.l %d1,%d0 ; lea 0x100a4ef0,%a0): the
| machine chooser's commit. a1 is the bank, d0 part * 6322, d1 the track, d4 the
| chosen row, a0 the track's machine byte (d3 its old value). A refusal goes
| where stock goes for a row past its five, storing nothing.
        .globl  otm_main_commit
otm_main_commit:
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %d4, -(%sp)
        move.l  %d1, -(%sp)
        move.l  %a1, %d1
        add.l   %d0, %d1
        addi.l  #0x8ed80, %d1
        move.l  %d1, -(%sp)
        jsr     otm_assign
        lea     12(%sp), %sp
        tst.l   %d0
        bmi.s   2f
        move.l  %d0, %d4
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        move.b  %d4, (%a0)
        add.l   %d1, %d0
        lea     MIRROR_TYPES, %a0
        jmp     0x40079828
2:      movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        jmp     0x4007989c

| ---- the machine chooser --------------------------------------------------------
| 0x40079248 jmp (pea 5 ; pea 6): its rows, 5 + n (6 shown at once).
        .globl  otm_chooser_rows
otm_chooser_rows:
        INIT
        move.l  otm_total, -(%sp)
        move.l  #6, -(%sp)
        jmp     0x40079250

| 0x400786c8 jmp (mvs.b (%a0),%d0 ; cmp.l %d0,%d2 ; bne.s 0x400786fc): is the
| window's row d2 the current track's? (a0: the track's machine byte; the
| window shows rows from the list's top on, which stock, with five rows, kept
| at 0)
        .globl  otm_chooser_row
otm_chooser_row:
        TYPE    otm_chooser_type
        sub.l   LIST, %d0
        cmp.l   %d0, %d2
        beq.s   1f
        jmp     0x400786fc
1:      jmp     0x400786ce

| 0x40078886 jmp (mvs.b (%a0),%d0 ; move.l %d0,-(%sp) ; pea 0x460e7386): the
| cursor opens on the track's row.
        .globl  otm_chooser_open
otm_chooser_open:
        TYPE    otm_type
        move.l  %d0, -(%sp)
        pea     LIST
        jmp     0x40078890

| 0x40078678 jmp (moveq #4,%d0 ; cmp.l %d2,%d0 ; blt.s 0x400786a2 ;
| move.l %d2,-(%sp) ; movea.l %d3,%a0 ; jsr (%a0)): the window's row d2 shows
| the name of row top + d2 (d3: the name formatter, name(row)), up to the last
| row. Stock drew name(d2) with no top: past six rows the window scrolls.
        .globl  otm_chooser_draw, otm_chooser_hilite
otm_chooser_draw:
        INIT
        move.l  LIST, %d0
        add.l   %d2, %d0
        cmp.l   otm_last, %d0
        bgt.s   1f
        move.l  %d0, -(%sp)             | in place of stock's push of d2: the same stack
        movea.l %d3, %a0
        jsr     (%a0)
        jmp     0x40078684
1:      jmp     0x400786a2
| 0x400786ce jmp (moveq #4,%d0 ; cmp.l %d2,%d0 ; blt.s 0x400786fc): the
| window's row d2 is highlighted.
otm_chooser_hilite:
        INIT
        move.l  otm_last, %d0
        cmp.l   %d2, %d0
        blt.s   1f
        jmp     0x400786d4
1:      jmp     0x400786fc

| 0x40079904 jmp (moveq #4,%d3 ; cmp.l %d4,%d3 ; bge.w 0x400797cc): YES on row
| d4 commits it up to the last row.
        .globl  otm_chooser_cursor
otm_chooser_cursor:
        INIT
        move.l  otm_last, %d3
        cmp.l   %d4, %d3
        blt.s   1f
        jmp     0x400797cc
1:      jmp     0x4007990c

| 0x40078c22, 0x40078cb0, 0x40078d2e, 0x40078dee, 0x40078e7c jmp (moveq #5,%d1 ;
| cmp.l %d0,%d1 ; bge.s +2 ; moveq #5,%d0): the list's absolute row (d0) is
| kept in 0..otm_clamp (stock's 5 up to one machine, the last row past it).
        .macro  CLAMP resume
        INIT
        move.l  otm_clamp, %d1
        cmp.l   %d0, %d1
        bge.s   1f
        move.l  %d1, %d0
1:      jmp     \resume
        .endm
        .globl  otm_clamp1, otm_clamp2, otm_clamp3, otm_clamp4, otm_clamp5
otm_clamp1:
        CLAMP   0x40078c2a
otm_clamp2:
        CLAMP   0x40078cb8
otm_clamp3:
        CLAMP   0x40078d36
otm_clamp4:
        CLAMP   0x40078df6
otm_clamp5:
        CLAMP   0x40078e84

| ---- the tick (0.2) ----------------------------------------------------------------
| 0x4005221e jmp (jsr 0x4005213c ; jsr 0x4007e940), in the UI task's 60 Hz tick body
| 0x40052200, before the screen's builders after it read the page table: the
| machines' ui_tick, then each per-track page of the current track's machine
| (machines.c's otm_tick). The beq at 0x40052214 lands on the site's first
| byte. Octabam's ANALOG BD, OCTA303, CXOSC and BLACKPYRAMIDS all publish their
| page here (MIT); Modwerk's base has its tick hook here too.
        .globl  otm_tick_seam
otm_tick_seam:
        jsr     0x4005213c              | the two calls this replaced
        jsr     0x4007e940
        jsr     otm_tick
        jmp     0x40052228

| ---- the chooser's level (0.2) -----------------------------------------------------
| 0x400788c4 jmp (mvz.b (%a0),%d0 ; moveq #1,%d2 ; cmp.l %d0,%d2): the machine
| window opens on the sample-slot list for a STATIC or FLEX track (the byte at
| (a0) is 0 or 1), on the machine list for any other. A machine with
| OT_MACHINE_LIST_FIRST has its tracks read as its row, so they open on the list.
        .globl  otm_chooser_level
otm_chooser_level:
        TYPE    otm_level
        moveq   #1, %d2
        cmp.l   %d0, %d2
        jmp     0x400788ca

| ---- the slot assigner's commit (0.2) ----------------------------------------------
| 0x40079520 jmp (mvs.b (%a0),%d4 ; move.b %d2,(%a0) ; add.l %a1,%d0): a sample
| slot picked in either window's list makes the track's machine d2 (0 STATIC,
| 1 FLEX, 4 PICKUP). a0 is the track's machine byte, a1 the track, a2 the bank,
| d0 part * 6322; d4 keeps the old byte for stock. When the byte changes,
| machines.c's otm_assign is told (a machine's track that becomes STATIC is
| unsigned, and ev_machine_set fires); stock's stores follow.
        .globl  otm_slot_commit
otm_slot_commit:
        mvs.b   (%a0), %d4
        cmp.b   %d4, %d2
        beq.s   1f
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        mvz.b   %d2, %d1
        move.l  %d1, -(%sp)             | the new byte, as a row (0-4)
        move.l  %a1, -(%sp)             | the track
        move.l  %a2, %d1
        add.l   %d0, %d1
        addi.l  #0x8ed80, %d1
        move.l  %d1, -(%sp)             | the Part
        jsr     otm_assign
        lea     12(%sp), %sp
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
1:      move.b  %d2, (%a0)
        add.l   %a1, %d0
        jmp     0x40079526

| ---- the parameter page by machine byte -----------------------------------------
| 0x4003bff0 jmp (mvs.b (%a0),%d0 ; lea 0x400d5f38,%a0 ; bra.s 0x4003c054): a0
| the track's machine byte; its row's page from the grown table. (0x4003a524,
| 0x4003c8ca, 0x4003d134 and 0x400585ea are keep2 sites: lea otm_pages.)
        .globl  otm_pmtr
otm_pmtr:
        TYPE    otm_type
        lea     otm_pages, %a0
        jmp     0x4003c054
