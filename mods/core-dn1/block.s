| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1: each block's sound parameters (Digitone mk1). ColdFire V4;
| assemble with -mcpu=54455. Beside core.s; the Digitakt's core does not
| have it.
|
|   CONTROLLERS  the render's controller stage, this site's call
|
| ---- ev_render_params: the parameter words, before the controllers -------
| At 0x4009db80 (1.44 0x4009dba0) the audio render (vector 191) calls its
| controller stage, CONTROLLERS (0x4009c058; 1.44 0x4009c078), with blk + 80
| at 4(sp): the block's sound parameter words, just written from the
| smoother, which glides each toward its target (fw_voice_params). Nine
| words of header, then voice v's 79 words at 9 + 79 v (8.8, the sound's
| slots, the step's locks applied), the global FX's at 9 + 79 * 8 (chorus,
| delay, reverb, master). The controller stage (mod wheel, breath,
| aftertouch), the LFOs, pitch, pan and gain then ride on them, and the
| second CPU is handed them to render. Core makes that call itself after
| the handlers: what a handler writes is this block's value, under the
| controllers and the LFOs, and the smoother writes the words again for
| the next block, so a change lasts one block and never builds up. The kit
| and the targets are not touched.
|
| Handlers: f(int16_t *words), words = voice 0's slot 0; fw_voice_track[v]
| is voice v's track. Interrupt level, ~1500 blocks a second, inside the
| render; the second CPU renders these words, and ev_render_voices sees
| their samples, in the next two blocks. A handler that uses the EMAC keeps MACSR as it
| found it. d0-d1/a0-a1 are free here, as across the call this replaces;
| core tail-jumps to CONTROLLERS with the stack as it came. With no
| handler: a test and the jump.
        .section .run, "ax"
        .globl  core_render_params
core_render_params:
        tst.l   ev_render_params
        beq.s   9f
        move.l  %a2, -(%sp)
        lea     ev_render_params, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        moveq   #18, %d1                | 9 words of header
        add.l   8(%sp), %d1             | blk + 80
        move.l  %d1, -(%sp)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      move.l  (%sp)+, %a2
9:      jmp     CONTROLLERS
