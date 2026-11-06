| SPDX-License-Identifier: GPL-2.0-or-later
| Native LFO destination names through core_param_override. Each site keeps
| the native sound slot, list order and drawing; only its text arguments change.
        .section .run, "ax"

| name_at(offset, field, stock): offset is 60 * parameter id. The first
| name() that answers wins; no answer leaves the native text unchanged.
name_at:
        move.l  %a2, -(%sp)
        lea     core_param_override, %a2
1:      move.l  (%a2)+, %d0
        beq.s   3f
        movea.l %d0, %a0
        move.l  4(%a0), %d0
        beq.s   1b
        movea.l %d0, %a0
        move.l  8(%sp), %d1
        moveq   #60, %d0
        divu.l  %d0, %d1
        move.l  12(%sp), -(%sp)
        move.l  %d1, -(%sp)
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1b
        bra.s   4f
3:      move.l  16(%sp), %d0
4:      movea.l (%sp)+, %a2
        rts

| As on the Digitakt, reserve the pushed arguments under the return
| address, then keep every register while asking the mod for their text.
| OS 1.44 0x400c69a2: list row, [group][long name]; d1 = 60 * id.
        .globl  core_lfo_list
core_lfo_list:
        move.l  (%sp), -(%sp)
        move.l  (%sp), -(%sp)
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        move.l  %d1, %d7
        move.l  0x28(%a3,%d1.l), -(%sp)
        pea     0x28.w
        move.l  %d7, -(%sp)
        bsr.w   name_at
        lea     12(%sp), %sp
        move.l  %d0, 68(%sp)
        move.l  %d6, -(%sp)
        pea     0x2c.w
        move.l  %d7, -(%sp)
        bsr.w   name_at
        lea     12(%sp), %sp
        move.l  %d0, 64(%sp)
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        rts

| OS 1.44 0x400c6a20: a row too wide for the list, [group][short name].
| Like the DEST box it asks for the destination name (0x28), not the
| knob's label (0x30), so a label override alone keeps the stock text.
        .globl  core_lfo_compact
core_lfo_compact:
        move.l  (%sp), -(%sp)
        move.l  (%sp), -(%sp)
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        move.l  0x30(%a3,%d2.l), -(%sp)
        pea     0x28.w
        move.l  %d2, -(%sp)
        bsr.w   name_at
        lea     12(%sp), %sp
        move.l  %d0, 68(%sp)
        move.l  %d6, -(%sp)
        pea     0x2c.w
        move.l  %d2, -(%sp)
        bsr.w   name_at
        lea     12(%sp), %sp
        move.l  %d0, 64(%sp)
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        rts

| OS 1.44 0x40089b4c: the DEST box's group, [string][group].
        .globl  core_lfo_group
core_lfo_group:
        move.l  (%sp), -(%sp)
        move.l  (%sp), -(%sp)
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        move.l  %d3, 64(%sp)
        move.l  0x2c(%a0,%d0.l), -(%sp)
        pea     0x2c.w
        move.l  %d0, -(%sp)
        bsr.w   name_at
        lea     12(%sp), %sp
        move.l  %d0, 68(%sp)
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        rts

| OS 1.44 0x40089bca: the DEST box's name. A destination override also
| names this small box; without one it keeps the stock short name.
        .globl  core_lfo_name
core_lfo_name:
        sub.l   %d0, %d2
        move.l  (%sp), -(%sp)
        lea     -60(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)
        move.l  0x30(%a1,%d2.l), -(%sp)
        pea     0x28.w
        move.l  %d2, -(%sp)
        bsr.w   name_at
        lea     12(%sp), %sp
        move.l  %d0, 64(%sp)
        movem.l (%sp), %d0-%d7/%a0-%a6
        lea     60(%sp), %sp
        rts
