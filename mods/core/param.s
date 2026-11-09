| SPDX-License-Identifier: GPL-2.0-or-later
| core 4.4 (Digitakt mk1): ev_param_step, the step a knob turn adds, as
| core-dn1's (5.4) on the Digitone mk1; the walk and core_param_shown are
| step.s's, shared with them. The addresses are the same in OS 1.53 and
| 1.54.
|
| The page views' encoder handler (0x40032a78) has the turn's knob in d3 and
| its parameter in d2, and gets the step from 0x400c06ae (8.8, acceleration
| in). At 0x40032bc4 it pops that call's arguments and keeps the step in d5,
| `lea 20(sp),sp; move.l d0,d5`, then branches on it (0: no edit). Every
| edit after that adds d5: the sound's (view +88), GRID REC's held steps'
| locks (+124) and +92's. Core takes those six bytes: the same, then the
| handlers of ev_param_step (step.s), the stock then editing, clamping and
| showing its pop-up as for any turn. With no handler, d5 and the flags are
| the stock's.

        .section .run, "ax"
        .globl  core_param_step
core_param_step:
        move.l  (%sp), 20(%sp)          | lea 20(sp),sp, under our return
        lea     20(%sp), %sp
        move.l  %d0, %d5                | the step: the move the jsr replaced
        beq.s   9f                      | none: the stock makes no edit
        tst.l   ev_param_step
        beq.s   9f
        move.l  %d3, -(%sp)             | the knob
        move.l  %d5, -(%sp)             | the step
        move.l  %d2, -(%sp)             | the id
        move.l  %a2, -(%sp)             | the view
        jsr     core_param_step_run     | ../core/step.s
        lea     16(%sp), %sp
        move.l  %d0, %d5
9:      tst.l   %d5                     | the flags the move left; rts keeps them
        rts
