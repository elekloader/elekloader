| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 1.1 and core-dn2 0.3: ev_key_leds, the key-LED frame after the LEDs
| are cleared and before the stock views set theirs: the same event and
| prototype as core-dn1 3.3's. ColdFire V4; assemble with -mcpu=54455.
|
| The LED frame (DT2 0x4003117c, DN2 0x4002c854) runs when the view
| controller's byte +0x21 is set: it clears every LED (jsr (pc) LEDS_CLEAR,
| the LedManager pushed before it), lets the views set theirs (move.l d2,-(sp)
| then the call), and turns off what nobody set. The site is the clear's jsr
| and that push (DT2 0x400311a2, DN2 0x4002c87a, 6 bytes). The setter,
| fw_led_set(mgr, logical, layer, value), refuses an LED whose layer is set,
| so what a handler sets stays over the views'. Logical ids: the page keys
| TRIG .. MOD are key code + 19, the trig keys key code + 1.
|
| Handlers: void f(void *mgr), on the UI task. A mod that changes its LEDs
| asks for a frame by setting the view controller's +0x21 (Brain + 0x40).
        .text
        .globl  core_key_leds
core_key_leds:
        move.l  4(%sp), -(%sp)          | mgr
        jsr     LEDS_CLEAR
        addq.l  #4, %sp
        move.l  %a2, -(%sp)
        lea     ev_key_leds, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  8(%sp), -(%sp)          | mgr (after the saved a2 and the return)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movea.l (%sp)+, %a2
        movea.l (%sp)+, %a0             | the return
        move.l  %d2, -(%sp)             | the push the site displaced
        jmp     (%a0)
