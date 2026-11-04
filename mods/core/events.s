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
