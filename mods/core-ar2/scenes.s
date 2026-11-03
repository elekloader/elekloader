| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.8: the sites of mod parameters in scenes and performances
| (scenes.c). Each jmp site's displaced instructions are done here, then a
| jump back into stock; each jsr site's, then a return (OS 1.74).
        .text
        .globl  core_scene_turn_site, core_scene_push_site, core_perf_entry, core_scene_entry
        .globl  core_code_id, core_id_code
        .align  2

| 0x400385f8, a knob's turn on a parameter view (view, id, delta): `lea
| -48(%sp),%sp; movem.l %d2-%d7/%a2-%a3,(%sp)`. A mod parameter's knob with
| a scene's or a performance's pad held is core's.
core_scene_turn_site:
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     core_scene_turn
        lea     12(%sp),%sp
        tst.l   %d0
        bmi.s   1f
        rts
1:      lea     -48(%sp),%sp
        movem.l %d2-%d7/%a2-%a3,(%sp)
        jmp     0x40038600

| 0x400387ba, a knob's push (view, id): `lea -48(%sp),%sp; movem.l
| %d2-%d5/%a2-%a4,(%sp)`.
core_scene_push_site:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_scene_push
        addq.l  #8,%sp
        tst.l   %d0
        bmi.s   1f
        rts
1:      lea     -48(%sp),%sp
        movem.l %d2-%d5/%a2-%a4,(%sp)
        jmp     0x400387c2

| 0x4011f744, the performance list's build for the audio side (the entry in
| %a1, its track byte in %d0): `move.b 3(%a1),%d1; moveq #12,%d3`. An entry
| with a code above 41 gets track 0xff, so stock takes it as empty.
core_perf_entry:
        mvz.b   3(%a1),%d3
        moveq   #41,%d1
        cmp.l   %d1,%d3
        bls.s   1f
        st      %d0
1:      move.b  3(%a1),%d1
        moveq   #12,%d3
        rts

| 0x4011f844, the active scene's build (the entry in %a0, the scene in %d1):
| `mvs.b 4(%a0),%d2; move.b 2(%a0),%d0`. The same: %d3 is free here.
core_scene_entry:
        mvs.b   4(%a0),%d2
        move.b  2(%a0),%d0
        mvz.b   3(%a0),%d3
        cmp.l   #41,%d3
        bls.s   1f
        st      %d0
1:      rts

| 0x40007602 and 0x400075c8, a lock's code to the kit file's number and
| back (track, n): `move.l %d2,-(%sp); moveq #12,%d2; move.l 8(%sp),%d1`.
| A drum track's 42..127 (a mod parameter's code; stock's numbers are
| 0..41) stays itself, so a saved kit keeps its mod entries; from 0.9 the FX
| track's (12) too.
core_code_id:
        bsr.s   pass
        tst.l   %d1
        bne.s   1f
        rts
1:      move.l  %d2,-(%sp)
        moveq   #12,%d2
        move.l  8(%sp),%d1
        jmp     0x4000760a

core_id_code:
        bsr.s   pass
        tst.l   %d1
        bne.s   1f
        rts
1:      move.l  %d2,-(%sp)
        moveq   #12,%d2
        move.l  8(%sp),%d1
        jmp     0x400075d0

| (track at 8(%sp), n at 12(%sp), past the return): %d1 = 0 and %d0 = n for
| a track's 42..127 (from 0.9 the FX track's too, 12: its scene and
| performance entries; stock's FX range is 0..37), else %d1 = 1
pass:
        moveq   #1,%d1
        moveq   #13,%d0
        cmp.l   8(%sp),%d0
        bls.s   9f
        move.l  12(%sp),%d0
        cmp.l   #42,%d0
        bcs.s   9f
        cmp.l   #127,%d0
        bhi.s   9f
        moveq   #0,%d1
9:      rts
