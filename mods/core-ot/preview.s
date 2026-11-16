| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: audio previews and the recorders' commands
| (core-ot 0.41). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| A preview (the audio editor's FUNC+YES, the sample browser, the slot chooser)
| is asked for by message and started by the sys task: a static slot in
| 0x400940ac, a flex slot or recorder buffer in 0x40096c54; it stops in
| 0x40093e9c / 0x40096ab0. Only these write the preview words 0x400d7c44 (static)
| and 0x400d7c48 (flex), so ev_preview sees every start and stop:
| void f(unsigned trk, unsigned on, unsigned flex, unsigned slot), slot as in the
| word (flex 0x80..0x87 are the recorder buffers). Watch only.
|
| 0x40005178(trk, cmd, quant) is how a REC key or a MIDI note gives a track's
| recorder a command, on any machine (a quantized one is parked for the next
| step). ev_rec_cmd(trk, cmd, quant) sees it first, watch only: refusing there
| would split the 0xf010 command from its play trig. The word: bits 12-14 the
| inputs (REC1 0x1000 ..), low byte 0x10 start / 0x40 update / 0x80 stop; TRACK+REC1
| gave 0x9010 then 0x1010 in ot_emu. Not seen: sequencer REC trigs
| (armed in the frame builder), stops written straight to the mailbox (0x4000429c),
| takes ended by their length (ev_rec_length).
|
|   0x40094324  move.l d6,0x400d7c44            ev_preview(trk, 1, 0, slot)
|   0x40096f14  move.l d6,0x400d7c48            ev_preview(trk, 1, 1, slot)
|   0x40093eb2  lea 0x46c80354,a0               ev_preview(trk, 0, 0, slot)
|   0x40096ac6  lea 0x46c80354,a0               ev_preview(trk, 0, 1, slot)
|   0x40005178  lea -12(sp),sp ; movem.l d2-d4  ev_rec_cmd
        .section .run, "ax"

| PREV on, flex, slot: d4 the track; d0-d1/a0-a1 kept.
        .macro  PREV on, flex, slot
        tst.l   ev_preview
        beq.s   1f
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  \slot, -(%sp)
        pea     \flex
        pea     \on
        move.l  %d4, -(%sp)
        jsr     core_preview_c          | rec.c
        lea     16(%sp), %sp
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
1:
        .endm

        .globl  core_preview_static, core_preview_flex
        .globl  core_preview_static_stop, core_preview_flex_stop
core_preview_static:
        move.l  %d6, 0x400d7c44
        PREV    1, 0, %d6
        jmp     0x4009432a
core_preview_flex:
        move.l  %d6, 0x400d7c48
        PREV    1, 1, %d6
        jmp     0x40096f1a
core_preview_static_stop:
        PREV    0, 0, 0x400d7c44
        lea     0x46c80354, %a0
        jmp     0x40093eb8
core_preview_flex_stop:
        PREV    0, 1, 0x400d7c48
        lea     0x46c80354, %a0
        jmp     0x40096acc

| The entry: d0-d1/a0-a1 free, the arguments at 4(sp).
        .globl  core_rec_cmd
core_rec_cmd:
        tst.l   ev_rec_cmd
        beq.s   1f
        move.l  12(%sp), -(%sp)         | quant
        move.l  12(%sp), -(%sp)         | cmd
        move.l  12(%sp), -(%sp)         | trk
        jsr     core_rec_c
        lea     12(%sp), %sp
1:      lea     -12(%sp), %sp
        movem.l %d2-%d4, (%sp)
        jmp     0x40005180
