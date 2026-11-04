| SPDX-License-Identifier: GPL-2.0-or-later
| core 5.0 (Digitakt mk1): the firmware's moments as events. ColdFire V4;
| assemble with -mcpu=54455. Beside core.s.
|
| Each event here is an observer list unless it says "may take": its
| handlers get the routine's own arguments (longs, as the stock pushed
| them) and every register comes back as it went. A handler that may take
| returns nonzero to stop the stock: the routine does not run (or the call
| it replaced is skipped). Rates and tasks are given per event: an event
| marked "interrupt" runs inside the audio render or the sequencer's tick,
| where a handler must be short and must not block.
|
| Addresses are 1.53's; each OS's port in mod.json gives its own.

| NOTIFY list, nargs[, veto]: call each handler of `list` with the routine's
| first `nargs` stack arguments (at an entry: 4(sp) is the first). With veto,
| a handler's nonzero return returns from the routine at once.
        .macro  NOTIFY list, nargs, veto=0
        tst.l   \list
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     \list, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        .rept   \nargs
        move.l  (20+4*\nargs)(%sp), -(%sp)
        .endr
        jsr     (%a0)
        lea     (4*\nargs)(%sp), %sp
        .if     \veto
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        rts
        .else
        bra.s   1b
        .endif
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:
        .endm

| CALLV list, value: an observer list called with one long (an immediate or a
| register the macro does not use: not d0-d1/a0-a2); every register kept.
        .macro  CALLV list, val
        tst.l   \list
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     \list, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  \val, -(%sp)
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:
        .endm

        .section .run, "ax"

| ---- storage -------------------------------------------------------------------
| ev_project_saving: f(void *this, int slot, int ctx). A project is about to
| be written to a slot (0-127; 128 the working copy), at the slot writer's
| entry (SLOT_WRITER, 0x4001692a): a SAVE writes slot N, then 128. The
| save job, on the UI side.
        .globl  core_project_saving
core_project_saving:
        NOTIFY  ev_project_saving, 3
        lea     -52(%sp), %sp
        movem.l %d2-%d4/%a2-%fp, (%sp)
        jmp     SLOT_WRITER + 8

| ev_project_saved: f(void *block, int slot, int ok). The slot writer's card
| write returned (0x40016a02): ok is its result (nonzero: written).
        .globl  core_project_saved
core_project_saved:
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        jsr     SLOT_CARD_WRITE
        lea     12(%sp), %sp
        tst.l   ev_project_saved
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_saved, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        moveq   #0, %d1
        move.b  3(%sp), %d1             | ok: the saved d0's low byte
        move.l  %d1, -(%sp)
        move.l  32(%sp), -(%sp)         | slot
        move.l  32(%sp), -(%sp)         | block
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ev_project_loaded: f(void *job, int err). A project load finished (err 0:
| "LOAD OK"), at the load job's completion (LOAD_DONE, 0x4004f054): the
| working project is the loaded one. The UI side.
        .globl  core_project_loaded
core_project_loaded:
        NOTIFY  ev_project_loaded, 2
        link.w  %fp, #-32
        movea.l 8(%fp), %a0
        jmp     LOAD_DONE + 8

| ev_project_new: f(void *project). The working project was made a new one
| (defaults, empty kits and patterns): NEW_PROJECT (0x40084c00) returned. At
| boot when no working copy survives, and NEW PROJECT in a slot.
        .globl  core_project_new
core_project_new:
        move.l  4(%sp), -(%sp)
        bsr.s   7f
        addq.l  #4, %sp
        NOTIFY  ev_project_new, 1
        rts
7:      lea     -20(%sp), %sp
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     NEW_PROJECT + 8

| ev_pattern_op: f(int op, int temp, int part, int what). A kit, pattern or
| sequence is about to be saved (op 0: PATTERN_SAVE, 0x40016b16) or
| reloaded (op 1: PATTERN_RELOAD, 0x40017c16). temp 1: to or from the
| temp area (the working copy); part 0 what 1 the kit, part 1 what 1 the
| pattern, part 1 what 0 the sequence. The UI task, once per op.
        .globl  core_pattern_save, core_pattern_reload
core_pattern_save:
        moveq   #0, %d0
        bsr.s   pattern_op
        lea     -44(%sp), %sp
        movem.l %d2-%d7/%a2-%a4, (%sp)
        jmp     PATTERN_SAVE + 8
core_pattern_reload:
        moveq   #1, %d0
        bsr.s   pattern_op
        link.w  %fp, #-44
        movem.l %d2-%d7/%a2-%a4, (%sp)
        jmp     PATTERN_RELOAD + 8
| d0 = op; from the stubs above: 8(sp) the routine's this, 12 temp, 16 ctx,
| 20 part, 24 what (bytes in longs).
pattern_op:
        tst.l   ev_pattern_op
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_pattern_op, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        moveq   #0, %d1
        move.b  47(%sp), %d1            | what
        move.l  %d1, -(%sp)
        move.b  47(%sp), %d1            | part
        move.l  %d1, -(%sp)
        move.b  43(%sp), %d1            | temp
        move.l  %d1, -(%sp)
        move.l  12(%sp), -(%sp)         | op: the saved d0
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ev_sample_assign: f(const char *name, int slot, int ok). A sample was put in
| a slot of the project's sample list (SAMPLE_ASSIGN, 0x4001da6a, returned;
| slot as asked: below 0 the first free one; ok nonzero when it went in).
| The sampler and the sample browser, on the UI side.
        .globl  core_sample_assign
core_sample_assign:
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        move.l  12(%sp), -(%sp)
        bsr.s   7f
        lea     12(%sp), %sp
        tst.l   ev_sample_assign
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_sample_assign, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        moveq   #0, %d1
        move.b  3(%sp), %d1             | ok: the result's low byte
        move.l  %d1, -(%sp)
        move.l  36(%sp), -(%sp)         | slot
        move.l  36(%sp), -(%sp)         | name
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts
7:      link.w  %fp, #-28
        movem.l %d2-%d4/%a2, (%sp)
        jmp     SAMPLE_ASSIGN + 8

| ---- transport and the sequencer ------------------------------------------------
| ev_transport: f(int what, int arg), may take. The sequencer is about to
| start (what 1: SEQ_PLAY 0x4006edf4, PLAY from stop or from pause, MIDI
| Start and Continue; arg its argument, 0 from the PLAY key), to stop (0:
| SEQ_STOP 0x40070218, the STOP key, MIDI and the project loads; arg its
| first argument) or to pause (2: SEQ_PAUSE 0x4006fd86, PLAY while
| playing). A handler that takes it: the sequencer does not. The UI task or
| the MIDI task. When an external clock stops (SYNC_STOP 0x4006f29c, the
| sync dispatcher in the render), what 0 and arg -1: there a handler's
| answer is not asked (interrupt level).
        .globl  core_play, core_stop, core_pause, core_sync_stop
core_play:
        moveq   #1, %d0
        bsr.s   core_transport
        bne.s   8f
        lea     -88(%sp), %sp           | SEQ_PLAY's first two instructions
        movem.l %d2-%d7/%a2-%fp, (%sp)
        jmp     SEQ_PLAY + 8
8:      rts
core_stop:
        moveq   #0, %d0
        bsr.s   core_transport
        bne.s   8f
        lea     -24(%sp), %sp           | SEQ_STOP's first three instructions
        move.l  %d3, -(%sp)
        move.l  %d2, -(%sp)
        jmp     SEQ_STOP + 8
8:      rts
core_pause:
        clr.l   -(%sp)                  | arg 0 (SEQ_PAUSE has none)
        clr.l   -(%sp)
        moveq   #2, %d0
        bsr.s   core_transport
        addq.l  #8, %sp                 | (flags kept: addq on an address register)
        bne.s   8f
        move.l  %d2, -(%sp)             | SEQ_PAUSE's first two instructions
        jsr     SEQ_GATE
        jmp     SEQ_PAUSE + 8
8:      rts
core_sync_stop:
        pea     -1.w
        clr.l   -(%sp)
        moveq   #0, %d0
        bsr.s   core_transport
        addq.l  #8, %sp
        clr.w   %d0                     | SYNC_STOP's first two instructions
        clr.l   SYNC_STATE
        jmp     SYNC_STOP + 8
| core_transport: d0 = what, the argument at 8(sp). -> Z clear when taken.
core_transport:
        tst.l   ev_transport
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_transport, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  28(%sp), -(%sp)         | arg (20 saved + return + caller's return)
        move.l  4(%sp), -(%sp)          | what: the saved d0
        jsr     (%a0)
        addq.l  #8, %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        moveq   #1, %d1                 | taken: Z clear
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      cmp.l   %d0, %d0                | Z set: on
        rts

| ev_seq_tick: f(void). Each sequencer tick (SEQ_TICK 0x4007041c, ~157 a
| second, playing or not), before it runs: interrupt level.
        .globl  core_seq_tick
core_seq_tick:
        NOTIFY  ev_seq_tick, 0
        lea     -116(%sp), %sp
        movem.l %d0-%fp, (%sp)
        jmp     SEQ_TICK + 8

| ev_pattern_request: f(void *list, int count). A pattern (or a chain of
| count patterns) is asked for (PATTERN_REQ 0x4006ffa2), before the stock
| queues it: PTN and a trig key, a program change, a project load. While
| playing it starts at the end of the current one (ev_pattern_switch);
| stopped, at once. The UI task or the MIDI task.
        .globl  core_pattern_request
core_pattern_request:
        NOTIFY  ev_pattern_request, 2
        lea     -20(%sp), %sp
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     PATTERN_REQ + 8

| ev_pattern_switch: f(void *pattern). The sequencer takes up another
| pattern (PATTERN_SWITCH 0x4006e6e0, which then sends its program change):
| a queued one at the end of the current one (from the tick, interrupt
| level), or one set while stopped and on STOP (the UI task).
        .globl  core_pattern_switch
core_pattern_switch:
        NOTIFY  ev_pattern_switch, 1
        link.w  %fp, #-4
        move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        jmp     PATTERN_SWITCH + 8

| ev_tempo: f(int tempo, int old). The sequencer's tempo changed (0x400ece46,
| the store into fw_tempo, BPM x 120): after a pattern's tempo is taken up
| (a load, STOP), a restore and the external clock. Every register kept.
        .globl  core_tempo
core_tempo:
        move.l  %d2, FW_TEMPO           | the store this replaced
        tst.l   ev_tempo
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_tempo, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  (%sp), -(%sp)           | old: the saved d0
        move.l  %d2, -(%sp)             | the new tempo
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ---- MIDI ----------------------------------------------------------------------
| ev_midi_in: f(const uint8_t *msg, int len, int source), may take. Every
| message the MIDI task dispatches (the status byte's handler table), on both
| of its paths (DIN 0x400d472c, USB 0x400d4796): notes, controllers,
| program changes, real time and SysEx, before ev_midi_cc, ev_midi_pc and
| ev_sysex. A handler that takes it: the stock never sees the message. The
| MIDI task.
        .globl  core_midi_in_a, core_midi_in_b
core_midi_in_a:
        bsr.s   core_midi_in
        bne.s   8f
        movea.l (%a2,%d0.l*4), %a0      | the two instructions this replaced
        jmp     (%a0)                   | with the site's return address on top
8:      rts
core_midi_in_b:
        bsr.s   core_midi_in
        bne.s   8f
        movea.l (%a2,%d1.l*4), %a0
        jmp     (%a0)
8:      rts
| -> Z clear when a handler took it; every register kept
core_midi_in:
        tst.l   ev_midi_in
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_midi_in, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  36(%sp), -(%sp)         | source  (20 saved + 2 returns + msg, len)
        move.l  36(%sp), -(%sp)         | len
        move.l  36(%sp), -(%sp)         | msg
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        moveq   #1, %d1                 | (d1 is free at the site: Z clear)
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      cmp.l   %d0, %d0
        rts

| ev_sysex: f(const uint8_t *msg, int len, int source), may take. Each whole
| SysEx message, F0 to F7, at the system switch's SysEx case (SYSEX_IN
| 0x400d52c4), F0 7E included (the stock answers device inquiries after it).
| A handler that takes it: the stock does not see it. digihealth's site
| (0x400d52f0) is further in: both coexist. The MIDI task: a handler that
| blocks holds every MIDI input. Reply with fw_sysex_send.
        .globl  core_sysex
core_sysex:
        NOTIFY  ev_sysex, 3, 1
        lea     -16(%sp), %sp
        movem.l %d2-%d3/%a2-%a3, (%sp)
        jmp     SYSEX_IN + 8

| ev_midi_cc: f(int track, int cc, int value, int flags), may take. A control
| change on a track's channel (or the auto channel: track 16), at the
| router's per-track call (CC_TRACK 0x400d6a58), before the stock applies it
| (parameters, NRPN). A handler that takes it: that track does not see it.
| The MIDI task.
        .globl  core_midi_cc
core_midi_cc:
        NOTIFY  ev_midi_cc, 4, 1
        tst.l   20(%sp)                 | CC_TRACK's first two instructions
        sne     %d0
        jmp     CC_TRACK + 6

| ev_midi_pc: f(int channel, int program, int flags), may take. A program
| change (PC_IN 0x400d4c0c), before the stock looks at it (pattern changes
| when the program change receive channel matches). flags: 0. The MIDI task.
        .globl  core_midi_pc
core_midi_pc:
        tst.l   ev_midi_pc
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_midi_pc, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        movea.l 24(%sp), %a1            | msg
        clr.l   -(%sp)                  | flags
        moveq   #0, %d1
        move.b  1(%a1), %d1
        move.l  %d1, -(%sp)             | program
        move.b  (%a1), %d1
        moveq   #15, %d0
        and.l   %d0, %d1
        move.l  %d1, -(%sp)             | channel
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      lea     -44(%sp), %sp           | PC_IN's first two instructions
        movem.l %d2-%d4, (%sp)
        jmp     PC_IN + 8

| ev_midi_out: f(const uint8_t *buf, int len, int port), may take. Each
| message or SysEx piece (at most 144 bytes) the sender sends (MIDI_SEND
| 0x400d6bd0; port 1 the default, the others by the MIDI settings), and each
| real-time byte (clock, start, stop: RT_SEND 0x400d6b8a, len 1, port -1).
| A handler that takes it: it is not sent. The caller's task.
        .globl  core_midi_out, core_midi_rt_out
core_midi_out:
        tst.l   ev_midi_out
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_midi_out, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  32(%sp), -(%sp)         | port
        move.l  28(%sp), -(%sp)         | len (its first argument)
        move.l  36(%sp), -(%sp)         | buf (its second)
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      lea     -12(%sp), %sp           | MIDI_SEND's first two instructions
        movem.l %d2-%d4, (%sp)
        jmp     MIDI_SEND + 8
core_midi_rt_out:
        tst.l   ev_midi_out
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_midi_out, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        pea     -1.w                    | port
        pea     1.w                     | len
        pea     35(%sp)                 | buf: the byte, the low one of 4(sp)
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      mvz.b   7(%sp), %d0             | RT_SEND's first two instructions
        move.l  %d0, 4(%sp)
        jmp     RT_SEND + 8

| ---- notes ---------------------------------------------------------------------
| ev_live_note: f(int track, int note, int vel, int on), may take. A played
| note reaches a track before its voice: on 1 (NOTE_ON 0x400d53dc), the trig
| keys, keyboard mode, the preview, a MIDI note on a track's channel; on 0
| (NOTE_OFF 0x400d575e), its release (vel: the routine's third argument). A
| handler that takes it: the track does not play (or release) it. The UI
| task or the MIDI task. The sequencer's own trigs do not pass here
| (seq-events-dt1).
        .globl  core_note_on, core_note_off
core_note_on:
        moveq   #1, %d0
        bsr.s   live_note
        bne.s   8f
        link.w  %fp, #-104              | NOTE_ON's first two instructions
        moveq   #127, %d0
        jmp     NOTE_ON + 6
8:      rts
core_note_off:
        moveq   #0, %d0
        bsr.s   live_note
        bne.s   8f
        link.w  %fp, #-108              | NOTE_OFF's first two instructions
        moveq   #127, %d0
        jmp     NOTE_OFF + 6
8:      rts
| d0 = on; 8(sp) track, 12 note, 16 vel. -> Z clear when taken.
live_note:
        tst.l   ev_live_note
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_live_note, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  (%sp), -(%sp)           | on: the saved d0
        move.l  40(%sp), -(%sp)         | vel
        move.l  40(%sp), -(%sp)         | note
        move.l  40(%sp), -(%sp)         | track
        jsr     (%a0)
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        moveq   #1, %d1                 | taken: Z clear
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      cmp.l   %d0, %d0
        rts

| ---- the UI ----------------------------------------------------------------------
| ev_ui_msg: f(uint8_t *msg), may take. Every message the UI task takes from
| its mailbox (0x4000b6f4), before the switch on its type (the byte at +0):
| keys, knobs, the 30 Hz tick, the sequencer's and the MIDI task's posts. A
| handler that takes it: the task goes back to its mailbox, as for the types
| the stock ignores. The UI task.
        .globl  core_ui_msg
core_ui_msg:
        tst.l   ev_ui_msg
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_ui_msg, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  (%sp), -(%sp)           | msg: the saved d0
        jsr     (%a0)
        addq.l  #4, %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     24(%sp), %sp            | and the return into the switch
        jmp     UI_LOOP
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      movea.l %d0, %a2                | the three instructions this replaced
        mvz.b   (%a2), %d0
        cmp.l   %d0, %d1                | its flags go to the switch's test
        rts

| ev_view: f(int op, void *container, void *a, void *b), before the view stack
| changes (the UI task). op 0 push (VIEW_PUSH 0x400ca776): a = &new view, b =
| flag (nonzero: first close views of the same name); op 1 replace
| (VIEW_REPLACE 0x400cb16c, the page keys): a = &old, b = &new; op 2 pop
| (VIEW_POP 0x400caa58, NO, FUNC release): a and b are not the routine's.
| container is the Brain + 0x40 for the main stack.
        .globl  core_view_push, core_view_replace, core_view_pop, fw_view
core_view_push:
        moveq   #0, %d0
        bsr.s   core_view
        lea     -40(%sp), %sp
        movem.l %d2-%d4/%a2-%fp, (%sp)
        jmp     VIEW_PUSH + 8
core_view_replace:
        moveq   #1, %d0
        bsr.s   core_view
        link.w  %fp, #-44
        movem.l %d2/%a2-%a5, (%sp)
        jmp     VIEW_REPLACE + 8
core_view_pop:
        moveq   #2, %d0
        bsr.s   core_view
        lea     -48(%sp), %sp
        movem.l %d2-%d6/%a2-%fp, (%sp)
        jmp     VIEW_POP + 8

core_view:
        tst.l   ev_view
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        move.l  36(%sp), -(%sp)
        move.l  36(%sp), -(%sp)
        move.l  36(%sp), -(%sp)
        move.l  %d0, -(%sp)
        lea     ev_view, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        jsr     (%a0)
        bra.s   1b
2:      lea     16(%sp), %sp
        movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| fw_view(): the view on top of the main stack (the one keys go to), or 0
| (before the first LED frame, where core_key_leds keeps the main stack's
| container); in d0 and a0 (the C compiler takes a pointer result from a0).
fw_view:
        move.l  core_view_ctr, %d0
        beq.s   1f
        movea.l %d0, %a0
        lea     0x14(%a0), %a1          | the list's end
        movea.l 0x18(%a0), %a0          | its last node: the top view's
        moveq   #0, %d0
        cmpa.l  %a0, %a1
        beq.s   1f
        move.l  8(%a0), %d0
1:      movea.l %d0, %a0
        rts

| ev_mute: f(int where, int track, int muted, int row). A track's mute is
| about to change (the UI task, FUNC + a trig key): where 0 the global mutes
| (MUTE_GLOBAL 0x40020008), 1 the pattern's (MUTE_PATTERN 0x400130c4, the
| "Pattern mutes" mode: FUNC + BANK twice), 2 a song row's (MUTE_ROW
| 0x40020eae, song mode; row its row, else -1). track 0-15, muted 1 or 0.
        .globl  core_mute_global, core_mute_pattern, core_mute_row
core_mute_global:
        moveq   #0, %d0
        bsr.s   mute_where
        link.w  %fp, #-48
        movem.l %d2-%d6/%a2-%a5, (%sp)
        jmp     MUTE_GLOBAL + 8
core_mute_pattern:
        moveq   #1, %d0
        bsr.s   mute_where
        link.w  %fp, #-52
        movem.l %d2-%d7/%a2-%a5, (%sp)
        jmp     MUTE_PATTERN + 8
| d0 = where; 12(sp) track, 16(sp) muted (a byte in a long)
mute_where:
        tst.l   ev_mute
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_mute, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        pea     -1.w                    | row
        moveq   #0, %d1
        move.b  43(%sp), %d1            | muted
        move.l  %d1, -(%sp)
        move.l  40(%sp), -(%sp)         | track
        move.l  12(%sp), -(%sp)         | where: the saved d0
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts
| at the routine's entry: 4(sp) its object, 8 row, 12 track, 16 muted (a byte)
core_mute_row:
        tst.l   ev_mute
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_mute, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  28(%sp), -(%sp)         | row
        moveq   #0, %d1
        move.b  43(%sp), %d1            | muted
        move.l  %d1, -(%sp)
        move.l  40(%sp), -(%sp)         | track
        pea     2.w                     | where
        jsr     (%a0)
        lea     16(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      lea     -16(%sp), %sp           | MUTE_ROW's first two instructions
        movem.l %d2-%d4/%a2, (%sp)
        jmp     MUTE_ROW + 8

| ev_key_leds: f(void *container). Each UI tick's LED frame (0x400099b6),
| after the LEDs are cleared and before the views set theirs: the first
| setter of an LED wins, so a handler's LEDs stay over the stock views'.
| Set them with fw_led_set(brain, led, state, colour), brain = container -
| 0x40. The UI task, 30 times a second.
        .globl  core_key_leds
core_key_leds:
        movea.l 4(%sp), %a0             | the main stack's container, for fw_view
        move.l  %a0, core_view_ctr      | (a0 is free: the call this replaced)
        NOTIFY  ev_key_leds, 1
        jmp     VIEW_LEDS

        .section .bss
        .align  4
core_view_ctr:
        .space  4
        .section .run, "ax"

| ---- voices and the engine's parameters ------------------------------------------
| ev_voice_on: f(int voice, int track, void *event). A track's voice starts
| (0x40077a7a, in the render's event loop, as the start bit for the track is
| set): voice and track are the same on the Digitakt (one voice a track);
| event is the trig's record. Interrupt level (the render), once per start:
| a handler must be short. Every register kept. The sites digihealth's fast
| audio patches (the voice-start calls further on) are not touched.
        .globl  core_voice_on
core_voice_on:
        or.l    %d0, %d3                | the start bit (this replaced it)
        tst.l   ev_voice_on
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_voice_on, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  16(%sp), -(%sp)         | event: the saved a2
        move.l  %d2, -(%sp)             | track
        move.l  %d2, -(%sp)             | voice
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      move.l  0x800019ac, %d0         | the second instruction this replaced
        rts

| ev_sound_live: f(int value, int track, int slot). A sound parameter is
| written into the engine's copy (fw_set_param, 0x400771f8, past its first
| three instructions): value 8.8, track 0-7 (16: the master), slot the
| parameter. The knobs, p-lock playback and the CC maps come this way. The UI
| task or the sequencer. Every register kept.
        .globl  core_sound_live
core_sound_live:
        tst.l   ev_sound_live
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_sound_live, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  12(%sp), -(%sp)         | slot: the saved a1
        move.l  4(%sp), -(%sp)          | track: the saved d0
        move.l  12(%sp), -(%sp)         | value: the saved d1
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      lea     0x800014f0, %a0         | the instruction this replaced
        rts

| ev_render_params: f(int16_t *words). Each render pass, once the engine's
| parameter words are complete for it (0x40077f7c, before the voices start):
| track t's 53 words at words + 0x35 t (fw_voice_params is word 17 of track
| 0). A handler may change them for this pass. Interrupt level, ~1500 a
| second: keep it short. Every register kept.
        .globl  core_render_params
core_render_params:
        tst.l   ev_render_params
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_render_params, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        movea.l 16(%sp), %a1            | the render's state: the saved a2
        pea     18(%a1)                 | its tracks' words
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      move.l  (%sp), -(%sp)           | the return, one long down,
        move.l  %d0, -(%sp)
        move.l  #FW_VOICE_VEL, %d0
        move.l  %d0, 8(%sp)             | and under it what `pea FW_VOICE_VEL` pushed
        move.l  (%sp)+, %d0
        rts

| ---- edits ---------------------------------------------------------------------
| The pattern's track objects these pass have their track's index in the
| long at +0x38. All in the UI task, per key or knob detent.
|
| ev_trig_set: f(void *track, int step, int on). A step's trig is placed (on
| 1) or removed (0), in GRID RECORDING and with the trig keys: TRIG_SET
| (0x400261c2), a basic trig, and TRIG_SET_D (0x4002622a), a trig with the
| default flags. Removing a trig is this with on 0 (there is no other step
| clear).
        .globl  core_trig_set, core_trig_set_d
core_trig_set:
        NOTIFY  ev_trig_set, 3
        lea     -16(%sp), %sp
        movem.l %d2-%d4/%a2, (%sp)
        jmp     TRIG_SET + 8
core_trig_set_d:
        NOTIFY  ev_trig_set, 3
        lea     -16(%sp), %sp
        movem.l %d2-%d4/%a2, (%sp)
        jmp     TRIG_SET_D + 8

| ev_plock_store: f(void *track, int step, int slot, int value). A parameter
| lock is written (PLOCK_STORE 0x40024768): a trig held and a knob turned
| (the first turn makes the lock with the current value), and the paste.
        .globl  core_plock_store
core_plock_store:
        NOTIFY  ev_plock_store, 4
        move.l  %a2, -(%sp)             | PLOCK_STORE's first three instructions
        move.l  %d2, -(%sp)
        movea.l 12(%sp), %a2
        jmp     PLOCK_STORE + 8

| ev_track_length: f(void *track, int length). A track's length is set
| (TRACK_LENGTH 0x40024f94, before its clamp to 2-64; length in the low
| word): the TRK page's LENGTH, once a track, sixteen times when it is set
| for all of them.
        .globl  core_track_length
core_track_length:
        NOTIFY  ev_track_length, 2
        lea     -20(%sp), %sp
        movem.l %d2-%d3/%a2/%fp, (%sp)
        jmp     TRACK_LENGTH + 8

| ev_param_set: f(void **page, void *sound, void *param). A sound page's knob
| has stored its value in the sound (its words at +0x14, 2 bytes a slot) and
| the page applies it to the engine (PARAM_SET 0x40015e00): param is the
| parameter's descriptor, whose slots ev_sound_live then sees one by one.
| (*page)'s long at +4 is the track.
        .globl  core_param_set
core_param_set:
        NOTIFY  ev_param_set, 3
        lea     -24(%sp), %sp
        movem.l %d2-%d3/%a2-%a5, (%sp)
        jmp     PARAM_SET + 8

| ---- kits and sounds, read from and written to their records ---------------------------------------------
| Beside the entries dt-fm and machine-pages patch: each site is past those
| bytes, where both mods come back to (machine-pages jumps to 0x4007a23e and
| 0x4007a5a8 itself). The UI side; at a project load or save, per kit
| (128) and per sound.
|
| ev_sound_read: f(void *sound, const void *buffer). A sound is about to be
| read from its record (SOUND_LOAD 0x4007a23e, before its type check).
| ev_sound_write: f(void *dest, void *sound, void *ctx). A sound is being
| written to its record (0x4007a5a8).
| ev_kit_write: f(void *dest, void *kit, void *ctx). A kit is being written
| (0x4007a6ae).
| ev_kit_read: f(void *kit, const void *buffer). A kit's record passed its
| check and is about to be read (KIT_BODY 0x4007a386).
        .globl  core_sound_read, core_sound_write, core_kit_write, core_kit_read
core_sound_read:
        tst.l   ev_sound_read
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_sound_read, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  48(%sp), -(%sp)         | buffer (20 saved + return + 16 + sound)
        move.l  48(%sp), -(%sp)         | sound
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      movea.l 24(%sp), %a2            | the two instructions this replaced
        movea.l 28(%sp), %a3
        rts

core_sound_write:
        tst.l   ev_sound_write
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_sound_write, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  56(%sp), -(%sp)         | ctx (20 saved + return + 20 + dest, sound)
        move.l  56(%sp), -(%sp)         | sound
        move.l  56(%sp), -(%sp)         | dest
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      movea.l 28(%sp), %a2
        movea.l 32(%sp), %a3
        rts

core_kit_write:
        tst.l   ev_kit_write
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_kit_write, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  64(%sp), -(%sp)         | ctx (20 saved + return + 28 + dest, kit)
        move.l  64(%sp), -(%sp)         | kit
        move.l  64(%sp), -(%sp)         | dest
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      movea.l 36(%sp), %a2
        movea.l 40(%sp), %a3
        rts

core_kit_read:
        NOTIFY  ev_kit_read, 2
        lea     -28(%sp), %sp           | KIT_BODY's first two instructions
        movem.l %d2-%d4/%a2-%a5, (%sp)
        jmp     KIT_BODY + 8

| ---- the sampler -------------------------------------------------------------------
| ev_sampler: f(int op). The recorder (the SAMPLE key's page) changes state:
| op 0 armed (SMP_ARM 0x400768a0, YES), 1 recording (SMP_START 0x400768ce,
| FUNC+YES), 2 stopped (SMP_STOP 0x40076918, YES while recording), 3
| finished (0x4000b172, after the take is trimmed: fw_sampler_state 4 kept, 0
| empty), 4 aborted (SMP_ABORT 0x4007693c, FUNC+NO while armed), 5 a kept
| take is to be saved (SMP_SAVE 0x400a8526, YES: the save job starts and
| picks the slot), 6 the take is being written to the +Drive (SMP_WRITE
| 0x400a8854). A threshold
| start while armed happens in the render (fw_sampler_state 1 to 2): no
| event. The UI task.
        .globl  core_smp_arm, core_smp_start, core_smp_stop, core_smp_finish, core_smp_abort
        .globl  core_smp_save, core_smp_write
core_smp_arm:
        CALLV   ev_sampler, #0
        move.l  %d2, -(%sp)             | SMP_ARM's first three instructions
        move.w  %sr, %d1
        move.w  #0x2700, %sr
        jmp     SMP_ARM + 8
core_smp_start:
        CALLV   ev_sampler, #1
        move.l  %d3, -(%sp)
        move.l  %d2, -(%sp)
        move.w  %sr, %d3
        jmp     SMP_START + 6
core_smp_stop:
        CALLV   ev_sampler, #2
        move.l  %d2, -(%sp)
        move.w  %sr, %d2
        move.w  #0x2700, %sr
        jmp     SMP_STOP + 8
core_smp_finish:
        jsr     SMP_FINISH
        CALLV   ev_sampler, #3
        rts
core_smp_abort:
        CALLV   ev_sampler, #4
        lea     -12(%sp), %sp
        movem.l %d2-%d4, (%sp)
        jmp     SMP_ABORT + 8
core_smp_save:
        CALLV   ev_sampler, #5
        link.w  %fp, #-104              | SMP_SAVE's first two instructions
        movea.l 8(%fp), %a0
        jmp     SMP_SAVE + 8
core_smp_write:
        CALLV   ev_sampler, #6
        link.w  %fp, #-28               | SMP_WRITE's first two instructions
        movea.l 8(%fp), %a0
        jmp     SMP_WRITE + 8

| ---- clipboard and reload --------------------------------------------------------
| ev_clip_op: f(int op, void *a, int b). The FUNC+RECORD / STOP / PLAY ops
| (on the pattern, or on the held trig's step), about to run: op 0 copy
| (CLIP_COPY's call 0x4002c162: a the pattern store, b the index), 1 paste
| (CLIP_PASTE 0x40015d0e: a the track object, b the menu item, 0 for a
| plain paste; the boot runs it twice too), 2 clear (the call 0x40012c0c: a the object, b its
| argument), 3 a track's sound copied (SOUND_COPY 0x4003bd82, TRK held and
| FUNC+RECORD on the SRC page: a the page, b what: below 8 the track's
| sound, 8-15 a MIDI track's settings, 16 the FX), 4 that paste (SOUND_PASTE
| 0x4003c0d0, FUNC+STOP), 5 that clear (SOUND_CLEAR 0x4003bee8, FUNC+PLAY).
| The UI task.
        .globl  core_clip_copy, core_clip_paste, core_clip_clear, core_sound_copy
        .globl  core_sound_paste, core_sound_clear
core_clip_copy:
        moveq   #0, %d0
        movea.l 4(%sp), %a0
        move.l  8(%sp), %d1
        bsr.s   clip_op
        jmp     CLIP_COPY               | the call this replaced, its return on top
core_clip_clear:
        moveq   #2, %d0
        movea.l 4(%sp), %a0
        move.l  8(%sp), %d1
        bsr.s   clip_op
        jmp     CLIP_CLEAR
core_clip_paste:
        moveq   #1, %d0
        movea.l 8(%sp), %a0
        move.l  12(%sp), %d1
        bsr.s   clip_op
        lea     -20(%sp), %sp           | CLIP_PASTE's first two instructions
        movem.l %d2-%d3/%a2-%a4, (%sp)
        jmp     CLIP_PASTE + 8
core_sound_copy:
        moveq   #3, %d0
        movea.l 4(%sp), %a0
        move.l  8(%sp), %d1
        bsr.s   clip_op
        link.w  %fp, #-32               | SOUND_COPY's first two instructions
        moveq   #7, %d0
        jmp     SOUND_COPY + 6
core_sound_paste:
        moveq   #4, %d0
        movea.l 4(%sp), %a0
        move.l  8(%sp), %d1
        bsr.s   clip_op
        link.w  %fp, #-36               | SOUND_PASTE's first two instructions
        moveq   #7, %d0
        jmp     SOUND_PASTE + 6
core_sound_clear:
        moveq   #5, %d0
        movea.l 4(%sp), %a0
        move.l  8(%sp), %d1
        bsr.s   clip_op
        link.w  %fp, #-36               | SOUND_CLEAR's first two instructions
        moveq   #7, %d0
        jmp     SOUND_CLEAR + 6
| d0 = op, a0 = a, d1 = b (scratch at these sites: a call or a routine's entry)
clip_op:
        tst.l   ev_clip_op
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_clip_op, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  4(%sp), -(%sp)          | b: the saved d1
        move.l  12(%sp), -(%sp)         | a: the saved a0
        move.l  8(%sp), -(%sp)          | op: the saved d0
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ev_track_reload: f(int track). A held trig key and NO reload that track's
| sound from the saved pattern (TRACK_RELOAD 0x40028df2), before the stock
| releases the track's queued voices and reloads it. The UI task.
        .globl  core_track_reload
core_track_reload:
        tst.l   ev_track_reload
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_track_reload, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        movea.l 24(%sp), %a1            | its argument
        movea.l (%a1), %a1              | the object it points to
        move.l  8(%a1), -(%sp)          | the track
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      movea.l 4(%sp), %a0             | TRACK_RELOAD's first two instructions
        movea.l (%a0), %a0
        jmp     TRACK_RELOAD + 6

| ev_project_erase: f(int slot, int how). A project slot is about to be
| cleared (how 0: MANAGE PROJECTS > CLEAR, the slot made a new project, then
| written; CLEAR_SLOT 0x4009213a, called from the clear job's loop through
| a register, which the core points here) or deleted (how 1: DELETE,
| SLOT_DELETE 0x40091fbc). Once per ticked slot, the UI side: a mod drops
| the data it keeps for that slot.
        .globl  core_project_clear, core_project_delete
core_project_clear:
        move.l  8(%sp), %d0             | the slot (scratch: a call's d0)
        moveq   #0, %d1
        bsr.s   project_erase
        jmp     CLEAR_SLOT              | with the call's return on top
core_project_delete:
        move.l  8(%sp), %d0
        moveq   #1, %d1
        bsr.s   project_erase
        move.l  %d2, -(%sp)             | SLOT_DELETE's first three instructions
        moveq   #-1, %d0
        move.l  12(%sp), %d2
        jmp     SLOT_DELETE + 8
| d0 = slot, d1 = how
project_erase:
        tst.l   ev_project_erase
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_erase, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  4(%sp), -(%sp)          | how: the saved d1
        move.l  4(%sp), -(%sp)          | slot: the saved d0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ---- song mode and the beat --------------------------------------------------------
| ev_song_mode: f(int on). Song mode is switched on (1: the sequencer is to
| take its rows from the song) or off (0), at SONG_MODE 0x4007162c; it takes
| effect at the current row's end (ev_pattern_switch then gives the row's
| pattern). The UI task.
        .globl  core_song_mode
core_song_mode:
        tst.l   ev_song_mode
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_song_mode, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  24(%sp), %d1            | the routine's argument
        moveq   #0, %d0
        cmpi.l  #SONG_TABLE, %d1        | the song's table: on
        bne.s   3f
        moveq   #1, %d0
3:      move.l  %d0, -(%sp)
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      lea     -24(%sp), %sp           | SONG_MODE's first three instructions
        move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        jmp     SONG_MODE + 8

| ev_beat: f(int beat). Each metronome beat while playing (0x40071314, the
| click's on/off test, whether the click is on or not): beat is the
| firmware's beat byte (BEAT_POS; nonzero: the accented click). From the
| sequencer's tick: interrupt level, keep it short. Every register and the
| flags come back as they went.
        .globl  core_beat
core_beat:
        tst.l   ev_beat
        beq.s   7f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_beat, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        moveq   #0, %d0
        move.b  BEAT_POS, %d0
        move.l  %d0, -(%sp)
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
7:      tst.l   METRO_ON                | the test this replaced, last: its flags
        rts
