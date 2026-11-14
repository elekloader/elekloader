| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: a pattern's check and an effect's choice
| (core-ot 0.36). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| void ev_pattern_check(void *pattern): stock's pattern validator (0x4009a670) is
| about to check a pattern: at boot's restore (all 16, before any project load),
| at a bank load. A mod whose pattern bytes stock would refuse puts them right
| first. int ev_fx_choose(unsigned slot, unsigned trk, unsigned id): YES in the
| FX1 (slot 0) or FX2 (slot 1) chooser is about to put effect id on the current
| track; nonzero = refused (the first taker), the chooser stays open. Modwerk's
| DSP loader refuses there; the code is our own.
|
|   0x4009a670  the validator's entry (lea -44(sp) ; movem.l)       ev_pattern_check
|   0x400526e4  FX1's select (lea -68(sp) ; movem.l)                ev_fx_choose
|   0x40052474  FX2's select                                        ev_fx_choose
        .section .run, "ax"

        .globl  core_pattern_check, core_fx1_choose, core_fx2_choose
core_pattern_check:
        tst.l   ev_pattern_check
        beq.s   9f
        lea     ev_pattern_check, %a1
1:      move.l  (%a1)+, %d0
        beq.s   9f
        move.l  %a1, -(%sp)
        move.l  8(%sp), -(%sp)          | the pattern: 4(sp), past a1
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        movea.l (%sp)+, %a1
        bra.s   1b
9:      lea     -44(%sp), %sp           | what the site replaced
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     0x4009a678

| The choosers: guard.c reads the row, the effect and the track and asks the handlers.
        .macro  CHOOSE slot, next
        tst.l   ev_fx_choose
        beq.s   9f
        pea     \slot
        jsr     core_fx_choose
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   9f
        rts                             | refused: the chooser stays as it is
9:      lea     -68(%sp), %sp           | what the site replaced
        movem.l %d2-%d7/%a2-%a6, (%sp)
        jmp     \next
        .endm

core_fx1_choose:
        CHOOSE  0, 0x400526ec
core_fx2_choose:
        CHOOSE  1, 0x4005247c
