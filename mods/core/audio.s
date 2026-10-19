| SPDX-License-Identifier: GPL-2.0-or-later
| core 4.2 (Digitakt mk1): exclusive audio, and the memory it lends.
| ColdFire V4; assemble with -mcpu=54455. From mod.json:
|
|   OUT_BUF      the output's double buffer (the SSI's, 2 x 32 frames L,R, 24
|                bits in 32): 0x4ba8f080 in 1.53, 0x4ba9f080 in 1.54
|   AFTER_MIX    the render, just past its call to the mixer (0x40078150)
|   FW_VOICES    what the instruction core's site replaced pushed (0x800014f0)
|
| ---- core_audio: one mod owns the audio while it wants to -------------------
| The render (vector 191) handles the sequencer's events, then runs the
| engine: the smoother, the LFOs, playback, the overdrive, the envelopes, the
| filters, and the mixer (the inputs, the tracks, delay, reverb, compressor,
| the output and USB). On the Digitakt all of it runs on the one ColdFire,
| and it takes about 80% of a block on a unit, so an owner gets its time by
| stopping the engine: core takes the instruction that starts it (0x40077e94,
| the same in 1.53 and 1.54), and while there is an owner it skips everything
| up to the mixer's call and goes on just past it. The events, the sequencer's
| clock, USB and the sampler still run, so the MIDI tracks keep playing.
|
| A mod contributes a pointer to its owner record to the table core_audio.
| A build may have several; the first one whose on is set has the audio:
|   +0 on      nonzero: the mod wants the audio
|   +4 render  render(int32_t *out, const int32_t *in): in is the block's
|              input, 32 frames L,R, Q1.31; out is 32 frames L,R, Q1.31, all
|              of which render writes (the same polarity as in)
|   +8 flags   0
| While an owner has the audio, core lends it the memory the stopped engine
| no longer uses: the delay's 4 MB line (CORE_LEND_LO to CORE_LEND_HI, a
| region its mod claims in the profile's 'lend' area). The handover:
| - an owner's on set: core clears the line, a part every block, and pushes
|   the data cache; then core_audio_live names its record, and from that
|   block on its render runs and the line is its own;
| - its on cleared (or another record first in the table): core_audio_live
|   goes to 0 at once, and core clears the line again, pushes the data cache
|   and only then lets the engine run, so the delay hears nothing of it.
| While core clears, the output is silent. With no owner every block is
| stock. render runs at interrupt level, inside the render, whose EMAC
| settings it keeps if it uses the EMAC.
|
| What core writes for an owner, as the mixer would for the inputs alone:
| the output's half (the codec), its polarity the stock's (the mixer's output
| stage inverts what it mixed: core writes -out); the mixer's own buffer
| (what the sampler records as the main mix) and USB's main pair, out; USB's
| tracks silent and its inputs the input. The tracks' blocks are cleared as
| an owner takes the audio, so the sampler and USB hear nothing stale.
        .equ    CLEAR_CHUNK, 32768      | bytes cleared a block: 4 MB in 128
        .equ    LEND_LO, 0x47C00000     | the delay's line, by its cached alias
        .equ    LEND_SIZE, 0x00400000
        .equ    DC_INDEX_END, 8192      | 512 sets x 16 bytes: every line, 4 ways
        .equ    INPUT_RING, 0x80001000  | the codec's input ring, two halves
        .equ    MIX_BUF, 0x8000ea70     | the mixer's buffer, 32 frames L,R
        .equ    USB_BLOCK, 0x80002160   | USB's block, 12 longs a frame
        .equ    TRACK_BLOCKS, 0x80001a18

        .section .run, "ax"

| 0x40077e94, by jsr (was: pea 0x800014f0): the engine as stock, or for an
| owner the block above and on past the mixer's call. The handler's frame is
| a6: -84(a6) the input ring's position, -88(a6) the output's half (0, 1).
        .globl  core_audio_gate
core_audio_gate:
        bsr.w   pick                    | d0 = the first record whose on is set
        tst.l   gate_phase
        bne.s   skip
        tst.l   %d0
        bne.s   1f
        move.l  (%sp), -(%sp)           | stock: the replaced push, under the
        move.l  #FW_VOICES, %d0         | return address
        move.l  %d0, 4(%sp)
        rts
1:      move.l  %d0, gate_cur           | an owner: the line cleared first
        moveq   #1, %d1
        move.l  %d1, gate_phase
        clr.l   gate_clear
        lea     TRACK_BLOCKS, %a0       | the tracks' blocks silent
        move.l  #256, %d1
2:      clr.l   (%a0)+
        subq.l  #1, %d1
        bne.s   2b

skip:   addq.l  #4, %sp                 | on past the mixer: no return
        move.l  %d0, %d6                | d6 the record picked, or 0
        move.l  -84(%fp), %d2           | d2 the input's half (as 0x4007811e)
        bge.s   1f
        addi.l  #255, %d2
1:      andi.l  #256, %d2
        addi.l  #INPUT_RING, %d2
        move.l  -88(%fp), %d0           | a3 the output's half
        lsl.l   #8, %d0
        addi.l  #OUT_BUF, %d0
        movea.l %d0, %a3
        movea.l %d2, %a0                | the input to Q31 in place, as the
        moveq   #64, %d1                | mixer has it (the sampler reads it so)
2:      move.l  (%a0), %d0
        asl.l   #8, %d0
        move.l  %d0, (%a0)+
        subq.l  #1, %d1
        bne.s   2b

        move.l  gate_phase, %d0
        subq.l  #2, %d0
        bne.s   clearing
        cmp.l   gate_cur, %d6           | granted: still the owner?
        beq.s   play
        clr.l   core_audio_live         | let go: the line back, cleared
        clr.l   gate_clear
        moveq   #3, %d0
        move.l  %d0, gate_phase
        bra.s   silent
play:   move.l  %d2, -(%sp)             | render(core_audio_out, the input)
        pea     core_audio_out
        movea.l gate_cur, %a0
        movea.l 4(%a0), %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   write

clearing:                               | phase 1 (to grant) or 3 (to let go)
        move.l  gate_phase, %d0
        subq.l  #1, %d0
        bne.s   1f
        cmp.l   gate_cur, %d6           | granting, and the owner went (or
        beq.s   1f                      | another came first): let go instead
        moveq   #3, %d0
        move.l  %d0, gate_phase
1:      bsr.w   clear_part              | -> d0 nonzero when the line is clear
        tst.l   %d0
        beq.s   silent
        bsr.w   dc_push
        move.l  gate_phase, %d0
        subq.l  #1, %d0
        bne.s   2f
        moveq   #2, %d0                 | granted: from the next block it plays
        move.l  %d0, gate_phase
        move.l  gate_cur, %d0
        move.l  %d0, core_audio_live
        bra.s   silent
2:      clr.l   gate_phase              | let go: stock from the next block
        clr.l   gate_cur
silent: lea     core_audio_out, %a0
        moveq   #64, %d1
1:      clr.l   (%a0)+
        subq.l  #1, %d1
        bne.s   1b

| write: core_audio_out to the output's half (negated, 24 bits), the mixer's
| buffer and USB.
write:  lea     core_audio_out, %a0
        lea     MIX_BUF, %a1
        lea     USB_BLOCK, %a2
        movea.l %d2, %a4                | the input, Q31
        moveq   #32, %d3
1:      move.l  (%a0)+, %d0
        move.l  (%a0)+, %d1
        move.l  %d0, (%a1)+
        move.l  %d1, (%a1)+
        move.l  %d0, (%a2)+             | USB: the main pair, the tracks silent,
        move.l  %d1, (%a2)+             | the inputs
        clr.l   (%a2)+
        clr.l   (%a2)+
        clr.l   (%a2)+
        clr.l   (%a2)+
        clr.l   (%a2)+
        clr.l   (%a2)+
        clr.l   (%a2)+
        clr.l   (%a2)+
        move.l  (%a4)+, (%a2)+
        move.l  (%a4)+, (%a2)+
        neg.l   %d0
        neg.l   %d1
        asr.l   #8, %d0
        asr.l   #8, %d1
        move.l  %d0, (%a3)+
        move.l  %d1, (%a3)+
        subq.l  #1, %d3
        bne.s   1b
        move.l  %d2, -(%sp)             | as the mixer's call left the stack
        move.l  %d2, -(%sp)
        move.l  %d2, -(%sp)
        move.l  #MIX_BUF, %d0           | what the mixer returns
        moveq   #5, %d7                 | as the render set it before the call
        jmp     AFTER_MIX               | (move.l d0,d4; d2 the input's half)

| pick -> d0 = the first record in core_audio whose on is set, or 0.
pick:   lea     core_audio, %a0
1:      move.l  (%a0)+, %d0
        beq.s   2f
        movea.l %d0, %a1
        tst.l   (%a1)                   | on
        beq.s   1b
2:      rts

| clear_part: the next CLEAR_CHUNK bytes of the line -> d0 nonzero when it
| is all clear. Keeps d2, d6, a3.
clear_part:
        lea     -24(%sp), %sp
        movem.l %d2-%d7, (%sp)
        movea.l #LEND_LO, %a0
        adda.l  gate_clear, %a0
        movea.l %a0, %a1                | its end
        adda.l  #CLEAR_CHUNK, %a1
        moveq   #0, %d0
        moveq   #0, %d1
        moveq   #0, %d2
        moveq   #0, %d3
        moveq   #0, %d4
        moveq   #0, %d5
        moveq   #0, %d6
        moveq   #0, %d7
1:      movem.l %d0-%d7, (%a0)
        lea     32(%a0), %a0
        cmpa.l  %a1, %a0
        bcs.s   1b
        move.l  gate_clear, %d0
        addi.l  #CLEAR_CHUNK, %d0
        move.l  %d0, gate_clear
        cmpi.l  #LEND_SIZE, %d0
        scc     %d0                     | the end: 0xff
        andi.l  #1, %d0
        movem.l (%sp), %d2-%d7
        lea     24(%sp), %sp
        rts

| dc_push: every line of the data cache pushed (written back) and
| invalidated, by set and way, so what was written through the cached alias
| is in the DDR before the engine's DMA reads it there.
dc_push:
        moveq   #0, %d0
1:      movea.l %d0, %a0
        cpushl  %dc, (%a0)
        addq.l  #1, %a0
        cpushl  %dc, (%a0)
        addq.l  #1, %a0
        cpushl  %dc, (%a0)
        addq.l  #1, %a0
        cpushl  %dc, (%a0)
        addi.l  #16, %d0
        cmpi.l  #DC_INDEX_END, %d0
        blt.s   1b
        rts

| core_audio_caps: what core_audio does beyond an owner's audio. The
| Digitakt's: CORE_AUDIO_LEND (4), the delay's line lent (core-dn1's 4.0 has
| CORE_AUDIO_INSERT, 2, which the Digitakt's has not).
        .balign 4
        .globl  core_audio_caps, core_audio_live
core_audio_caps:
        .long   4
| core_audio_live: the record that has the audio and the line, or 0.
core_audio_live:
        .long   0
gate_phase:
        .long   0                       | 0 stock, 1 clearing to grant, 2 an
gate_cur:                               | owner plays, 3 clearing to let go
        .long   0
gate_clear:
        .long   0                       | the line's bytes cleared so far
core_audio_out:
        .skip   256
