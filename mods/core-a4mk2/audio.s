| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2: the audio interrupt's effects and noise events and
| the dry mute, OS 1.56. Native A4 events, as ev_analog_frame and
| ev_voice_params: ev_fx_in, ev_fx_out, ev_noise_fade, ev_noise_mix, and the
| table core_dry_mute (analog-four-mk2/ui.h has the types; README, "Audio
| events").
|
| Handlers follow the stock C ABI (d0/d1/a0/a1 scratch, the rest saved) and
| leave the EMAC unit (its accumulators, MACSR and MASK) as they found it.
        .text
        .align  2

| ---- core_dry_mute: 0x400fa6c4 in the track levels (0x400fa2b0), the loop
| over the four voices (d2 = voice) that writes each voice's dry VCA words
| (frame words +0x16/+0x18), at the load of the volume for those words and its
| first multiply-accumulate, both done here (6 bytes; resume 0x400fa6ca). A voice whose bit is set in
| core_dry_mute gets volume 0 for its dry words, what stock computes for VOL
| 0; the send gain and the voice's capture are not touched. No call: the
| accumulators are live here. Only d0 and the flags change, as at stock.
        .globl  core_dry_mute_site
core_dry_mute_site:
        btst    %d2,core_dry_mute
        bne.s   1f
        move.l  (%a3),%d0                 | the volume (stock)
        bra.s   2f
1:      moveq   #0,%d0
2:      mac.l   %d0,%d7,%acc0             | stock
        jmp     0x400fa6ca

| ---- ev_fx_in, ev_fx_out: 0x40103dda, the effects block's only call
| (keep2), its arguments (frames, out) on the stack. The block reads the codec
| frames (32 frames of 8 longs; voice v at slot 2 + v) for its send mix and
| writes its 32 stereo pairs to out (0x80007e68).
| ev_fx_in(frames, out, voice_track) before the call, ev_fx_out(out, frames,
| voice_track) after it. The block is called with the caller's registers and
| arguments, unchanged.
        .globl  core_fx_site
core_fx_site:
        tst.l   ev_fx_in
        beq.s   3f
        lea     -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a2,(%sp)
        lea     ev_fx_in,%a2
1:      move.l  (%a2)+,%d0
        beq.s   2f
        pea     0x80005ff0                | the voices' tracks
        move.l  32(%sp),-(%sp)            | out (lea -20, pea: ret 24, frames 28, out 32)
        move.l  32(%sp),-(%sp)            | frames
        movea.l %d0,%a0
        jsr     (%a0)
        lea     12(%sp),%sp
        bra.s   1b
2:      movem.l (%sp),%d0-%d1/%a0-%a2
        lea     20(%sp),%sp
3:      move.l  8(%sp),-(%sp)             | out
        move.l  8(%sp),-(%sp)             | frames
        jsr     0x400fe73c
        addq.l  #8,%sp
        tst.l   ev_fx_out
        beq.s   6f
        lea     -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a2,(%sp)
        lea     ev_fx_out,%a2
4:      move.l  (%a2)+,%d0
        beq.s   5f
        pea     0x80005ff0                | the voices' tracks
        move.l  28(%sp),-(%sp)            | frames (lea -20, pea: ret 24, frames 28, out 32)
        move.l  36(%sp),-(%sp)            | out
        movea.l %d0,%a0
        jsr     (%a0)
        lea     12(%sp),%sp
        bra.s   4b
5:      movem.l (%sp),%d0-%d1/%a0-%a2
        lea     20(%sp),%sp
6:      rts

| ---- ev_noise_fade: 0x400ffe04 in the noise generator, after the four fade
| envelopes (0x80002be8, a long per voice) are updated and before the levels
| are taken from them (the envelopes' address load is done here; resume 0x400ffe0a).
| Handlers: f(env, voice_track). d0-d2/a0 are loaded next by stock; the rest
| is kept (the call is C ABI).
        .globl  core_noise_fade_site
core_noise_fade_site:
        tst.l   ev_noise_fade
        beq.s   9f
        lea     -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a2,(%sp)
        lea     ev_noise_fade,%a2
1:      move.l  (%a2)+,%d0
        beq.s   2f
        pea     0x80005ff0                | the voices' tracks
        pea     0x80002be8                | the fade envelopes
        movea.l %d0,%a0
        jsr     (%a0)
        addq.l  #8,%sp
        bra.s   1b
2:      movem.l (%sp),%d0-%d1/%a0-%a2
        lea     20(%sp),%sp
9:      lea     0x80002be8,%a0
        jmp     0x400ffe0a

| ---- ev_noise_mix: 0x400ffe50, the first instruction of the noise
| generator's final mix (its first address load is done here; resume
| 0x400ffe56), where the mix's three sources per voice are ready (0x80002d78
| + 140v: a 12-byte filter history, then 32 longs each of three sources, at
| +0, +0x230, +0x460 from it). Handlers: f(bufs, voice_track). Nothing is
| live in registers here; the mix loads what it needs.
        .globl  core_noise_mix_site
core_noise_mix_site:
        tst.l   ev_noise_mix
        beq.s   9f
        lea     -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a2,(%sp)
        lea     ev_noise_mix,%a2
1:      move.l  (%a2)+,%d0
        beq.s   2f
        pea     0x80005ff0                | the voices' tracks
        pea     0x80002d78                | the sources
        movea.l %d0,%a0
        jsr     (%a0)
        addq.l  #8,%sp
        bra.s   1b
2:      movem.l (%sp),%d0-%d1/%a0-%a2
        lea     20(%sp),%sp
9:      lea     0x80005ba8,%a1
        jmp     0x400ffe56

| ---- ev_render_out, order 0: the dry mute is set again each block by the
| mods that want it (their ev_render_out handlers, after this one); the next
| block's track levels read it.
        .globl  core_dry_mute_clear
core_dry_mute_clear:
        clr.b   core_dry_mute
        rts

        .data
        .globl  core_dry_mute
core_dry_mute:  .byte   0                 | bit v: voice v's dry VCA words get volume 0
        .align  2
