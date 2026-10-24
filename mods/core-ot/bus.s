| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: the hook bus (core 0.2).
| ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| Each site below calls the handlers mods subscribe to its event, from a
| table the linker builds (docs/ADAPTING.md, "The hook bus"): a list of
| pointers ending in 0. Handlers use the C convention: arguments on the
| stack, d0-d1/a0-a1 free, everything else kept, the result in d0.
|
| The sites are not the ones octabam's modules hook: every byte the
| converted mods of irpina/octabam2elemod v1.0 patch stays theirs, so those
| files link with this core as they did with 0.1. The addresses come from
| mod.json (defsym):
|
|   TICK_NEXT   the sys task's tick step (0x40031970), called for message 5
|   KEYFN       key(code, pressed) (0x40031734), the panel's key dispatch
|   ENCFN       enc(encoder, delta) (0x40031944), the encoder dispatch
|   FRAME_PTR   the long holding the frame the compositor builds (0x400b9710)
|   ISR_FLAG    the long the frame interrupt's tail tests (0x46104d08)
|
| From 0.5 also (see each event below): PLAYING (0x800065b8), DTCN3 (DMA timer
| 3's counter), and the addresses the new jmp sites go back to.

        .section .run, "ax"

        .include "meter.inc"

| ---- ev_tick: the sys task's tick, 60 times a second ------------------------
| At 0x40061e94 (was: jsr 0x40031970), in the sys loop's case for message
| kind 5, which the 120 Hz timer posts every second tick. The sys task also
| runs ev_key, ev_enc and (nearly always) ev_draw, so their handlers never
| interrupt one another. Handlers: void f(void).
        .globl  core_tick
core_tick:
        move.l  %a2, -(%sp)
        jsr     ots_tick                | core's store first (store.c, 0.6)
        lea     ev_tick, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   1b
2:      movea.l (%sp)+, %a2
        jmp     TICK_NEXT               | the call this replaced, same stack

| ---- ev_draw: over every composed frame -------------------------------------
| At 0x40013cae in the compositor 0x40013abc (was: movea.l 0x400b9710,%a5),
| through core_draw_gate (gate.s), which comes here once .boot has run;
| after every window is drawn into the frame and before the frame is
| compared with the last one sent and the changed bytes go to the LCD. The
| frame is cleared and composed again each time, so what handlers draw shows
| until the next composition. The compositor's d2-d7/a2-a6 are live here.
| Handlers: void f(unsigned char *frame), each over the last.
        .globl  core_draw
core_draw:
        move.l  %a2, -(%sp)
        lea     ev_draw, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  FRAME_PTR, -(%sp)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movea.l (%sp)+, %a2
        movea.l FRAME_PTR, %a5          | the instruction this replaced
        rts

| ---- ev_key, ev_enc: the panel's keys and encoders --------------------------
| At 0x40061dc8 (was: jsr 0x40031734) and 0x40061e00 (was: jsr 0x40031944),
| the sys loop's cases for message kinds 1 and 2. (sp) = return, 4 the key
| code or encoder, 8 pressed or the delta. Handlers: int f(int, int) -> d0
| nonzero when they took it; then nothing after them sees it, stock included.
        .globl  core_key, core_enc
core_key:
        move.l  %a2, -(%sp)
        lea     ev_key, %a2
        bsr.s   dispatch
        movea.l (%sp)+, %a2
        tst.l   %d0
        bne.s   1f
        jmp     KEYFN                   | the stock path, same stack
1:      rts

core_enc:
        jsr     ots_ui_enc              | a prompt of core's store (0.6) takes the turn
        tst.l   %d0
        bne.s   2f
        move.l  %a2, -(%sp)
        lea     ev_enc, %a2
        bsr.s   dispatch
        movea.l (%sp)+, %a2
        tst.l   %d0
        bne.s   1f
        jmp     ENCFN
1:      rts
2:      rts

| dispatch: a2 = the table; 12(sp) the first argument, 16(sp) the second
| (after its return address and the caller's saved a2). -> d0 = the first
| nonzero result, or 0.
dispatch:
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  16(%sp), -(%sp)
        move.l  16(%sp), -(%sp)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1b
2:      rts

| ---- ev_midi: each MIDI message in -------------------------------------------
| At 0x40005572 in the MIDI thread's loop (was: movea.l (%a2,%d0.l*4),%a0 ;
| jsr (%a0)): the call of the message's handler from the table at a2
| (0x400d6474), d0 = its status >> 4. (sp) = return, 4(sp) = the message
| (status, then its data bytes), 8(sp) = 0. Handlers: int f(const unsigned
| char *msg) -> d0 nonzero when they took it: the firmware's handler is not
| called. Otherwise it is, from the same table, with the same stack.
        .globl  core_midi
core_midi:
        lea     -8(%sp), %sp
        movem.l %d0/%a2, (%sp)          | the handler's index, the table
        lea     ev_midi, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        move.l  12(%sp), -(%sp)         | the message
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0/%a2          | taken
        lea     8(%sp), %sp
        rts
2:      movem.l (%sp), %d0/%a2
        lea     8(%sp), %sp
        movea.l (%a2,%d0.l*4), %a0      | the instructions this replaced
        jmp     (%a0)

| ---- ev_frame: the frame interrupt, every 16 samples ------------------------
| At 0x4000d94e in the frame interrupt 0x4000aad0 (vector 0x41, level 5;
| was: tst.l 0x46104d08), on its normal path after the frame is built and
| before it puts back the interrupted task's EMAC state, so a handler may
| use the EMAC. Every register is restored from the interrupt's frame at
| its end. Handlers: void f(void), at interrupt level: keep them short.
        .globl  core_frame
core_frame:
        lea     ev_frame, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   1b
2:      tst.l   ISR_FLAG                | the instruction this replaced: its
        rts                             | flags feed the interrupt's beq

| ---- ev_frame_end: the end of the frame interrupt (core-ot 0.4) --------------
| At 0x4000d9a6 (was: movem.l (%sp),%d0-%fp ; lea 252(%sp),%sp, then rte): the
| frame interrupt's last point, past TUNER's counter store and USB audio's
| tail (bytes of octabam2elemod v1.0's mods, 0x4000d99a..0x4000d9a5). The
| interrupted task's EMAC state is already back by then, so after the handlers
| the interrupt's own restore of it (0x4000d964..0x4000d980, from its frame)
| runs again: a handler may use the EMAC. Every other register comes back from
| the frame too. Handlers: void f(void), at interrupt level 5: keep them short.
        .globl  core_frame_end
core_frame_end:
        lea     ev_frame_end, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   1b
2:      move.l  DTCN3, %d0              | the frame budget meter: this frame's
        lea     ot_meter, %a0           | interrupt, entry to here, handlers' time in
        sub.l   M_T0(%a0), %d0
        move.l  %d0, M_LAST(%a0)
        add.l   %d0, M_TOTAL(%a0)
        cmp.l   M_WORST(%a0), %d0
        bls.s   3f
        move.l  %d0, M_WORST(%a0)
3:      cmp.l   M_TPF(%a0), %d0
        bls.s   4f
        addq.l  #1, M_OVER(%a0)
4:      addq.l  #1, M_FRAMES(%a0)
        lea     220(%sp), %a0           | the EMAC restore, as the interrupt does it
        move.l  #0, %macsr
        movem.l (%a0), %d0-%d7
        move.l  %d0, %acc0
        move.l  %d1, %acc1
        move.l  %d2, %acc2
        move.l  %d3, %acc3
        move.l  %d4, %accext01
        move.l  %d5, %accext23
        move.l  %d6, %mask
        move.l  %d7, %macsr
        movem.l (%sp), %d0-%d7/%a0-%a6  | the instructions this replaced
        lea     252(%sp), %sp
        rte

| ---- ev_midi_out: each message the firmware sends (core-ot 0.4) --------------
| At 0x40010bd0 in midi_send (was: movea.l 28(%sp),%a2 ; tst.l 0x460ba978),
| past its prologue (USB MIDI of octabam2elemod v1.0 patches the prologue,
| 0x40010bc8..0x40010bcf): 24(sp) is the message's length, 28(sp) the
| message. Handlers: void f(const unsigned char *msg, unsigned len), each in
| turn, then stock sends it. They run where the sender runs (a task, or an
| interrupt): keep them short.
        .globl  core_midi_out
core_midi_out:
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_midi_out, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  44(%sp), -(%sp)         | the length (24 past the 20 saved)
        move.l  52(%sp), -(%sp)         | the message (28, 20 saved, 4 pushed)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
        movea.l 28(%sp), %a2            | the instructions this replaced: the
        tst.l   0x460ba978              | tst last, its flags for stock's bne
        jmp     0x40010bda

| ---- ev_midi_out_rt: each realtime byte the firmware sends (core-ot 0.4) -----
| At 0x400108c4 in the priority sender (was: move.w %sr,%d2 ; move.w
| #0x2700,%sr), past its wait for room in the queue (its entry is USB MIDI's
| of octabam2elemod v1.0, 0x400108b0..0x400108b5): the byte is in d1.
| Handlers: void f(unsigned byte), each in turn, then stock sends it.
        .globl  core_midi_out_rt
core_midi_out_rt:
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1/%a3, (%sp)
        lea     ev_midi_out_rt, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        mvz.b   7(%sp), %d1             | the byte (d1's low byte, saved at 4)
        move.l  %d1, -(%sp)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a1/%a3
        lea     20(%sp), %sp
        move.w  %sr, %d2                | the instructions this replaced
        move.w  #0x2700, %sr
        jmp     0x400108ca

| ---- ev_frame_begin: the head of the frame interrupt (core-ot 0.5) -------------
| At 0x4000aad0, the frame interrupt's entry (vector 0x41, level 5; was: lea
| -252(%sp),%sp ; movem.l %d0-%fp,(%sp)): the stub makes the interrupt's frame,
| as the two instructions did, and the handlers run before it does anything
| else. The interrupted task's EMAC state is NOT saved yet: a handler must not
| touch the EMAC (ev_frame, later, may). Handlers: void f(void), at interrupt
| level: keep them short. Core's own first handler is the frame budget meter:
| it stamps DMA timer 3 before any other.
        .globl  core_frame_begin
core_frame_begin:
        lea     -252(%sp), %sp
        movem.l %d0-%d7/%a0-%a6, (%sp)  | what the two instructions did
        move.l  DTCN3, %d0              | d0 = now
        lea     ot_meter, %a0
        move.l  %d0, M_T0(%a0)          | the meter: this frame began
        move.l  M_PREV(%a0), %d1        | d1 = when the last one began
        move.l  %d0, M_PREV(%a0)
        move.l  M_FRAMES(%a0), %d2
        bne.s   1f
        move.l  %d0, M_WIN(%a0)         | the first frame: start the window
        clr.l   M_WINN(%a0)
        bra.s   5f
1:      move.l  %d0, %d3
        sub.l   %d1, %d3                | d3 = it began this long after the last
        move.l  M_TPF(%a0), %d1
        move.l  %d1, %d2
        lsr.l   #1, %d2
        add.l   %d2, %d1                | d1 = a frame and a half
        cmp.l   %d1, %d3
        bls.s   2f
        addq.l  #1, M_LATE(%a0)         | at least one frame went missing
2:      move.l  M_WINN(%a0), %d2
        addq.l  #1, %d2
        move.l  %d2, M_WINN(%a0)
        cmpi.l  #1024, %d2
        bne.s   5f
        move.l  %d0, %d3                | 1,024 frames are over: they took
        sub.l   M_WIN(%a0), %d3         | this many counts
        lsr.l   #8, %d3
        lsr.l   #2, %d3                 | / 1,024
        beq.s   3f
        move.l  %d3, M_TPF(%a0)         | the counts one frame takes
3:      clr.l   M_WINN(%a0)
        move.l  %d0, M_WIN(%a0)
5:      lea     ev_frame_begin, %a2
4:      move.l  (%a2)+, %d0
        beq.s   6f
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   4b
6:      jmp     FRAME_BEGIN_NEXT

| ---- ev_transport: start, resume and stop (core-ot 0.5) ---------------------
| Three sites that store the sequencer's PLAYING word (0x800065b8: 0 stopped,
| 1 playing, 2 what the STOP routine leaves); each is the store itself, so a
| jmp replaces it and the stub makes it first: handlers see the new word.
|
|   0x4009c3d4  START     the cold start (the PLAY key, MIDI start), in the
|                         transport routine 0x4009b964 (was: move.l %d0,PLAYING)
|   0x4009c4d4  RESUME    the same routine's resume path
|   0x4009f5c6  STOP      the STOP routine 0x4009f5bc, at its head
|
| Handlers: void f(unsigned what, unsigned playing): what is 0 start, 1 resume,
| 2 stop; playing the word as stored (1, 1, 2). They run in the task that
| called the transport routine; they must not call into the sequencer or send
| MIDI (the routine is half done). Every register and the flags come back as
| the store left them.
        .globl  core_t_start, core_t_resume, core_t_stop
core_t_start:
        move.l  %d0, PLAYING            | the store this replaced (d0 = 1)
        move.l  %d1, -(%sp)
        moveq   #0, %d1
        bsr.s   transport
        move.l  (%sp)+, %d1
        tst.l   %d0                     | the flags the store left
        jmp     T_START_NEXT
core_t_resume:
        move.l  %d0, PLAYING            | d0 = 1
        move.l  %d1, -(%sp)
        moveq   #1, %d1
        bsr.s   transport
        move.l  (%sp)+, %d1
        tst.l   %d0
        jmp     T_RESUME_NEXT
core_t_stop:
        move.l  %d0, PLAYING            | d0 = 2
        move.l  %d1, -(%sp)
        moveq   #2, %d1
        bsr.s   transport
        move.l  (%sp)+, %d1
        tst.l   %d0
        jmp     T_STOP_NEXT
transport:                              | d1 = what, d0 = playing; keeps d0, a0, a1
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.l  %a3, -(%sp)
        lea     ev_transport, %a3
1:      move.l  (%a3)+, %d0
        beq.s   2f
        move.l  4(%sp), -(%sp)          | playing (d0 saved: 4 past a3's)
        move.l  12(%sp), -(%sp)         | what (d1 saved: 8 past, and 4 pushed)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movea.l (%sp)+, %a3
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     16(%sp), %sp
        rts

| ---- ev_boot_done and ev_idle: main's end (core-ot 0.5) ----------------------
| At 0x4001fc96, main's last init call (was: jsr 0x40098a2c, which makes the
| flex-analysis task); the `bras .` after it is main's idle. A jsr to this stub
| makes the call, then runs ev_boot_done once: the OS's tasks exist, no project
| is loaded yet. With no ev_idle handler in the build the stub returns, and
| main parks in the stock `bras .` as before. With one it does not return: it is
| main's idle loop, calling ev_idle over and over. Handlers: void f(void), in
| the main task; ev_idle's must not wait for anything: if main blocks, nothing
| is left to run at the lowest priority.
        .globl  core_main_end
core_main_end:
        jsr     IDLE_INIT               | the call this replaced
        jsr     ots_boot                | the CS1 claims (store.c, 0.6), before ev_boot_done
        move.l  %a2, -(%sp)             | main's a2 stays as it was, if it parks
        lea     ev_boot_done, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   1b
2:      tst.l   ev_idle                 | a handler? (the table's first word)
        beq.s   5f
3:      lea     ev_idle, %a2            | main's idle loop
4:      move.l  (%a2)+, %d0
        beq.s   3b
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   4b
5:      movea.l (%sp)+, %a2
        rts                             | to the stock `bras .`

| ---- ev_tick120: the key-repeat task's tick, 120 times a second (core-ot 0.5) --
| At 0x40055966 in the key-repeat task's loop (was: jsr (%a2) ; addq.l #4,%sp ;
| bra 0x4005595c), after the wait for the UI tick event (DMA timer 1's handler
| signals it at 120 Hz) and the key-repeat step. Handlers: void f(void), in
| that task (2 KB of stack): keep them short. core's paced MIDI out drains here.
        .globl  core_tick120
core_tick120:
        jsr     (%a2)                   | the key-repeat step, as the loop did
        addq.l  #4, %sp
        jsr     otp_drain               | core's paced MIDI out (exports.c): one message
        move.l  %a4, -(%sp)
        lea     ev_tick120, %a4
1:      move.l  (%a4)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        jsr     (%a0)
        bra.s   1b
2:      movea.l (%sp)+, %a4
        jmp     TICK120_LOOP
