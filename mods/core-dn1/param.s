| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 5.4: ev_param_step, the step a knob turn adds, as core's (4.4) on
| the Digitakt mk1; the walk and core_param_shown are ../core/step.s's.
| core_param_turn (edit.s) calls this first, at the knob handler's question
| (0x400455ba, the same in 1.44), before ev_param_turn and the held-steps
| check: the knob handler adds d3 to the sound or to GRID REC's held steps'
| locks after the call, then clamps it and shows its pop-up. Here a2 = the
| view, d2 = the id, d3 = the step and d4 = the knob.

        .text                           | after the C code, as ../core/step.s
        .globl  core_param_step_d3
core_param_step_d3:
        move.l  %d4, -(%sp)             | the knob
        move.l  %d3, -(%sp)             | the step
        move.l  %d2, -(%sp)             | the id
        move.l  %a2, -(%sp)             | the view
        jsr     core_param_step_run     | ../core/step.s
        lea     16(%sp), %sp
        move.l  %d0, %d3
        rts
