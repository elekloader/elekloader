| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.18: knob flags (flags.c). Stock's readers of the parameter
| record's +0x24 flags for a knob's own id, OS 1.74:
|
| 0x40007234, flags(id): `move.l 4(%sp),%d1; cmpi.l #469,%d1`, from its
| first instruction. Stock's body touches %d0, %d1 and %a0; this keeps %a1.
| Its callers that test 0x10000 for the knob's id are the destination
| knobs' value checks (0x400aa4ac, 0x400aa648); 0x400aa280 asks it for the
| list's entries, stock ids.
|
| The knob set's four inline reads, each `move.l 0x24(An,%d0.l),Dn` and
| what follows, each rejoining stock after its bytes:
|   0x40039fbe turn, first touch: `move.l 0x24(%a3,%d0.l),%d0; andi.l
|              #0x10000,%d0`, id in %d2, beq at 0x40039fc8; %a0 holds a
|              routine stock calls next
|   0x4003a224 turn, after its delta: the same, id in %d2, beq at 0x4003a22e
|   0x4003a890 push: the same on %a0, id in %d4, beq.w at 0x4003a89a
|   0x40039d94 the list's creation: `move.l 0x24(%a0,%d1.l),%d7; move.l
|              %d0,-(%sp); jsr 0x40006ea8`, id in %d0, the id staying pushed
|              as stock leaves it, rejoining at 0x40039da0 (andi.l #0x10000,%d7)
| The andi sets the condition codes the beq reads, and jmp keeps them.

        .text
        .globl  core_flags_hook, core_flags_turn_site, core_flags_delta_site
        .globl  core_flags_push_site, core_flags_list_site, core_knob_list_id

| the id's entry flags into %d0 (id at 4(%sp)); every other register kept
        .align  2
flags_of:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  16(%sp),-(%sp)            | id
        jsr     core_knob_flags_of
        addq.l  #4,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        rts

        .align  2
core_flags_hook:
        move.l  %a1,-(%sp)
        move.l  8(%sp),-(%sp)             | id
        jsr     core_param_flags
        addq.l  #4,%sp
        move.l  (%sp)+,%a1
        rts

        .align  2
core_flags_turn_site:
        move.l  0x24(%a3,%d0.l),-(%sp)    | stock's flags
        move.l  %d2,-(%sp)                | id
        bsr.w   flags_of
        addq.l  #4,%sp
        or.l    (%sp)+,%d0
        andi.l  #0x10000,%d0
        jmp     0x40039fc8

        .align  2
core_flags_delta_site:
        move.l  0x24(%a3,%d0.l),-(%sp)
        move.l  %d2,-(%sp)
        bsr.w   flags_of
        addq.l  #4,%sp
        or.l    (%sp)+,%d0
        andi.l  #0x10000,%d0
        jmp     0x4003a22e

        .align  2
core_flags_push_site:
        move.l  0x24(%a0,%d0.l),-(%sp)
        move.l  %d4,-(%sp)
        bsr.w   flags_of
        addq.l  #4,%sp
        or.l    (%sp)+,%d0
        andi.l  #0x10000,%d0
        jmp     0x4003a89a

        .align  2
core_flags_list_site:
        move.l  %d0,core_knob_list_id     | the knob this list is created for
        move.l  0x24(%a0,%d1.l),%d7
        move.l  %d0,-(%sp)                | id: stock's argument, and ours
        bsr.w   flags_of
        or.l    %d0,%d7
        move.l  (%sp),%d0
        jsr     0x40006ea8
        jmp     0x40039da0

        .data
        .align  2
| the id of the knob the picker last created a list for, any knob's: the
| list keeps no id, and a mod's own ids all have container index -1
core_knob_list_id:
        .long   0
