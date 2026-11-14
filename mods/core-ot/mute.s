| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the track mutes (core-ot 0.9). ColdFire V4 (MCF5445x);
| assemble with -mcpu=54455.
|
| The live mute words are 0x80000008 (the audio tracks) and 0x8000000c (the MIDI tracks). Every
| routine that changes one stores it there and then, with the next instruction, copies it into the
| Part (0x100b14d4, 0x100b14d8), which is what a SAVE keeps. There are 30 such pairs in the image
| (the census is ours: every `move.l dN,0x100b14d4` / `0x100b14d8` but the load's range check at
| 0x4000fe6c / 0x4000fe82; octabam's PATTERN MUTE takes 12 of them, MIT). Each copy is a site:
|
|   unsigned ev_mute(unsigned kind, unsigned word)
|     kind 0 = OT_MUTE_AUDIO, 1 = OT_MUTE_MIDI; word = the live word just stored.
|     Returns the word to copy into the Part (chained: the next handler gets it). The live word is
|     not changed: what the unit plays is stock's.
|
| The copy is made after the handlers, and the condition codes are set from the live word as the
| copy's move would have. The changes come from the panel (the sys task), MIDI (0x4000e79c, the
| MIDI task) and the engine's project jobs (0x400866c4): handlers run in that task.

        .section .run, "ax"

| A site: the copy `move.l \reg,\dst` at \addr (6 bytes) becomes `jmp core_mute_<addr>`.
        .macro  MUTE addr, reg, kind, dst
        .globl  core_mute_\addr
core_mute_\addr:
        tst.l   ev_mute
        beq.s   9f
        lea     -24(%sp), %sp
        movem.l %d0-%d2/%a0-%a2, (%sp)
        move.l  %\reg, %d2              | the word so far
        lea     ev_mute, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  %d2, -(%sp)
        pea     \kind
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        move.l  %d0, %d2
        bra.s   1b
2:      move.l  %d2, \dst               | the Part's copy: what the handlers returned
        movem.l (%sp), %d0-%d2/%a0-%a2
        lea     24(%sp), %sp
        tst.l   %\reg                   | the flags the stock move left
        jmp     \addr + 6
9:      move.l  %\reg, \dst             | no handler: the instruction this replaced
        jmp     \addr + 6
        .endm

| The audio tracks' word (0x100b14d4).
        MUTE    0x4000ed60, d0, 0, 0x100b14d4
        MUTE    0x400238f0, d0, 0, 0x100b14d4
        MUTE    0x4003d314, d0, 0, 0x100b14d4
        MUTE    0x4003d3d8, d1, 0, 0x100b14d4
        MUTE    0x4003d400, d0, 0, 0x100b14d4
        MUTE    0x4004034c, d1, 0, 0x100b14d4
        MUTE    0x400403b2, d0, 0, 0x100b14d4
        MUTE    0x40043e5a, d1, 0, 0x100b14d4
        MUTE    0x40043e82, d0, 0, 0x100b14d4
        MUTE    0x40045440, d0, 0, 0x100b14d4
        MUTE    0x4005e432, d0, 0, 0x100b14d4
        MUTE    0x4005e4b0, d0, 0, 0x100b14d4
        MUTE    0x4006298c, d0, 0, 0x100b14d4
        MUTE    0x40065534, d0, 0, 0x100b14d4
        MUTE    0x40065574, d0, 0, 0x100b14d4
        MUTE    0x4007d1ec, d1, 0, 0x100b14d4
        MUTE    0x4007d61c, d0, 0, 0x100b14d4
        MUTE    0x4007d778, d1, 0, 0x100b14d4
        MUTE    0x4007d81a, d2, 0, 0x100b14d4
        MUTE    0x40088028, d0, 0, 0x100b14d4
        MUTE    0x40088084, d0, 0, 0x100b14d4
        MUTE    0x400880de, d0, 0, 0x100b14d4
| The MIDI tracks' word (0x100b14d8).
        MUTE    0x4000f256, d0, 1, 0x100b14d8
        MUTE    0x4003d340, d0, 1, 0x100b14d8
        MUTE    0x4004545c, d0, 1, 0x100b14d8
        MUTE    0x4007d208, d0, 1, 0x100b14d8
        MUTE    0x4007d670, d0, 1, 0x100b14d8
        MUTE    0x4007d6ec, d2, 1, 0x100b14d8
        MUTE    0x4008813a, d0, 1, 0x100b14d8
        MUTE    0x40088196, d0, 1, 0x100b14d8
