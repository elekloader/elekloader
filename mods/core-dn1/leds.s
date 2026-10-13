| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1: the key-LED frame (Digitone mk1). ColdFire V4; assemble with
| -mcpu=54455. Beside core.s; the Digitakt's core does not have it.
|
|   VIEW_LEDS  the frame's call to the visible views' LED update
|
| ---- ev_key_leds: the key LEDs, before the stock views set theirs --------
| At 0x40018332 (the same in 1.44), the UI task's LED frame calls VIEW_LEDS
| (0x400e5d22; 1.44 0x400e5f96) right after it clears every LED: each
| visible view sets its own, and the first setter of an LED wins. Core
| makes that call itself after the handlers, so a handler's LED stays over
| the stock view's: a mod shows its state on the trig keys, the track keys
| or the page keys while the stock page under it keeps the rest.
|
| Handlers: f(void *mgr), mgr = the LedManager (the call's second
| argument); the UI task, every frame. On entry 4(sp) is the views'
| argument and 8(sp) the LedManager; core tail-jumps to VIEW_LEDS with the
| stack as it came, so the call returns to the frame as the stock one does.
| d0-d1/a0-a1 are free here, as across the call this replaces. With no
| handler: a test and the jump.
        .section .run, "ax"
        .globl  core_key_leds
core_key_leds:
        tst.l   ev_key_leds
        beq.s   9f
        move.l  %a2, -(%sp)
        lea     ev_key_leds, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  12(%sp), -(%sp)         | the LedManager
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      move.l  (%sp)+, %a2
9:      jmp     VIEW_LEDS
