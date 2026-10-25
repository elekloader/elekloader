| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the voice events (core 0.7).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| The firmware's voices are driven from the frame interrupt's builder
| (inline in the frame interrupt 0x4000aad0), which turns the sequencer's and
| the panel's mailboxes (0x46c80354[track]) into the DSP command byte
| (0x46104d15[track]), the live lanes, and the voice records the DSP receives;
| and from the voice struct's routines (0x40006820 reset, 0x4000f450 bind). Octabam's synth
| machines, MUTE MODES, TEMPO SYNC, PLOCKS P2 and the recorder fixes each
| detour a handful of these places, and only one of them can: here each place
| is an event with a table, as the rest of the bus.
|
| The sites are NOT the ones octabam's modules hook. SYNTH, in irpina/
| octabam2elemod v1.0, writes 0x4000c634, 0x4000b51a, 0x4000685c, 0x400795ba,
| 0x40079816 and 0x4005a848, and FLEX SEEKBIND 0x4000f834; each event below
| sits on the instruction after, before or beside the one octabam detours, so
| those files link with this core as with 0.6 (tests: V10_NEIGHBOURS) and a
| build that has both sees both. Every stub replays the instructions it
| displaced, keeps every register the way the stock code left it, and with no
| subscriber costs a test of the table and the replay.
|
| Handlers use the C convention (arguments on the stack, d0-d1/a0-a1 free, the
| rest kept, the result in d0). All of them but ev_voice_kill run in the frame
| interrupt (level 5, 362.8 us a frame, the worst frames near 92%): keep them
| to a few dozen instructions. ev_voice_kill runs in tasks as well, with every
| interrupt masked. None may use the EMAC: the interrupt has put the
| interrupted task's EMAC state away before these places, but the builder goes
| on using the EMAC itself after them (ev_frame and ev_frame_end, at the end of
| the interrupt, are the events where a handler may).
|
| The addresses come from mod.json (defsym):
|
|   DSPCMD       the DSP command byte per track (0x46104d15 + track)
|   V_*_NEXT ...  the instruction each stub goes back to

        .section .run, "ax"

| ---- ev_voice_start: a DSP command for a track --------------------------------
| At 0x4000c63c in the frame builder, one instruction after the copy of the
| track's DSP command byte 0x46104d15[t] into d0 (octabam's cxosc and SYNTH
| detour that copy, 0x4000c634); was: lea 0x46104d0c,%a0. a1 = the track, d0
| the byte, which the next instruction stores in the packer's nibble byte and
| whose low nibble goes on to the DSP. Every START form passes here (a trig
| the sequencer posts, a panel key, a MIDI note); octabam's measurements: a
| trig posts 0x10|n, n the sub-frame position the packer splits the frame's
| render calls at, and the panel key 0x30. The stub runs the handlers only for
| a byte that is not 0 (nothing was posted this frame for the track).
| Handlers: unsigned f(unsigned trk, unsigned cmd) -> the command byte the
| next handler, the packer and the DSP get (its low byte): return cmd to leave
| it. The stub stores the result in DSPCMD[t].
        .globl  core_v_start
core_v_start:
        tst.b   %d0
        beq.s   9f
        tst.l   ev_voice_start
        beq.s   9f
        lea     -12(%sp), %sp
        movem.l %d0/%a1-%a2, (%sp)      | 0: the byte, 4: the track, 8: the caller's a2
        lea     ev_voice_start, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        mvz.b   3(%sp), %d0             | the byte so far
        move.l  %d0, -(%sp)
        move.l  8(%sp), -(%sp)          | the track (4 saved past a1, 4 pushed)
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        move.b  %d0, 3(%sp)             | what the handler made of it
        bra.s   1b
2:      movem.l (%sp), %d0/%a1-%a2
        lea     12(%sp), %sp
        lea     DSPCMD, %a0
        move.b  %d0, (%a0,%a1.l)        | the posted byte, as the handlers left it
9:      lea     0x46104d0c, %a0         | the instruction this replaced
        jmp     V_START_NEXT

| ---- ev_voice_release: the AMP release of a track -----------------------------
| At 0x4000b4f6 in the frame builder (was: move.b 0x80001828,%d4 ; move.b
| %d4,118(%sp)): the track's mailbox word 0x46c80354[t] has bit 6 set, which
| every release posts (the last panel key up, a MIDI note-off, whatever the
| sequencer posts). d2 = the track, d0 = the word (bit 2 is a START posted in the
| same frame: the voice restarts, nothing releases). The release bytes
| 0x80001828/9 are read after the handlers, so a handler may change them.
| Handlers: void f(unsigned trk, unsigned word).
        .globl  core_v_release
core_v_release:
        tst.l   ev_voice_release
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)  | 0: the word, 4: d1, 8: a0, 12: a1, 16: the caller's a2
        lea     ev_voice_release, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  (%sp), -(%sp)           | the word
        move.l  %d2, -(%sp)             | the track
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      move.b  0x80001828, %d4         | the instructions this replaced
        move.b  %d4, 118(%sp)
        jmp     V_REL_NEXT

| ---- ev_voice_kill: a voice ended hard -----------------------------------------
| At 0x40006862 in the routine 0x40006820(track), which resets a voice (an
| argument of 8 or more recurses over the eight tracks), one instruction after
| octabam's SYNTH detour (0x4000685c) and past the store that clears the
| voice's byte in the CF voice array 0x800049d8. The sequencer's STOP and
| pattern changes, the sample loaders, the preview, the frame builder's own end
| mask, the renderer when a voice has played to its end (0x40008110) and the
| binder when the track has no sample (0x4000f518) all come here. The
| routine has raised the interrupt level to 7 and keeps it until its return:
| a handler runs with every interrupt masked and must not wait for anything.
| d1 = the track (0-7), a1 live. Handlers: void f(unsigned trk).
        .globl  core_v_kill
core_v_kill:
        tst.l   ev_voice_kill
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_voice_kill, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  4(%sp), -(%sp)          | the track (d1, saved)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      move.l  %d1, %d0                | the instructions this replaced
        lsl.l   #3, %d0
        movea.l %d0, %a0
        jmp     V_KILL_NEXT

| ---- ev_voice_bind: a sample voice is bound to its sample -----------------------
| At 0x4000f4dc in the binder 0x4000f450(track, ...), which the renderer and
| the packer's kind table (FLEX START) call, both in the frame interrupt: it
| has picked the arena entries (a5, a4) the track's sample lives in and is
| about to write them to the voice struct (a2 = 0x800049d8 + 168 t: +4, +8;
| was: move.l %a5,4(%a2) ; move.l %a4,8(%a2)). 64(sp) = the track. Handlers:
| int f(unsigned trk, void *voice): return nonzero to leave the voice's
| pointers as they were (octabam's MUTE MODES does for a silenced track;
| whether the DSP watches these stores is, there, an untested guess). The
| voice is then unbound: the renderer finds nothing left to play and resets
| it (ev_voice_kill), one or two times a frame. The later handlers are not
| called.
        .globl  core_v_bind
core_v_bind:
        tst.l   ev_voice_bind
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_voice_bind, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  %a2, -(%sp)             | the voice
        move.l  88(%sp), -(%sp)         | the track (64 in the binder's frame, 20 saved, 4 pushed)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
        jmp     V_BIND_NEXT             | taken: the stores are not made
2:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
9:      move.l  %a5, 4(%a2)             | the instructions this replaced
        move.l  %a4, 8(%a2)
        jmp     V_BIND_NEXT

| ---- ev_voice_seek: the binder counts a (re)start of the voice ----------------
| At 0x4000f82c in the same routine, eight bytes before octabam's FLEX
| SEEKBIND counter (0x4000f834: addq.l #1,144(a2) ; move.l %a0,152(a2)). The
| voice's start pointers have been written (a0 = the position it starts at);
| the next stock instruction bumps the voice's start counter (+0x90), which the
| frame builder reads as "send this voice as new" and the DSP restarts its
| read pointer on. 55(sp) = the binder's flag for a re-bind of the same sample
| (same slot, type and generation). Handlers: int f(unsigned trk, void *voice,
| unsigned same): return nonzero to leave the counter alone, so the DSP seeks
| the running voice (octabam's FLEX SEEKBIND does so for a same-sample
| re-bind of a recorder's buffer). With a FLEX SEEKBIND of v1.0 in the build
| the stock path still goes through its detour.
        .globl  core_v_seek
core_v_seek:
        movea.l 50(%sp), %a3            | the instructions this replaced
        move.l  %a0, 56(%a3)
        tst.l   ev_voice_seek
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_voice_seek, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        mvz.b   75(%sp), %d1            | 55 in the binder's frame, 20 saved
        move.l  %d1, -(%sp)             | same
        move.l  %a2, -(%sp)             | the voice
        move.l  92(%sp), -(%sp)         | the track (64, 20 saved, 8 pushed)
        movea.l %d0, %a0
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
        move.l  %a0, 152(%a2)           | the store that follows the counter's bump
        jmp     V_SEEK_NEXT
2:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
9:      jmp     V_SEEK_BUMP             | stock's bump (and a detour of it, if the build has one)

| ---- ev_trig_apply: a trig's locks reached the track's lane --------------------
| At 0x4000c59e in the frame builder, where the restore path (the Part's values
| back into the live lane 0x80000810 + 72 t for the slots the last trig locked)
| and the apply path (this trig's lock bytes into the lane) join (was: move.l
| %a4,%d0 ; move.w %d0,%ccr ; bmi 0x4000c614). a4 = the trig's flags: octabam's
| PLOCKS P2 reads bit 31 (no apply), bit 6 (no restore) and bit 2 (keep the
| record). 114(sp) = the track. Handlers: void f(unsigned trk, unsigned flags).
        .globl  core_v_trig
core_v_trig:
        tst.l   ev_trig_apply
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_trig_apply, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  %a4, -(%sp)             | the flags
        move.l  138(%sp), -(%sp)        | the track (114, 20 saved, 4 pushed)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
9:      move.l  %a4, %d0                | the instructions this replaced
        move.w  %d0, %ccr
        bmi.s   8f
        jmp     V_TRIG_LONG
8:      jmp     V_TRIG_SKIP

| ---- ev_dsp_trig: the trig flags of a track's DSP frame word -------------------
| At 0x40004c72 in the voice record writer 0x40004bd4 (called from the frame
| builder once a frame), in its loop over the eight tracks: d4 = the track,
| d1 = the byte at 0x80004858 + 8 t the builder wrote (bits 0 and 1: a trig
| happened; bit 2: a release; was: moveq #3,%d0 ; and.l %d1,%d0 ; beq
| 0x40004cb0). When bits 0 or 1 are set, stock builds the per-trig flag bits of
| the track's word 30 in the DSP block from them (0x80 | 0x10 | 0x40 ...).
| Handlers: int f(unsigned trk, unsigned flags): return nonzero and the word
| is built as it is for a frame with no trig (stock's own exit,
| 0x40004cb0), which is what octabam's MUTE MODES does for a silenced
| track.
        .globl  core_dsp_trig
core_dsp_trig:
        moveq   #3, %d0
        and.l   %d1, %d0
        beq.s   8f
        tst.l   ev_dsp_trig
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_dsp_trig, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  4(%sp), -(%sp)          | the flags (d1, saved)
        move.l  %d4, -(%sp)             | the track
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
8:      jmp     V_FLAGS_NONE            | no trig for this track this frame
2:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
9:      jmp     V_FLAGS_TRIG

| ---- ev_dsp_record: a track's record for the DSP, written -----------------------
| At 0x40004d40 in the same loop, after stock has put the FX ids into the
| record (a2 = the track's 64 bytes = 32 halfwords, which the DSP's block takes
| a word each; was: move.b 3516(%a0),%d2 ; ext.w %d2 ; move.w %d2,56(%a2): the
| FX2 id). The copier, the slew and the scene stage have written every
| halfword before this; the writer after it only adds the tempo (halfword 31),
| so a store a handler makes here lands last in the frame (octabam's TEMPO
| SYNC puts the held MIDI note in the low byte of halfword 13). d4 = the
| track. Handlers: void f(unsigned trk, uint16_t *rec).
        .globl  core_dsp_record
core_dsp_record:
        move.b  3516(%a0), %d2          | the instructions this replaced
        ext.w   %d2
        move.w  %d2, 56(%a2)
        tst.l   ev_dsp_record
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_dsp_record, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  %a2, -(%sp)             | the record
        move.l  %d4, -(%sp)             | the track
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
9:      jmp     V_REC_NEXT

| ---- ev_voice_reset: a voice about to be reset (core-ot 0.8) --------------------
| At 0x40006820, the entry of the reset routine above (was: move.l a2,-(sp) ;
| move.l d2,-(sp) ; move.l 12(sp),d1), before it masks the interrupts: (sp) =
| the return, 4 the track. An argument above 7 recurses over the eight tracks
| through this entry (lea of 0x40006820), so it is passed on unasked and each
| track comes back here. Octabam's MUTE MODES drops the reset of a muted track
| here (MIT). d0-d1/a0-a1 are free at a routine's entry. Handlers: int f(unsigned
| trk), nonzero to keep the voice: the routine returns and ev_voice_kill does not
| fire; the later handlers are not asked.
        .globl  core_v_reset
core_v_reset:
        tst.l   ev_voice_reset
        beq.s   9f
        move.l  4(%sp), %d0
        moveq   #7, %d1
        cmp.l   %d0, %d1
        bcs.s   9f                      | every track: stock's loop calls back for each
        move.l  %a2, -(%sp)
        lea     ev_voice_reset, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  8(%sp), -(%sp)          | the track (4 past a2 and the return)
        movea.l %d1, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1b
        movea.l (%sp)+, %a2
        rts                             | kept: the reset is not made
2:      movea.l (%sp)+, %a2
9:      move.l  %a2, -(%sp)             | the instructions this replaced
        move.l  %d2, -(%sp)
        move.l  12(%sp), %d1
        jmp     V_RESET_NEXT
