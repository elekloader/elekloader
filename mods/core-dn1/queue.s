| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 4.1: the note queue (Digitone mk1). ColdFire V4; assemble with
| -mcpu=54455. Beside core.s; the Digitakt's core does not have it.
|
|   FW_EV_QUEUE queue(event, time): the routine every note source hands its
|               events to (the sequencer's fires, a trig or keyboard note, MIDI
|               in, the arpeggiator), which links them into the list the render
|               takes them from when the timeline reaches their time
|
| ---- ev_note_queue: each event, with the time it plays -------------------
| The sequencer fires a step about one step before it plays: the fire builds
| the step's event (its note, or none for a lock trig, and its p-locks) and
| queues it for the time the step plays, microtiming and swing in. An event
| reaches the render at that time, which starts its voices with its locks
| or puts a lock trig's locks into the track's voices. A mod that plays a
| track's locks itself (an effect's p-locks) needs that time. At the
| entry of FW_EV_QUEUE (0x400fff04; 1.44 0x40100178), by jmp: core runs the
| handlers with the interrupts masked, as the routine itself then masks
| them, then the routine (its first two instructions at 1: below).
|
| Handlers: f(void *event, uint32_t time). The event is 0x48 bytes
| (core3.h's FW_EVENT_*): +0 its kind (0 a step's fire, its trig a note or a
| lock trig; 1 the first step's, made as PLAY starts; others the
| arpeggiator's and the sequencer's own), +8 its track (0-3 the synth
| tracks), +0x28 a fire's step word (bit 0 a note trig, bit 1 a lock trig),
| +0x38 its p-lock list or 0 (+8 the count, from +0x14 {u16 slot, s16 value}
| 8 bytes apart).
| time is on the timeline (fw_timeline's units). Interrupt level or a task,
| masked: keep them short, only read the event, and call no firmware.
        .section .run, "ax"
        .globl  core_note_queue
core_note_queue:
        tst.l   ev_note_queue
        beq.s   1f
        lea     -8(%sp), %sp
        movem.l %d2/%a2, (%sp)          | 8 the return, 12 the event, 16 the time
        move.w  %sr, %d2
        move.w  #0x2700, %sr
        lea     ev_note_queue, %a2
2:      move.l  (%a2)+, %d0
        beq.s   3f
        move.l  16(%sp), -(%sp)         | the time
        move.l  16(%sp), -(%sp)         | the event
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   2b
3:      move.w  %d2, %sr
        movem.l (%sp), %d2/%a2
        lea     8(%sp), %sp
1:      lea     -20(%sp), %sp           | the two instructions this replaced
        movem.l %d2-%d4/%a2-%a3, (%sp)
        jmp     FW_EV_QUEUE + 8
