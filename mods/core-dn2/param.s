| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn2 0.5 (Digitone II): ev_param_step, the step a knob turn adds, as
| core's (4.4) on the Digitakt mk1 and core-dn1's (5.4); the walk and
| core_param_shown are ../core/step.s's. OS 1.12.
|
| The page views' encoder handler (0x40067470) has the turn's knob in d3 and
| its parameter row code in d2, and gets the step from 0x4011364e (8.8, the
| row's range and acceleration in). At 0x400675c4 it pops that call's
| arguments and keeps the step in d4, `lea 20(sp),sp; move.l d0,d4`, then
| branches on it (0: no edit). Every edit after that adds d4: the sound's
| (view +88) and the held trigs' locks (+92, +96, +100). Core takes those
| six bytes: the same, then the handlers of ev_param_step, the stock then
| editing, clamping and showing its pop-up as for any turn. With no
| handler, d4 and the flags are the stock's.

        .text                           | after the C code, so no symbol before it moves
        .globl  core_param_step
core_param_step:
        move.l  (%sp), 20(%sp)          | lea 20(sp),sp, under our return
        lea     20(%sp), %sp
        move.l  %d0, %d4                | the step: the move the jsr replaced
        beq.s   9f                      | none: the stock makes no edit
        tst.l   ev_param_step
        beq.s   9f
        move.l  %d3, -(%sp)             | the knob
        move.l  %d4, -(%sp)             | the step
        move.l  %d2, -(%sp)             | the row code
        move.l  %a2, -(%sp)             | the view
        jsr     core_param_step_run     | ../core/step.s
        lea     16(%sp), %sp
        move.l  %d0, %d4
9:      tst.l   %d4                     | the flags the move left; rts keeps them
        rts
