| SPDX-License-Identifier: GPL-2.0-or-later
| smpl-machines: the render's call of the sample layer's trig intake
| (0x40121134, `jsr 0x4011c1e0`, OS 1.74). The render ORs the voices whose
| sample layer starts this block into %d3 and those that stop into %d5
| (0x401210ee, 0x401210f8), pushes both and calls 0x4011c1e0, which takes
| voice 0 and hands the rest to 0x4011c6b8 through 0x8000586c; the render
| pushes %d5 and %d3 again for 0x4011c6b8 (0x4012127e), which stops voices
| 1..7 by %d5. Nothing else reads %d3 or %d5 between the two calls, and
| 0x4011c1e0 keeps them (C). So smpl_gate changes both the registers and
| the pushed copies: a voice whose track runs a SMPL machine starts nothing
| and stops what it played, in both routines.
        .text
        .globl  smpl_gate_s
        .align  2
smpl_gate_s:
        move.l  %d5,-(%sp)              | masks[1]: stops
        move.l  %d3,-(%sp)              | masks[0]: starts
        pea     (%sp)
        jsr     smpl_gate
        addq.l  #4,%sp
        move.l  (%sp)+,%d3
        move.l  (%sp)+,%d5
        move.l  %d3,4(%sp)              | the pushed copies, past the return address
        move.l  %d5,8(%sp)
        jmp     SAMPLE_INTAKE

| The audio side's routine that puts a sound on a track, 0x4011f60a
| (sound, track): smpl_apply notes it, then stock's first two instructions
| (lea -16(%sp),%sp; movem.l %d2-%d4/%a2,(%sp)) and on.
        .globl  smpl_apply_s
        .align  2
smpl_apply_s:
        move.l  8(%sp),-(%sp)           | track
        move.l  8(%sp),-(%sp)           | sound
        jsr     smpl_apply
        addq.l  #8,%sp
        lea     -16(%sp),%sp
        movem.l %d2-%d4/%a2,(%sp)
        jmp     APPLY_BODY

| The sound setup menu's build, 0x400ef184: stock's jsr (%a5) adds the
| MACHINE row (view, row on the stack), then lea 0x20(%sp),%sp. Add it, then
| the SMPL MACHINE row smpl_menu_row makes, if any.
        .globl  smpl_menu_s
        .align  2
smpl_menu_s:
        move.l  8(%sp),-(%sp)           | row
        move.l  8(%sp),-(%sp)           | view
        jsr     (%a5)                   | 0x400727f2, add a row
        addq.l  #8,%sp
        move.l  4(%sp),-(%sp)           | view
        jsr     smpl_menu_row
        addq.l  #4,%sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0,-(%sp)              | our row
        move.l  8(%sp),-(%sp)           | view
        jsr     (%a5)
        addq.l  #8,%sp
1:      movea.l (%sp)+,%a0
        lea     0x20(%sp),%sp
        jmp     (%a0)

| The row's label, a functor invoker returning a std::string (into %a0), as
| stock's MACHINE label 0x400ee184 does with "MACHINE".
        .globl  smpl_row_label_s
        .align  2
smpl_row_label_s:
        link.w  %a6,#-4
        move.l  %d2,-(%sp)
        pea     -1(%a6)                 | the allocator
        pea     smpl_row_text
        move.l  %a0,%d2
        move.l  %a0,-(%sp)
        jsr     STRING_INIT
        lea     12(%sp),%sp
        move.l  %d2,%d0
        move.l  -8(%a6),%d2
        unlk    %a6
        rts


| the machine list's row label: stock's label functor invoker, (out in %a0,
| functor, row) -> the std::string at out, from smpl_list_label(row)
        .globl  smpl_list_label_s
        .align  2
smpl_list_label_s:
        link.w  %a6,#-4
        move.l  %d2,-(%sp)
        move.l  %a0,%d2                 | out
        move.l  12(%a6),-(%sp)          | the row
        jsr     smpl_list_label
        addq.l  #4,%sp
        pea     -1(%a6)                 | the allocator
        move.l  %d0,-(%sp)
        move.l  %d2,-(%sp)
        jsr     STRING_INIT
        lea     12(%sp),%sp
        move.l  %d2,%d0
        movea.l %d2,%a0
        move.l  -8(%a6),%d2
        unlk    %a6
        rts

| Stock's SRC machine setter, 0x400bd1ba (sound, machine, track, flag):
| smpl_src_before notes the SMPL machine the sound runs, stock's body runs
| as a call (smpl_src_orig: its link and movem, then 0x400bd1c2), and
| smpl_src_after writes the new SMPL machine's defaults if it changed. The
| setter's result stays in %d0.
        .globl  smpl_src_s
        .align  2
smpl_src_s:
        move.l  12(%sp),-(%sp)          | track
        move.l  8(%sp),-(%sp)           | sound
        jsr     smpl_src_before
        addq.l  #8,%sp
        move.l  %d0,-(%sp)              | before
        move.l  20(%sp),-(%sp)          | flag
        move.l  20(%sp),-(%sp)          | track
        move.l  20(%sp),-(%sp)          | machine
        move.l  20(%sp),-(%sp)          | sound
        jsr     smpl_src_orig
        lea     16(%sp),%sp
        move.l  %d0,-(%sp)              | the setter's result
        move.l  4(%sp),-(%sp)           | before
        move.l  24(%sp),-(%sp)          | track
        move.l  20(%sp),-(%sp)          | sound
        jsr     smpl_src_after
        lea     12(%sp),%sp
        move.l  (%sp)+,%d0
        addq.l  #4,%sp                  | before
        rts

smpl_src_orig:
        link.w  %a6,#-0x28
        movem.l %d2-%d6/%a2-%a4,(%sp)
        jmp     SRC_BODY
