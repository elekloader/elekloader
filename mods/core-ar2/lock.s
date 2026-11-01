| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.8: the LFO destination sites of the lockable mod parameters
| (lock.c). Each is a jmp site whose displaced instructions are done here,
| then a jump back into stock (OS 1.74).
        .text
        .globl  core_lfo_apply, core_id_slot, core_dst_list_end, core_dst_label, core_dst_text
        .globl  core_rand_param, core_reload_body
        .align  2

        .equ    PSEUDO, 469

| 0x4009eac0, in the LFO block (one pass a track; the track's number at
| 48(%sp), its struct in %a3, the slot sign-extended in %d4, the LFO's value
| in %d0): `moveq #41,%d1; move.l %d4,32(%a2); cmp.l %d4,%d1; bcs.s skip`.
| A stock slot clears the track's mod code and goes on; a slot above 41 takes
| the depth as stock would (0x4009eacc..0x4009eae2) and keeps the code and
| the offset for core_lfo_offset, then skips the add as stock does.
core_lfo_apply:
        move.l  %d4,32(%a2)
        moveq   #41,%d1
        cmp.l   %d4,%d1
        bcs.s   1f
        move.l  48(%sp),%d1
        lea     core_lfo_code,%a0
        clr.b   (%a0,%d1.l)
        jmp     0x4009eaca
1:      move.l  48(%sp),%d1
        lea     core_lfo_code,%a0
        move.b  %d4,(%a0,%d1.l)
        lea     core_lfo_value,%a0
        add.l   %d1,%a0
        add.l   %d1,%a0
        mvs.w   4668(%a3),%d1
        add.l   #-16384,%d1
        add.l   %d1,%d1
        mac.l   %d1,%d0,%acc0
        movclr.l %acc0,%d0
        move.w  %d0,(%a0)
        jmp     0x4009eb20

| 0x40006ea8, a parameter id's sound slot: `move.l 4(%sp),%d1; cmpi.l #469,%d1`.
| A pseudo id's slot is its code (lock.c).
core_id_slot:
        move.l  4(%sp),%d1
        cmp.l   #PSEUDO,%d1
        bcs.s   1f
        move.l  %d1,-(%sp)
        jsr     core_pseudo_slot
        addq.l  #4,%sp
        rts
1:      jmp     0x40006eb2

| 0x400aa31e, the destination list builder's end (the parameter set in %a4,
| the result in %a2, the list's kind at 32(%sp)): `move.l %a2,%d0; movem.l
| (%sp),%a2-%a6`. A sound's set (its vtable +76 is core's) gets the mod
| parameters after stock's: in the LFO's list (kind 0x600, with 0x400 set)
| the LFO destinations, and from 0.9 in the velocity and aftertouch lists
| (0x200) the "mod" ones, whose routes core's apply sites take out of the
| audio side's parameter table (routes.c), where a code above 41 would be
| another track's parameter.
core_dst_list_end:
        move.l  32(%sp),%d0
        and.l   #0x600,%d0
        beq.s   1f
        move.l  (%a4),%a0
        move.l  76(%a0),%d1
        cmp.l   #core_dst_slot_id,%d1
        bne.s   1f
        move.l  %d0,-(%sp)
        move.l  %a2,-(%sp)
        jsr     core_dst_append
        addq.l  #8,%sp
1:      move.l  %a2,%d0
        movem.l (%sp),%a2-%a6
        lea     24(%sp),%sp
        rts

| 0x4004a7e8, the destination menu's item label (the id in %d3, the owner's
| short name in %d0, the string in %d2): `cmpi.l #469,%d3`. A pseudo id's
| label is its category (MOD when it has none), a colon and its name,
| through stock's own format.
core_dst_label:
        cmp.l   #PSEUDO,%d3
        bcs.s   1f
        move.l  %d0,-(%sp)
        move.l  %d3,-(%sp)
        jsr     core_pseudo_name
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   2f
        addq.l  #4,%sp
        move.l  %d0,-(%sp)
        move.l  %d3,-(%sp)
        jsr     core_pseudo_category
        addq.l  #4,%sp
        move.l  %d0,-(%sp)
        jmp     0x4004a806
2:      move.l  (%sp)+,%d0
1:      cmp.l   #PSEUDO,%d3
        jmp     0x4004a7ee

| 0x400ffab2, the DST value's text (the id in %d0): `lea 0x401bfbc8,%a0`.
| A pseudo id shows its short name.
core_dst_text:
        cmp.l   #PSEUDO,%d0
        bcs.s   1f
        move.l  %d0,-(%sp)
        jsr     core_pseudo_short
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0,%a2
        jmp     0x400fface
1:      lea     0x401bfbc8,%a0
        jmp     0x400ffab8

| 0x400aa32a, one parameter's randomize (set, id), vtable +28 of every
| parameter set and what RANDOMIZE PAGE runs for each knob: `link.w
| %fp,#-52; movem.l %d2-%d5,(%sp)`. A mod parameter's knob is core's
| (core_lock_rand, 1); anything else stock's.
core_rand_param:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_lock_rand
        addq.l  #8,%sp
        tst.l   %d0
        beq.s   1f
        moveq   #1,%d0
        rts
1:      link.w  %fp,#-52
        movem.l %d2-%d5,(%sp)
        jmp     0x400aa332

| 0x400af32c, RELOAD PAGE's copy of the page's parameters (view, track, ids,
| ...): `lea -60(%sp),%sp; movem.l %d2-%d7/%a2-%a6,(%sp)`. core_reload (lock.c)
| calls stock through this, then takes the mod parameters' values back.
core_reload_body:
        lea     -60(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x400af334
