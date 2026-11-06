| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 5.0.1 (Digitone mk1): a pool sound put on a track as the SOUND
| BROWSER does, and the tracks' mutes as FUNC + a track key sets them.
| ColdFire V4; assemble with -mcpu=54455. Beside core.s.
|
|   POOL_OFF     the sound pool's place in the project; KIT_OFF the kit's
|   POOL_GET     the pool's getter: a0 -> {sound, holder}, (pool, index)
|   SOUND_LOAD   the kit's sound load (kit, sound, track); core's site is at its entry
|   SP_RELEASE   a shared pointer's release (&holder)
|   MUTES_OFF    the project's track state (MUTE_SET's object): its +0x28 method
|                gives the active pattern's, with the global mutes at +28
|   MUTE_SET     set(state, track, muted, 1); core's ev_mute site is at its entry
|
| Addresses are 1.43's; the 1.44 port in mod.json gives its own.

        .section .run, "ax"

| ---- core_pool_load ---------------------------------------------------------------
| int core_pool_load(int track, int index): the project's pool sound `index`
| (0-127) put on `track` (0-3) as the SOUND BROWSER's FUNC+YES does: the pool's
| own getter copies the sound, and the kit's load (the routine ev_sound_load
| hooks, so its handlers see this load too) puts it on the track. The UI task.
| -> 0 loaded, 1 the track's slot took nothing, 2 no such track, -1 no such
| pool sound.
        .globl  core_pool_load
core_pool_load:
        lea     -24(%sp), %sp
        movem.l %d2-%d3/%a2, (%sp)      | 12(sp): the getter's pair {sound, holder}
        clr.l   12(%sp)
        clr.l   16(%sp)
        lea     12(%sp), %a2
        move.l  32(%sp), -(%sp)         | index
        movea.l FW_PROJECT, %a0
        adda.l  #POOL_OFF, %a0
        move.l  %a0, -(%sp)             | the pool
        movea.l %a2, %a0                | -> the pair
        jsr     POOL_GET
        addq.l  #8, %sp
        moveq   #-1, %d3
        move.l  (%a2), %d2              | the sound's copy, or 0
        beq.s   1f
        move.l  28(%sp), -(%sp)         | track
        move.l  %d2, -(%sp)
        movea.l FW_PROJECT, %a0
        adda.l  #KIT_OFF, %a0
        move.l  %a0, -(%sp)             | the kit
        jsr     SOUND_LOAD
        lea     12(%sp), %sp
        move.l  %d0, %d3
1:      pea     4(%a2)                  | the copy's holder, released
        jsr     SP_RELEASE
        addq.l  #4, %sp
        move.l  %d3, %d0
        movem.l (%sp), %d2-%d3/%a2
        lea     24(%sp), %sp
        rts

| ---- core_mutes, core_mute_set ----------------------------------------------------
| uint32_t core_mutes(void): the global mutes (FUNC + a track key), bit t for
| track t (0-3 the synth tracks, 4-7 the MIDI tracks); 0 with no pattern.
        .globl  core_mutes, core_mute_set
core_mutes:
        movea.l FW_PROJECT, %a0
        adda.l  #MUTES_OFF, %a0
        movea.l (%a0), %a1
        move.l  %a0, -(%sp)
        movea.l 40(%a1), %a1
        jsr     (%a1)
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1f
        movea.l %d0, %a0
        move.l  28(%a0), %d0
1:      rts

| void core_mute_set(int track, int muted): a track's global mute set or
| cleared as FUNC + its track key does (ev_mute's handlers see it, where 0).
| track 0-7. The UI task.
core_mute_set:
        pea     1.w
        moveq   #0, %d0
        tst.l   12(%sp)                 | muted
        beq.s   1f
        moveq   #1, %d0
1:      move.l  %d0, -(%sp)
        move.l  12(%sp), -(%sp)         | track
        movea.l FW_PROJECT, %a0
        adda.l  #MUTES_OFF, %a0
        move.l  %a0, -(%sp)
        jsr     MUTE_SET
        lea     16(%sp), %sp
        rts
