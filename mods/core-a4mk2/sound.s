| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.8: ev_voice_params' and ev_sound_setup's sites, OS 1.56
| (sound.c has core_sound_setup_toggle; analog-four-mk2/ui.h the events).
        .text
        .align  2

| ---- ev_voice_params: 0x40103af6 in the audio interrupt (0x401013be), its
| `jsr 0x400fa2b0` (6 bytes; resume 0x40103afc): after the voice parameter
| words (0x80005af0 + 8p + 2v) had their modulation added and were clamped
| (0x401032d2..0x40103432), before 0x400fa2b0 (VCA and pan) and 0x400ff74e
| (the filter words of the analog frame) read them. Handlers: f(words,
| voice_track), every block; the registers the call sees are stock's.
        .globl  core_voice_params_site
core_voice_params_site:
        tst.l   ev_voice_params
        beq.s   9f
        lea     -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a2,(%sp)
        lea     ev_voice_params,%a2
1:      move.l  (%a2)+,%d0
        beq.s   2f
        pea     0x80005ff0                | the voices' tracks
        pea     0x80005af0                | the words
        movea.l %d0,%a0
        jsr     (%a0)
        addq.l  #8,%sp
        bra.s   1b
2:      movem.l (%sp),%d0-%d1/%a0-%a2
        lea     20(%sp),%sp
9:      jsr     0x400fa2b0
        jmp     0x40103afc

| ---- ev_sound_setup: 0x400e62c8 in SOUND SETUP's builder (0x400e5f54),
| after F1 RESONANCE BOOST's row, `movea.l (%a3),%a0; move.l 8(%a0),%d7`
| (6 bytes, the next row's add method; resume 0x400e62ce); %a3 the menu.
| Handlers: f(menu), in the UI task, whose rows follow BOOST's in their order.
| %sp comes back unchanged (the builder pops the four functors' arguments
| later) and %d7 holds the add method, read at 0x400e6356.
        .globl  core_sound_setup_site
core_sound_setup_site:
        tst.l   ev_sound_setup
        beq.s   9f
        lea     -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a2,(%sp)
        lea     ev_sound_setup,%a2
1:      move.l  (%a2)+,%d0
        beq.s   2f
        move.l  %a3,-(%sp)                | the menu
        movea.l %d0,%a0
        jsr     (%a0)
        addq.l  #4,%sp
        bra.s   1b
2:      movem.l (%sp),%d0-%d1/%a0-%a2
        lea     20(%sp),%sp
9:      movea.l (%a3),%a0
        move.l  8(%a0),%d7
        jmp     0x400e62ce
