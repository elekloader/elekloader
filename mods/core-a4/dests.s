| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4 0.5: the destination sites of the mod parameters (dests.c). Each is
| a jmp site whose displaced instructions are done here, then a jump back
| into stock (OS 1.56), or the routine's own return.
        .text
        .globl  core_dst_list_end, core_id_index, core_index_id, core_dst_check
        .globl  core_push_synth_end, core_push_fx_end, core_push_cv_end
        .globl  core_push_routes_end, core_push_macro_end, core_push_knob_end
        .align  2

        .equ    PSEUDO, 0x127

| 0x400a11a4, the destination list builder's end (0x400a1108; the result's
| vector at (%a2), the set at 24(%sp), the mask at 28(%sp)): `move.l %a2,%d0;
| movem.l (%sp),%a2-%a5`.
core_dst_list_end:
        move.l  28(%sp),-(%sp)
        move.l  28(%sp),-(%sp)
        move.l  (%a2),-(%sp)
        jsr     core_dst_append
        lea     12(%sp),%sp
        move.l  %a2,%d0
        movem.l (%sp),%a2-%a5
        jmp     0x400a11aa

| 0x40007632, a parameter id's engine index: `move.l 4(%sp),%d0; cmpi.l
| #0x127,%d0`. A "mod" pseudo id's index is its code.
core_id_index:
        move.l  4(%sp),%d0
        cmp.l   #PSEUDO,%d0
        bcs.s   1f
        move.l  %d0,-(%sp)
        jsr     core_dst_code
        addq.l  #4,%sp
        tst.l   %d0
        bge.s   2f
        move.l  4(%sp),%d0
        cmp.l   #PSEUDO,%d0
1:      jmp     0x4000763c
2:      rts

| 0x400077d2, (record kind, engine index) to id: `move.l %d2,-(%sp); moveq
| #111,%d2; move.l 8(%sp),%d0`. An index above 0x6f, which stock answers 0,
| is a code: its pseudo id.
core_index_id:
        move.l  8(%sp),%d1
        moveq   #0x70,%d0
        cmp.l   %d0,%d1
        bcs.s   1f
        move.l  %d1,-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_dst_id
        addq.l  #8,%sp
        rts
1:      move.l  %d2,-(%sp)
        moveq   #111,%d2
        move.l  8(%sp),%d0
        jmp     0x400077da

| 0x4009f6ca, in the Dest value check (0x4009f650; the set in %a2, the
| value's id in %d0): `cmpi.l #0x126,%d0` (then bhi: refused). A "mod"
| pseudo id of the set's kind passes.
core_dst_check:
        cmp.l   #PSEUDO,%d0
        bcs.s   1f
        move.l  %d0,-(%sp)
        move.l  %a2,-(%sp)
        jsr     core_dst_legal
        addq.l  #8,%sp
        tst.l   %d0
        beq.s   2f
        jmp     0x4009f6b6
2:      jmp     0x4009f728
1:      cmp.l   #0x126,%d0
        jmp     0x4009f6d0

| 0x40100b0e, the synth sound push's return (0x40100a04(sound, voice,
| track), its frame in %fp): `movem.l -56(%fp),%d2-%d7/%a2-%a5`.
core_push_synth_end:
        move.l  12(%fp),-(%sp)
        move.l  8(%fp),-(%sp)
        jsr     core_dst_push_synth
        addq.l  #8,%sp
        movem.l -56(%fp),%d2-%d7/%a2-%a5
        unlk    %fp
        rts

| 0x40100eb4, the FX push's return (0x40100d84(fx)): `movem.l
| -48(%fp),%d2-%d5/%a2-%a5`.
core_push_fx_end:
        move.l  8(%fp),-(%sp)
        jsr     core_dst_push_fx
        addq.l  #4,%sp
        movem.l -48(%fp),%d2-%d5/%a2-%a5
        unlk    %fp
        rts

| 0x401010dc, the CV push's return (0x40100fb8(cv), the block at 48(%sp)):
| `movem.l (%sp),%d2-%d3/%a2-%a6; lea 44(%sp),%sp`.
core_push_cv_end:
        move.l  48(%sp),-(%sp)
        jsr     core_dst_push_cv
        addq.l  #4,%sp
        movem.l (%sp),%d2-%d3/%a2-%a6
        lea     44(%sp),%sp
        rts

| 0x40100cf4, the route push's return (0x40100baa(sound, voice) at 28(%sp),
| 32(%sp)): `movem.l (%sp),%d2-%d3/%a2-%a5; lea 24(%sp),%sp`.
core_push_routes_end:
        move.l  32(%sp),-(%sp)
        move.l  32(%sp),-(%sp)
        jsr     core_dst_push_routes
        addq.l  #8,%sp
        movem.l (%sp),%d2-%d3/%a2-%a5
        lea     24(%sp),%sp
        rts

| 0x401012e2, the performance push's return (0x401011e0(entry, n) at
| 28(%sp), 32(%sp)): the same instructions.
core_push_macro_end:
        move.l  32(%sp),-(%sp)
        move.l  32(%sp),-(%sp)
        jsr     core_dst_push_macro
        addq.l  #8,%sp
        movem.l (%sp),%d2-%d3/%a2-%a5
        lea     24(%sp),%sp
        rts

| 0x40100678, the knob push's return (0x401005b8(value, track, index, dest)
| at 36(%sp)..48(%sp)): `movem.l (%sp),%d2-%d6/%a2-%a4; lea 32(%sp),%sp`.
core_push_knob_end:
        move.l  48(%sp),-(%sp)
        move.l  48(%sp),-(%sp)
        move.l  48(%sp),-(%sp)
        move.l  48(%sp),-(%sp)
        jsr     core_dst_knob
        lea     16(%sp),%sp
        movem.l (%sp),%d2-%d6/%a2-%a4
        lea     32(%sp),%sp
        rts

| 0x400f3630, 0x400f368e, 0x400f36ec, the synth, FX and CV Dest knob's value
| text (the target's id in %d0): `lea 0x40186af4,%a0`, then the record's
| short name pushed for "%s". A "mod" pseudo id pushes its short name.
        .globl  core_dst_text_synth, core_dst_text_fx, core_dst_text_cv
        .globl  core_dst_list_label, core_dst_setup_label
        .macro  dst_text back, push
        cmp.l   #PSEUDO,%d0
        bcs.s   1f
        move.l  %d0,-(%sp)
        jsr     core_pseudo_short
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   2f
        move.l  %d0,-(%sp)
        jmp     \push
2:      moveq   #0,%d0
1:      lea     0x40186af4,%a0
        jmp     \back
        .endm
core_dst_text_synth:
        dst_text 0x400f3636, 0x400f364e
core_dst_text_fx:
        dst_text 0x400f3694, 0x400f36ac
core_dst_text_cv:
        dst_text 0x400f36f2, 0x400f370a

| 0x40035a84, a destination list's item label (the id in %d0; %d1 and %d3 on
| the way to id x 0x38 for its category): `lea 0x40186af4,%a0`, then
| "%.16s:%.32s" with the record's category and long name. A "mod" pseudo id
| gives its category (MOD when it has none) and name.
core_dst_list_label:
        cmp.l   #PSEUDO,%d0
        bcs.s   1f
        subq.l  #8,%sp
        pea     4(%sp)
        pea     4(%sp)
        move.l  %d0,-(%sp)
        jsr     core_pseudo_names
        lea     12(%sp),%sp
        tst.l   %d0
        beq.s   2f
        move.l  (%sp),%d1                     | name, category -> category, name
        move.l  4(%sp),(%sp)
        move.l  %d1,4(%sp)
        jmp     0x40035aa8
2:      addq.l  #8,%sp
        moveq   #0,%d0
        moveq   #0,%d1
        moveq   #0,%d3
1:      lea     0x40186af4,%a0
        jmp     0x40035a8a

| 0x40036a0a, a route's, macro's or CONTROL IN's target on its setup screen
| (the id in %d6): `cmpi.l #0x126,%d6`, then the record's short name into
| 44(%sp) and its category into %d6 from 0x34(%a0, id x 0x38). A "mod"
| pseudo id gives its short name and category.
core_dst_setup_label:
        cmp.l   #PSEUDO,%d6
        bcs.s   1f
        move.l  %d6,-(%sp)
        jsr     core_pseudo_setup
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   2f
        move.l  %d0,44(%sp)
        lea     core_dst_category-0x34,%a0
        moveq   #0,%d6
        jmp     0x40036a42
2:      moveq   #0,%d6
1:      cmp.l   #0x126,%d6
        jmp     0x40036a10
