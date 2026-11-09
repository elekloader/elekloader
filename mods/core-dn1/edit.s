| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 4.2: knob turns and values on the parameter pages (Digitone mk1).
| ColdFire V4; assemble with -mcpu=54455. Beside core.s; the Digitakt's core
| does not have it.
|
|   HELD_STEPS  held(mgr): steps are held on the page's trig-hold manager
|   TURN_DONE   the knob handler's exit, returning 1 (the turn handled)
|   VALUE_FN    the pages' value getter, which this file's second site enters
|   VALUE58     the views' value routine (+0x58), the third site's
|
| Every parameter page, the stock ones and the mods' (27-30), is one class
| in the firmware with one knob handler (0x400452da) and one value getter
| (0x40043dd0), at the same addresses in 1.44. A turn goes to the held
| steps' p-locks when trig keys are held (GRID REC), and to the track's
| sound when none are; a knob shows its value from the same places, a held
| step's lock drawn inverted. A mod that keeps values of its own for a stock
| parameter (a lock for a held note, say) takes both: the turn, and the
| value the page shows.
|
| ---- ev_param_turn: a turn the stock would put into the sound -----------
| At 0x400455ba in the knob handler (was: jsr HELD_STEPS, with the page's
| trig-hold manager at 4(sp)), once it has the turn's parameter and step.
| Core makes that call: with steps held it returns the answer, and the stock
| edits their locks as before. With none, core runs the handlers first; the
| first that returns nonzero takes the turn, and the stock neither edits the
| sound nor shows its pop-up (core leaves through TURN_DONE). Otherwise core
| returns 0 and the stock edits the sound.
|
| Handlers: f(void *view, int id, int delta, int knob) -> nonzero: taken.
| view is the page (a C++ object, +0 its vtable), id the parameter (the
| firmware's, 0-191), delta the step the stock would add (8.8: a whole step
| of a coarse parameter is 256, acceleration in), knob 0-7 for A-H. The UI
| task. Here a2 = the view, d2 = the id, d3 = the step and d4 = the knob.
        .section .run, "ax"
        .globl  core_param_turn
core_param_turn:
        tst.l   ev_param_step           | 5.4: the step first, for the sound and the held steps' locks alike
        beq.s   0f
        jsr     core_param_step_d3      | param.s
0:      move.l  4(%sp), -(%sp)          | 5.1: a lane parameter's is core's (lanes.c)
        move.l  %d3, -(%sp)
        move.l  %d2, -(%sp)
        move.l  %a2, -(%sp)
        jsr     core_lanes_turn
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   0f
        addq.l  #8, %sp                 | our return and the manager
        jmp     TURN_DONE
0:      move.l  4(%sp), -(%sp)          | the manager
        jsr     HELD_STEPS
        addq.l  #4, %sp
        tst.b   %d0
        bne.s   9f                      | steps held: the stock's lock edit
        tst.l   ev_param_turn
        beq.s   9f
        move.l  %a3, -(%sp)
        lea     ev_param_turn, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  %d4, -(%sp)             | the knob
        move.l  %d3, -(%sp)             | the step
        move.l  %d2, -(%sp)             | the id
        move.l  %a2, -(%sp)             | the view
        movea.l %d0, %a0
        jsr     (%a0)
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movea.l (%sp)+, %a3
        addq.l  #8, %sp                 | our return and the manager
        jmp     TURN_DONE
2:      movea.l (%sp)+, %a3
        moveq   #0, %d0                 | no steps held: the stock edits the sound
9:      rts

| ---- ev_param_value: the value a page shows for a parameter ---------------
| At VALUE_FN's entry (0x40043dd0, by jmp; its first two instructions are
| run at 9: below): value(view, id, uint8_t *locked) -> the knob's value for
| parameter id, *locked 1 when it is a held step's p-lock (the page draws it
| inverted). A page asks for each of its knobs every frame it draws them.
| Core runs the handlers first: the first that returns nonzero answers,
| with the value it wrote and *locked as it left it (core clears it first).
| Otherwise the stock answers: the held steps' lock, or the sound's value.
|
| Handlers: f(void *view, int id, int32_t *value, uint8_t *locked) ->
| nonzero: answered. The UI task, every frame: keep them short.
|
| Not every picture on a page comes from that getter. The pages read a
| parameter's value from the track's sound through the view's value routine,
| VALUE58 (+0x58: value(view, id, sound), 0x400432dc, the same in 1.44) for
| what they draw themselves (SYN2's envelope page draws its knobs and both
| envelopes so, SYN1 its algorithm, a knob handler what to draw again), and
| the getter falls back to it. Core asks the handlers at its entry too, for
| the track's own sound (sound -1; a pool sound a step locks is the stock's);
| there `locked` is not used. And at the SYN envelope page's own call of it
| (0x4004b462, 1.44 0x4004b482; was: movea.l 88(a0),a0; jsr (a0)), where
| the page keeps the locked flag it draws a knob with, core asks them and
| sets that flag from their answer.
        .globl  core_param_value
core_param_value:
        subq.l  #4, %sp                 | 5.1: a lane parameter's is core's (lanes.c)
        move.l  16(%sp), -(%sp)         | locked
        pea     4(%sp)                  | &value
        move.l  20(%sp), -(%sp)         | id
        move.l  20(%sp), -(%sp)         | view
        jsr     core_lanes_value
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   0f
        move.l  (%sp)+, %d0
        rts
0:      addq.l  #4, %sp
        tst.l   ev_param_value
        beq.s   9f
        move.l  8(%sp), -(%sp)          | the id
        move.l  8(%sp), -(%sp)          | the view
        bsr.w   ask
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   9f
        movea.l 12(%sp), %a0            | answered: its locked flag, its value
        move.b  ask_locked, %d0
        move.b  %d0, (%a0)
        move.l  %d1, %d0
        rts
9:      lea     -44(%sp), %sp           | the two instructions this replaced
        moveq   #-1, %d0
        jmp     VALUE_FN + 6

        .globl  core_param_value58
core_param_value58:
        subq.l  #4, %sp                 | 5.1: a lane parameter's (lanes.c)
        clr.l   -(%sp)                  | no locked flag
        pea     4(%sp)                  | &value
        move.l  20(%sp), -(%sp)         | id
        move.l  20(%sp), -(%sp)         | view
        jsr     core_lanes_value
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   0f
        move.l  (%sp)+, %d0
        rts
0:      addq.l  #4, %sp
        tst.l   ev_param_value
        beq.s   9f
        moveq   #-1, %d0
        cmp.l   12(%sp), %d0            | the track's own sound
        bne.s   9f
        move.l  8(%sp), -(%sp)
        move.l  8(%sp), -(%sp)
        bsr.w   ask
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   9f
        move.l  %d1, %d0
        rts
9:      lea     -12(%sp), %sp           | the two instructions this replaced
        moveq   #9, %d0
        jmp     VALUE58 + 6

| At the SYN envelope page's call: 4(sp) the view, 8(sp) the id, 12(sp) the
| sound; the page's locked flag at 103(sp) (its sp+87 before the call).
        .equ    SYN_LOCKED, 103
        .globl  core_param_value_syn
core_param_value_syn:
        moveq   #-1, %d0
        cmp.l   12(%sp), %d0
        bne.s   8f
        tst.l   ev_param_value
        beq.s   8f
        move.l  8(%sp), -(%sp)
        move.l  8(%sp), -(%sp)
        bsr.w   ask
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   8f
        move.b  ask_locked, %d0
        move.b  %d0, SYN_LOCKED(%sp)
        move.l  %d1, %d0
        rts
8:      movea.l 4(%sp), %a0             | what this replaced: the view's +0x58
        movea.l (%a0), %a0
        movea.l 88(%a0), %a0
        jmp     (%a0)

| ask: 4(sp) the view, 8(sp) the id -> d0 nonzero when a handler answered,
| then d1 = its value and ask_locked its flag (cleared first). Keeps a2.
ask:
        lea     -8(%sp), %sp
        move.l  %a2, (%sp)              | 4: the value; 12 view, 16 id
        clr.b   ask_locked
        lea     ev_param_value, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        pea     ask_locked
        pea     8(%sp)                  | &value
        move.l  24(%sp), -(%sp)         | the id
        move.l  24(%sp), -(%sp)         | the view
        movea.l %d0, %a0
        jsr     (%a0)
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   1b
2:      move.l  4(%sp), %d1
        movea.l (%sp), %a2
        addq.l  #8, %sp
        rts

        .section .bss
ask_locked:
        .skip   2
