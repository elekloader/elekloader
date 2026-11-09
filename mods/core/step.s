| SPDX-License-Identifier: GPL-2.0-or-later
| ev_param_step's walk and core_param_shown, shared by core 4.4 (Digitakt
| mk1), core-dn1 5.4 and core-dn2 0.5. Each core's site code (its param.s)
| calls the walk with the turn its page handler holds. Last in sources.
|   SHOWN_FN  each core's: the page views' value of a parameter, a held
|             step's lock first: f(view, id, uint8_t *locked) -> 8.8
|
| Handlers: int32_t f(void *view, int32_t id, int32_t delta, int32_t knob)
| -> the step to add. Each gets the step the one before it left, and
| returns delta itself for a parameter that is not its own: every turn on
| every page comes here, so a handler decides that first, in a few
| instructions. A handler that wants the value the turn starts from asks
| core_param_shown, for its own parameters only. The UI task.

        .text                           | after the C code, so no symbol before it moves
        .globl  core_param_step_run, core_param_shown

| core_param_step_run(view, id, delta, knob) -> the step, after every
| handler; C's calling convention.
core_param_step_run:
        move.l  %a2, -(%sp)
        move.l  16(%sp), %d0            | the step
        lea     ev_param_step, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        move.l  %d0, 16(%sp)            | the step so far, in its argument
        movea.l %d1, %a0
        move.l  20(%sp), -(%sp)         | the knob
        move.l  20(%sp), -(%sp)         | the step
        move.l  20(%sp), -(%sp)         | the id
        move.l  20(%sp), -(%sp)         | the view
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      movea.l (%sp)+, %a2
        rts

| core_param_shown(view, id) -> the value a turn of parameter id on that
| page starts from (8.8): the held step's lock with steps held, else the
| sound's. The UI task.
core_param_shown:
        subq.l  #4, %sp                 | the getter's locked byte
        pea     (%sp)
        move.l  16(%sp), -(%sp)         | the id
        move.l  16(%sp), -(%sp)         | the view
        jsr     SHOWN_FN
        lea     16(%sp), %sp
        rts
