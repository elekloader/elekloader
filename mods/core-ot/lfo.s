| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: LFO waves mods add (core-ot 0.28).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455 (no scale-8 index).
|
| Stock has 19 LFO waves: TRI ITRI SAW ISAW SQR ISQR EXP IEXP RMP IRMP RND and
| T1-T8 (the LFO designer). Their handlers sit in a 32-slot table, 0x400d6210;
| slots 19-31 hold SAW's handler as padding, and the WAVE knob of the audio and
| MIDI LFO pages counts 19 (0x400d38ac, 0x400d4218). A mod's wave takes the next
| free slot, in ot_lfo_waves order, after stock's:
|
|   0x400d625c..0x400d628f  slots 19-31 -> core_wave_19..31     (data)
|   0x400d38ac, 0x400d4218  the WAVE counts: 19 + ot_lfo_waves_n  (data)
|   0x4003be4c              WAVE's label formatter: a mod wave's name
|   0x4004693c              WAVE's cell drawer: its icon test; a mod wave is
|                           drawn as text (it has no icon)
|
| With no wave contributed every one of these does what stock did. The idea is
| Modwerk's (docs/OCTATRACK_HARDWARE_TRAPS.md, "Audio-track LFO waveforms"); the
| code is our own.

        .section .run, "ax"

| ---- the dispatch slots -------------------------------------------------------
| The LFO engine (0x40003c88 audio, 0x4000d022 MIDI, in the frame interrupt)
| calls a slot with a0 = the LFO's record and d0 = nonzero on a restart; the
| handler writes the output to 4(a0) and may change d0, d1, a0, a1 only.
| Handlers: void f(struct ot_lfo *lfo, int32_t restart).
        .macro  WAVE k
        .globl  core_wave_\k
core_wave_\k:
        moveq   #\k - 19, %d1
        bra.w   core_wave_go
        .endm
        WAVE 19
        WAVE 20
        WAVE 21
        WAVE 22
        WAVE 23
        WAVE 24
        WAVE 25
        WAVE 26
        WAVE 27
        WAVE 28
        WAVE 29
        WAVE 30
        WAVE 31

core_wave_go:
        cmp.l   #ot_lfo_waves_n, %d1
        bcc.s   9f                      | no wave here: stock's padding (SAW)
        lea     ot_lfo_waves, %a1
        lsl.l   #2, %d1
        movea.l (%a1,%d1.l), %a1        | its struct ot_lfo_wave
        movea.l 4(%a1), %a1             | ->fn
        move.l  %d0, -(%sp)
        move.l  %a0, -(%sp)
        jsr     (%a1)
        addq.l  #8, %sp
        rts
9:      jmp     0x400038bc

| ---- the label ----------------------------------------------------------------
| 0x4003be4c, the WAVE formatter f(dst, value): stock forces a value of 19 or
| more to 0 (TRI) and tail-calls 0x40013a08(dst, label). A mod wave's label is
| its name.
        .globl  core_wave_label
core_wave_label:
        move.l  8(%sp), %d0
        moveq   #19, %d1
        cmp.l   %d1, %d0
        bcs.s   1f                      | stock's 0-18
        sub.l   %d1, %d0
        cmp.l   #ot_lfo_waves_n, %d0
        bcc.s   2f
        lea     ot_lfo_waves, %a0
        lsl.l   #2, %d0
        movea.l (%a0,%d0.l), %a0
        move.l  (%a0), 8(%sp)           | ->name
        jmp     0x40013a08
2:      moveq   #0, %d0                 | out of range: TRI, as stock
1:      lea     0x400be38e, %a0
        movea.l (%a0,%d0.l*4), %a0
        move.l  %a0, 8(%sp)
        jmp     0x40013a08

| ---- the cell -----------------------------------------------------------------
| 0x4004693c in WAVE's cell drawer (0x400468e4): d2 = the value, d4 = the cell's
| flags. Stock draws nothing above 19 and an icon from 0x400bdf36[d2] up to it
| (slot 19 there is not an icon). A mod wave gets flag 2, the text form.
        .globl  core_wave_cell
core_wave_cell:
        moveq   #18, %d0
        cmp.l   %d2, %d0
        bcc.s   1f                      | stock's 0-18: its icon
        move.l  %d2, %d0
        moveq   #19, %d1
        sub.l   %d1, %d0
        cmp.l   #ot_lfo_waves_n, %d0
        bcc.s   2f
        moveq   #2, %d0
        or.l    %d0, %d4                | a mod wave: its name, as text
1:      jmp     0x40046944
2:      jmp     0x40046aa8              | nothing, as stock above its range
