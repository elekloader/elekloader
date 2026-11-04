| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 5.0: the firmware's moments as events (Digitone mk1). ColdFire V4;
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
| Addresses are 1.43's; each OS's port in mod.json gives its own.

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

| CALL0 list / CALL1 list, value: an observer list called with no argument,
| or with one long; every register kept.
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

| ---- the system ---------------------------------------------------------------
| ev_boot_ui: f(void). The init task has loaded the project, built the engine
| and the MIDI task and is about to start the UI task (0x4008d3d4, the call of
| the UI spawner): the end of the boot. The views come next, in the UI task.
        .globl  core_boot_ui
core_boot_ui:
        CALLV   ev_boot_ui, #0
        jmp     UI_SPAWN

| ev_usb: f(int configured). The USB device was configured by the host (1) or
| its configuration was dropped (0): the gate for USB MIDI out. The USB
| stack's event callback (0x40004778, 0x400047ce); may be interrupt level.
        .globl  core_usb_on
core_usb_on:
        CALLV   ev_usb, #1
        move.l  %d0, USB_CONFIGURED     | the store this replaced (d0 = 1), last: its flags
        rts
        .globl  core_usb_off
core_usb_off:
        CALLV   ev_usb, #0
        clr.l   USB_CONFIGURED
        rts

| ---- storage -------------------------------------------------------------------
| ev_project_saving: f(void *store, int slot, int arg3). A project is about
| to be written to a slot (0-127; 128 the working project), at the slot
| writer's entry (0x40027704). The save task or the UI task.
        .globl  core_project_saving
core_project_saving:
        NOTIFY  ev_project_saving, 3
        lea     -52(%sp), %sp
        movem.l %d2-%d4/%a2-%fp, (%sp)
        jmp     SLOT_WRITER + 8

| ev_project_saved: f(void *block, int slot, int ok). The slot writer's card
| write returned (0x400277dc): ok is its result (nonzero: written).
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

| ---- transport and the sequencer ------------------------------------------------
| ev_transport: f(int what, int arg), may take. The sequencer is about to
| start (what 1: SEQ_START 0x40093e1c, arg its argument: 0 from the PLAY key
| and MIDI Start, 1 for MIDI Continue), to stop (0: SEQ_STOP 0x400952f0, the
| STOP key; arg its second argument) or to pause (2: SEQ_PAUSE 0x40094de0,
| PLAY while playing and MIDI Stop). A handler that takes it: the sequencer
| does not. The UI task or the MIDI task.
        .globl  core_play, core_stop, core_pause
core_play:
        moveq   #1, %d0
        bsr.s   core_transport
        bne.s   8f
        lea     -88(%sp), %sp           | SEQ_START's first two instructions
        movem.l %d2-%d7/%a2-%fp, (%sp)
        jmp     SEQ_START + 8
8:      rts
core_stop:
        move.l  8(%sp), -(%sp)          | arg: SEQ_STOP's second argument
        clr.l   -(%sp)                  | where core_play has its caller's return
        moveq   #0, %d0
        bsr.s   core_transport
        addq.l  #8, %sp                 | (flags kept: addq on an address register)
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
        addq.l  #8, %sp
        bne.s   8f
        move.l  %d2, -(%sp)             | SEQ_PAUSE's first two instructions
        jsr     STOP_CALL
        jmp     SEQ_PAUSE + 8
8:      rts
| core_transport: d0 = play, the argument at 8(sp). -> Z clear when taken.
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
        move.l  4(%sp), -(%sp)          | play: the saved d0
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

| ev_seq_tick: f(void). Each sequencer tick (0x40095518, ~140 a second), before
| it runs: interrupt.
        .globl  core_seq_tick
core_seq_tick:
        NOTIFY  ev_seq_tick, 0
        lea     -116(%sp), %sp
        movem.l %d0-%fp, (%sp)
        jmp     SEQ_TICK + 8

| ev_pattern_request: f(void *ui, void *req, int arg3). A pattern change is
| asked for (0x40029424), before the stock queues it. The UI task.
        .globl  core_pattern_request
core_pattern_request:
        NOTIFY  ev_pattern_request, 3
        link.w  %fp, #-88
        movea.l 8(%fp), %a0
        jmp     PATTERN_REQ + 8

| ---- MIDI ----------------------------------------------------------------------
| ev_midi_in: f(const uint8_t *msg, int len, int source), may take. Every
| message the MIDI task dispatches (the status byte's handler table), on both
| of its paths (0x400e99f4, 0x400e9a5e): notes, controllers, program changes,
| real time and SysEx, before ev_midi_cc, ev_midi_pc and ev_sysex. A handler
| that takes it: the stock never sees the message. The MIDI task.
        .globl  core_midi_in_a
core_midi_in_a:
        bsr.s   core_midi_in
        bne.s   8f
        movea.l (%a2,%d0.l*4), %a0      | the two instructions this replaced
        jmp     (%a0)                   | with the site's return address on top
8:      rts
        .globl  core_midi_in_b
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
        moveq   #1, %d1                 | taken: Z clear (d1 is the dispatch's, unused after)
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      cmp.l   %d0, %d0                | Z set: dispatch it
        rts

| ev_midi_out: f(const uint8_t *buf, int len, int port), may take. Every
| message and SysEx piece the port switch sends (0x400edf32): port 1 the
| default, 2 USB, 3 DIN, 4 both. A handler that takes it: not sent. Any task.
        .globl  core_midi_out
core_midi_out:
        NOTIFY  ev_midi_out, 3, 1
        lea     -12(%sp), %sp
        movem.l %d2-%d4, (%sp)
        jmp     MIDI_SEND + 8

| ---- voices ----------------------------------------------------------------------
| ev_voice_free: f(int voice). A voice is released (0x4009f9bc): its note
| ended, a note-off ran out, it was retriggered or its track stopped.
| Interrupt (the render).
        .globl  core_voice_free
core_voice_free:
        NOTIFY  ev_voice_free, 1
        lea     -16(%sp), %sp
        moveq   #7, %d1
        jmp     VOICE_FREE + 6

| ---- the UI task -------------------------------------------------------------------
| ev_ui_msg: f(uint8_t *msg). Each message the UI task takes from its queue
| (0x40019cda), before it switches on the type byte at msg[0]: a handler may
| rewrite it (a type past 34 is dropped). The UI task.
        .globl  core_ui_msg
core_ui_msg:
        move.l  4(%sp), -(%sp)
        jsr     QUEUE_GET
        addq.l  #4, %sp
        CALLV   ev_ui_msg, (%sp)        | the message: the saved d0
        rts

| ---- sequencer editing (the UI task) --------------------------------------------------
| ev_trig_set: f(void *pattern_track, int step, int mask, int on): a step's
| flags change (mask 1 and 2: its trigs) (0x4003673a).
        .globl  core_trig_set
core_trig_set:
        NOTIFY  ev_trig_set, 4
        link.w  %fp, #-36
        movem.l %d2-%d6/%a2-%a3, (%sp)
        jmp     TRIG_SET + 8
| ev_step_note: f(void *track, int step, int note, int slot): a note slot of
| a step is written (note -1 clears it) (0x40035858).
        .globl  core_step_note
core_step_note:
        NOTIFY  ev_step_note, 4
        link.w  %fp, #-36
        movem.l %d2-%d7/%a2, (%sp)
        jmp     STEP_NOTE + 8
| ev_plock_store: f(void *pattern, int track, int step, int slot, int value):
| a parameter lock is set, by live record or by an edit (0x40024236).
        .globl  core_plock_store
core_plock_store:
        NOTIFY  ev_plock_store, 5
        link.w  %fp, #-36
        movem.l %d2-%d6/%a2, (%sp)
        jmp     PLOCK_SET + 8
| ev_plock_clear: f(void *track, int step, int slot): one lock is removed
| (0x400356c6).
        .globl  core_plock_clear
core_plock_clear:
        NOTIFY  ev_plock_clear, 3
        move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        movea.l 12(%sp), %a2
        jmp     PLOCK_CLEAR + 8
| ev_step_clear: f(void *track, int step): every lock of a step is removed
| (0x40035708).
        .globl  core_step_clear
core_step_clear:
        NOTIFY  ev_step_clear, 2
        move.l  %a2, -(%sp)
        move.l  %d2, -(%sp)
        movea.l 12(%sp), %a2
        jmp     STEP_CLEAR + 8
| ev_track_length: f(void *track, int length): a track's length is set
| (2-64; 0x40035ba0).
        .globl  core_track_length
core_track_length:
        NOTIFY  ev_track_length, 2
        lea     -20(%sp), %sp
        movem.l %d2-%d3/%a2/%fp, (%sp)
        jmp     TRACK_LEN + 8

| ---- sound parameters --------------------------------------------------------------------
| ev_param_set: f(void *page, int id, int value, int a4, int a5, int lock): the
| stock setter commits a parameter (0x40020fc0): to the sound, or with lock
| nonzero to the held steps' locks. The UI task.
        .globl  core_param_set
core_param_set:
        NOTIFY  ev_param_set, 6
        link.w  %fp, #-40
        movem.l %d2-%d7/%a2, (%sp)
        jmp     PARAM_SET + 8
| ev_sound_live: f(int value, int track, int slot): a sound slot's value goes
| to the voices playing the track (0x4009c972, SOUND_LIVE): the last step of
| every stock sound write, and core_sound_set's.
        .globl  core_sound_live
core_sound_live:
        NOTIFY  ev_sound_live, 3
        lea     -16(%sp), %sp
        movem.l %d2-%d4/%a2, (%sp)
        jmp     SOUND_LIVE + 8

| ---- the master's effect stages (interrupt, each block) ----------------------------------
| ev_render_fx: f(int stage, int16_t *out, int16_t *in, void *params), may
| take. Stage 0-2 the three send effects (by their place on the bus: chorus,
| delay, reverb, as far as their parameters say), 3 the master drive. A
| handler that takes it has written out itself: the stock stage is skipped.
        .globl  core_fx0, core_fx1, core_fx2, core_fx3
core_fx0:
        lea     FX_STAGE0, %a1
        moveq   #0, %d0
        bra.s   core_fx
core_fx1:
        lea     FX_STAGE1, %a1
        moveq   #1, %d0
        bra.s   core_fx
core_fx2:
        lea     FX_STAGE2, %a1
        moveq   #2, %d0
        bra.s   core_fx
core_fx3:
        lea     FX_STAGE3, %a1
        moveq   #3, %d0
core_fx:
        tst.l   ev_render_fx
        beq.s   9f
        lea     -12(%sp), %sp
        movem.l %d2/%a1-%a2, (%sp)
        move.l  %d0, %d2
        lea     ev_render_fx, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  24(%sp), -(%sp)         | params (12 saved + return + out, in)
        move.l  24(%sp), -(%sp)         | in
        move.l  24(%sp), -(%sp)         | out
        move.l  %d2, -(%sp)             | stage
        jsr     (%a0)
        lea     16(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d2/%a1-%a2      | taken: the stage is not run
        lea     12(%sp), %sp
        rts
2:      movem.l (%sp), %d2/%a1-%a2
        lea     12(%sp), %sp
9:      jmp     (%a1)                   | the stock stage, with the site's arguments

| ---- tempo and the pattern switch (5.0, wave 2) -------------------------------
| ev_tempo: f(int bpm100), may take. The tempo is about to be set, in hundredths
| of a BPM (the setter clamps it to 3600-36000), at the setter's entry
| (0x4009c82c): the TEMPO menu, the knob. A handler that takes it leaves the
| tempo as it was. The UI task.
        .globl  core_tempo
core_tempo:
        NOTIFY  ev_tempo, 1, 1
        move.l  4(%sp), %d1             | the setter's first two instructions
        cmpi.l  #3600, %d1
        jmp     TEMPO_SET + 10

| ev_pattern_switch: f(int pending). A pattern change asked for earlier is
| about to be applied (0x400fde0c, the routine that moves the pending
| pattern to the current one and resets the steps); pending is its word.
| Called only when one is pending. The sequencer's side.
        .globl  core_pattern_switch
core_pattern_switch:
        tst.l   PATTERN_PENDING
        beq.s   8f
        CALLV   ev_pattern_switch, PATTERN_PENDING
8:      move.l  PATTERN_PENDING, %d0    | the instruction this replaced, last: its flags
        jmp     PATTERN_SWITCH + 6

| ---- the arpeggiator and the voice allocator (5.0, wave 2) -------------------
| ev_arp_step: f(int track, void *event). An arpeggiator step is due on a track
| (0x4009d6a0, the render's arp event branch, not a reset): the stock then
| counts it and decides from the arp pattern. Interrupt, the render.
        .globl  core_arp_step
core_arp_step:
        tst.l   ev_arp_step
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_arp_step, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  %a3, -(%sp)             | the event
        move.l  8(%a3), -(%sp)          | its track
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      move.l  #ARP_COUNTERS, %d1      | the instruction this replaced
        rts

| ev_voice_steal: f(int voice, int old_track). The allocator takes a busy voice
| slot 0-7 for a new note (0x4009f700, its steal path: more notes than
| voices); old_track is the track the voice still plays for (0xff if free).
| Interrupt, the render or the note-on.
        .globl  core_voice_steal
core_voice_steal:
        tst.l   ev_voice_steal
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_voice_steal, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  4(%sp), %d0             | the slot (the saved d1)
        mulu.w  #20, %d0
        lea     VOICE_SLOTS, %a1
        moveq   #0, %d1
        move.b  2(%a1,%d0.l), %d1       | its track (0xff: free)
        move.l  %d1, -(%sp)
        move.l  8(%sp), -(%sp)          | the slot
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      move.l  %d1, %d4                | the three instructions this replaced
        lsl.l   #4, %d4
        move.b  67(%sp), %d5            | the allocator's 63(sp), past this return
        rts
