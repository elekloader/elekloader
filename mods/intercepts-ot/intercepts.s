| SPDX-License-Identifier: GPL-2.0-or-later
| intercepts for the Octatrack, OS 1.40C: places in the stock firmware a mod
| intercepts, one mod at a time (intercepts 0.1). ColdFire V4 (MCF5445x);
| assemble with -mcpu=54455.
|
| An intercept gives its site to one mod. Its table holds 8-byte records,
| {claims, entry}, in (order, mod id) order, and the linker counts them
| (EVENT_n). The first record whose claims is 0, or returns nonzero for the
| call (int claims(unsigned arg), the C convention: d0-d1/a0-a1 free),
| intercepts it:
|
|   - its entry runs with every register, the condition codes and the stack
|     as they were at the site, as if the site had jumped there (a jmp event)
|     or called it (a jsr event, whose entry returns with rts past the site).
|     The entry does what the site's stock instructions did, as a detour
|     does, and goes back to stock itself;
|   - with entry 0, the stock instructions run at once.
|
| When no record intercepts it, the stock instructions run here, then stock
| goes on. A mod never patches these sites: it adds a record to the event's
| table.
|
| The events, their sites, their kinds (jmp or jsr) and their claims'
| arguments are the ones Modwerk's Octatrack base gives the same names, as
| "take" events (repeat98/modwerk, sdk/machines/octatrack/EVENTS.md), so a
| record means the same thing on both. This code is elekloader's own.
|
| Every site runs in a firmware task (the UI, MIDI or project file), after
| core's .boot has put .run in place.

        .section .run, "ax"

        .set    TRACK, 0x100b14cc       | the current track (a byte)

| intercept_find: a0 = the table, d0 = its count, d1 = the claims argument
| -> a0 = the entry of the record that intercepts the call, or 0 when none
| does or its entry is 0 (stock runs). Keeps d2-d3/a2; d0-d1/a0-a1 are free,
| as in a claims call.
intercept_find:
        lea     -12(%sp), %sp
        movem.l %d2-%d3/%a2, (%sp)
        movea.l %a0, %a2                | the next record
        move.l  %d0, %d3                | records left
        move.l  %d1, %d2                | the argument
1:      tst.l   %d3
        beq.s   3f                      | no record intercepts it
        subq.l  #1, %d3
        move.l  (%a2), %d0              | its claims
        beq.s   2f                      | 0: it intercepts every call
        move.l  %d2, -(%sp)
        movea.l %d0, %a0
        jsr     (%a0)
        addq.l  #4, %sp
        tst.l   %d0
        bne.s   2f
        addq.l  #8, %a2
        bra.s   1b
2:      movea.l 4(%a2), %a0             | its entry (0: stock)
        bra.s   4f
3:      suba.l  %a0, %a0
4:      movem.l (%sp), %d2-%d3/%a2
        lea     12(%sp), %sp
        rts

| INTERCEPT event, arg: the stub the event's site jumps to (or calls). arg is
| the claims argument: none (0), d2, or track (the byte at TRACK). When a
| record intercepts the call it leaves through the slot it reserved, into the
| record's entry; otherwise it falls through, everything as at the site, into
| the stock instructions written after it.
        .macro  INTERCEPT event, arg
        .globl  intercept_\event
intercept_\event:
        subq.l  #4, %sp                 | the slot it leaves through, into the entry
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a1, (%sp)
        move.w  %ccr, %d0
        move.w  %d0, 16(%sp)
        .ifc    \arg, d2
        move.l  %d2, %d1
        .else
        moveq   #0, %d1
        .ifc    \arg, track
        move.b  TRACK, %d1
        .endif
        .endif
        lea     \event, %a0
        move.l  #\event\()_n, %d0
        bsr     intercept_find
        move.l  %a0, 20(%sp)
        move.l  %a0, %d0
        beq.s   1f
        move.w  16(%sp), %d0            | intercepted: everything as at the site
        move.w  %d0, %ccr
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     20(%sp), %sp
        rts                             | into the record's entry
1:      move.w  16(%sp), %d0            | stock: everything as at the site
        move.w  %d0, %ccr
        movem.l (%sp), %d0-%d1/%a0-%a1
        lea     24(%sp), %sp
        .endm

| Each event below: its site (and what the site held), the stub, then the
| stock instructions the site displaced and the way back to stock. A
| PC-relative branch the site held becomes the same branch to its absolute
| target; the other instructions are the stock words, which do not depend on
| where they run.

| ---- MIDI notes on a listening track: d2 is the track -------------------------
| 0x4000e746 jmp (moveq #29,%d1 ; move.l %d1,(0,%a5,%d2.l*4)): a chromatic
| note-on, before stock posts its trigger and PTCH.
        INTERCEPT    ev_midi_note_on, d2
        .word   0x721d                  | moveq   #29, %d1
        .word   0x2b81, 0x2c00          | move.l  %d1, (0,%a5,%d2.l*4)
        jmp     0x4000e74c

| 0x4000dfd4 jmp (mvs.b (%a0),%d1 ; mvs.b (%a3),%d0 ; cmp.l %d1,%d0 ;
| bne.s 0x4000dff8): a chromatic note-off, before stock compares it with the
| track's held note.
        INTERCEPT    ev_midi_note_off, d2
        .word   0x7310                  | mvs.b   (%a0), %d1
        .word   0x7113                  | mvs.b   (%a3), %d0
        .word   0xb081                  | cmp.l   %d1, %d0
        beq.s   2f
        jmp     0x4000dff8              | the bne.s
2:      jmp     0x4000dfdc

| ---- the CHROMATIC keyboard ---------------------------------------------------
| 0x40045918 jmp (moveq #1,%d2 ; eor.l %d2,0x460d16fc): FUNC + LEFT or RIGHT,
| before stock toggles its octave word. The argument is the current track.
        INTERCEPT    ev_octave_key, track
        .word   0x7401                  | moveq   #1, %d2
        .word   0xb5b9, 0x460d, 0x16fc  | eor.l   %d2, 0x460d16fc
        jmp     0x40045920

| 0x40044968 jmp (moveq #1,%d1 ; cmp.l 0x460d16fc,%d1): the keyboard drawer
| reads stock's octave word.
        INTERCEPT    ev_keyboard_draw, none
        .word   0x7201                  | moveq   #1, %d1
        .word   0xb2b9, 0x460d, 0x16fc  | cmp.l   0x460d16fc, %d1
        jmp     0x40044970

| 0x4004fbde jmp (mvz.b (0,%a0,%d2.l),%d1 ; move.l %a2,%d0): a key is released.
        INTERCEPT    ev_chromatic_release, none
        .word   0x73b0, 0x2800          | mvz.b   (0,%a0,%d2.l), %d1
        .word   0x200a                  | move.l  %a2, %d0
        jmp     0x4004fbe4

| 0x4004fbfe jmp (tst.l 0x46c7dd26 ; bne.s 0x4004fc44): a key is pressed while
| another is held.
        INTERCEPT    ev_chromatic_held, none
        .word   0x4ab9, 0x46c7, 0xdd26  | tst.l   0x46c7dd26
        beq.s   2f
        jmp     0x4004fc44              | the bne.s
2:      jmp     0x4004fc06

| 0x4004fc58 jsr (lea (4,%a2,%a2.l*4),%a2 ; mvz.b 0x100b14cf,%d0): a key's
| index becomes a raw pitch.
        INTERCEPT    ev_chromatic_pitch, none
        .word   0x45f2, 0xac04          | lea     (4,%a2,%a2.l*4), %a2
        .word   0x71b9, 0x100b, 0x14cf  | mvz.b   0x100b14cf, %d0
        rts

| 0x4004fc94 jmp (tst.l 0x46c7dd26 ; beq.s 0x4004fcb2): a key trigs the voice.
        INTERCEPT    ev_chromatic_trig, none
        .word   0x4ab9, 0x46c7, 0xdd26  | tst.l   0x46c7dd26
        bne.s   2f
        jmp     0x4004fcb2              | the beq.s
2:      jmp     0x4004fc9c

| 0x4004fce0 jmp (tst.l 0x46c7dd26 ; beq.s 0x4004fcf8): a key goes to the live
| recorder.
        INTERCEPT    ev_chromatic_record, none
        .word   0x4ab9, 0x46c7, 0xdd26  | tst.l   0x46c7dd26
        bne.s   2f
        jmp     0x4004fcf8              | the beq.s
2:      jmp     0x4004fce8

| 0x4004fd06 jmp (addq.l #8,%sp ; tst.l %d0 ; blt.s 0x4004fd3e): a key was
| recorded.
        INTERCEPT    ev_chromatic_recorded, none
        .word   0x508f                  | addq.l  #8, %sp
        .word   0x4a80                  | tst.l   %d0
        bge.s   2f
        jmp     0x4004fd3e              | the blt.s
2:      jmp     0x4004fd0c

| 0x4004fd20 jmp (lea 20(%sp),%sp ; bra.s 0x4004fd3e): a recorded key got its
| PTCH lock.
        INTERCEPT    ev_chromatic_locked, none
        .word   0x4fef, 0x0014          | lea     20(%sp), %sp
        jmp     0x4004fd3e              | the bra.s

| 0x40050254 jmp (move.l 0x460d16fc,%d0 ; move.l %d0,%d1): a key's index is
| computed from stock's octave.
        INTERCEPT    ev_chromatic_index, none
        .word   0x2039, 0x460d, 0x16fc  | move.l  0x460d16fc, %d0
        .word   0x2200                  | move.l  %d0, %d1
        jmp     0x4005025c

| ---- locks, knobs, settings ---------------------------------------------------
| 0x40050e60 jsr (move.b %d4,(0x59,%a1,%a0.l) ; mvz.b 0x100b14cc,%d1): the
| p-lock editor stores a lock byte.
        INTERCEPT    ev_plock_store, none
        .word   0x1384, 0x8859          | move.b  %d4, (0x59,%a1,%a0.l)
        .word   0x73b9, 0x100b, 0x14cc  | mvz.b   0x100b14cc, %d1
        rts

| 0x40055170 jsr (move.b %d2,(%a2) ; move.b %d2,(%a5) ; move.w %a6,%d1): a
| knob turn stores a Part byte.
        INTERCEPT    ev_knob_store, none
        .word   0x1482                  | move.b  %d2, (%a2)
        .word   0x1a82                  | move.b  %d2, (%a5)
        .word   0x320e                  | move.w  %a6, %d1
        rts

| 0x40010212 jsr (tst.b 0x100b14ae): stock sanitises the battery-RAM
| settings block; its next instruction branches on these flags.
        INTERCEPT    ev_settings_sanitise, none
        .word   0x4a39, 0x100b, 0x14ae  | tst.b   0x100b14ae
        rts

| 0x40025ac2 jsr (clr.l 0x100b14d4): stock resets a project's settings to
| their defaults.
        INTERCEPT    ev_project_defaults, none
        .word   0x42b9, 0x100b, 0x14d4  | clr.l   0x100b14d4
        rts

| ---- the project file ---------------------------------------------------------
| 0x400866cc jmp (move.l 1472(%sp),%d6 ; move.l 1476(%sp),%d0): a project
| load starts.
        INTERCEPT    ev_project_load, none
        .word   0x2c2f, 0x05c0          | move.l  1472(%sp), %d6
        .word   0x202f, 0x05c4          | move.l  1476(%sp), %d0
        jmp     0x400866d4

| 0x400867a2 jmp (move.b 1167(%sp),%d1 ; mvs.b %d1,%d0 ; moveq #35,%d5): the
| project loader meets a line.
        INTERCEPT    ev_project_line, none
        .word   0x122f, 0x048f          | move.b  1167(%sp), %d1
        .word   0x7101                  | mvs.b   %d1, %d0
        .word   0x7a23                  | moveq   #35, %d5
        jmp     0x400867aa

| 0x400888aa jmp (mvs.b 0x8000004f,%d1 ; move.l %d1,-(%sp)): the project
| writer writes the settings lines.
        INTERCEPT    ev_project_write, none
        .word   0x7339, 0x8000, 0x004f  | mvs.b   0x8000004f, %d1
        .word   0x2f01                  | move.l  %d1, -(%sp)
        jmp     0x400888b2
