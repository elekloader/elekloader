| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the tempo a source commits, and a scene's load
| (core-ot 0.41). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| 0x4009c550 commits the tempo from its source, with interrupts masked (sr 0x2700):
| the pattern's (PATTERN TEMPO on, not in an arrangement) or the project's, into
| 0x80001814 and 0x80001818, then tells the UI. Its callers: project load, Part
| reload, the PATTERN TEMPO toggle, entering and leaving ARRANGEMENT, the frame's
| transport path. ev_tempo sees each commit: uint32_t f(uint32_t tempo, unsigned
| source) with tempo in bpm*24 and source 1 pattern, 0 project; nonzero = the tempo
| to commit instead (clamped 720..7200), the next handler sees it. The tempo knob,
| a pattern switch and external clock write 0x80001814 themselves: not a commit.
|
| 0x40002df4(bank, part, scene, slot) loads a scene of a Part into the live scene
| slot A (0) or B (1): SCENE A/B + TRIG, MIDI CC 55/56, and the re-apply after a
| scene paste or clear. ev_scene_load(slot, scene, bank, part) runs after it, before
| its registers come back (watch only).
|
|   0x4009c592  move.l d0,0x80001814                       ev_tempo
|   0x40002f3a  movem.l (sp),d2-d7/a2-a6 ; lea 52(sp),sp   ev_scene_load
        .section .run, "ax"

| d0: the tempo, d2: the saved sr (kept), d1/a0/a1 free.
        .globl  core_tempo
core_tempo:
        tst.l   ev_tempo
        beq.s   1f
        move.l  %d0, -(%sp)
        jsr     core_tempo_c            | tempo.c: d0 = the tempo to commit
        addq.l  #4, %sp
1:      move.l  %d0, 0x80001814
        jmp     0x4009c598

| The routine's frame: its 11 registers at (sp), its arguments from 56(sp). Every
| register is restored by the movem below, so a2 serves the loop.
        .globl  core_scene_load
core_scene_load:
        tst.l   ev_scene_load
        beq.s   9f
        move.l  %d0, -(%sp)
        lea     ev_scene_load, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  4+60(%sp), -(%sp)       | part
        move.l  8+56(%sp), -(%sp)       | bank
        move.l  12+64(%sp), -(%sp)      | scene
        moveq   #1, %d0
        and.l   16+68(%sp), %d0
        move.l  %d0, -(%sp)             | slot: 0 A, 1 B
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      move.l  (%sp)+, %d0
9:      movem.l (%sp), %d2-%d7/%a2-%a6
        lea     52(%sp), %sp
        jmp     0x40002f42
