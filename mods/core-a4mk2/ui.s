| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.7: the sites of ui.c's events, OS 1.56. Each stub keeps the
| registers the code around its site relies on: a C call keeps %d2-%d7 and
| %a2-%a6, and where %d1, %a0 or %a1 may be live the stub keeps them too.
        .text
        .align  2

| ---- ev_boot_done: 0x4009995c, `lea 0x400a51be,%a4` (6 bytes), in the UI
| task just before its loop's head (0x40099962), once
        .globl  core_boot_done_site
core_boot_done_site:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        jsr     core_boot_done
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        lea     0x400a51be,%a4
        rts

| ---- ev_idle: main's 0x40106302 `jsr 0x4009900c` (keep2): 0x4009900c makes
| and starts the UI task, then main spins at 0x401062fa, the lowest priority
| task. This never returns either. With no handler it jumps to stock's own
| spin, the self-branch at 0x401062fa, so main idles at stock's PC (analog-emu
| feeds USB MIDI and skips to the next interrupt only at the image's
| self-branches).
        .globl  core_idle_start
core_idle_start:
        jsr     0x4009900c
        tst.l   std_idle
        bne.s   1f
        jmp     0x401062fa
1:      jsr     core_idle_run
        bra.s   1b

| ---- ev_crash: 0x400638da `jsr 0x4000203a` (keep2), the install of stock's
| crash printer (the pushed argument): core_crash_install(printer) in C.

| ---- ev_usb: 0x4009a248, the UI loop's USB message: `mvz.w 8(%a2),%d0;
| cmpi.l #0x700,%d0` (10 bytes); a bne.w follows, on these flags
        .globl  core_usb_site
core_usb_site:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  %a2,-(%sp)
        jsr     core_usb_msg
        addq.l  #4,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        mvz.w   8(%a2),%d0
        cmpi.l  #0x700,%d0
        jmp     0x4009a252

| ---- ev_key_leds: 0x40096b42 `jsr 0x4007594c` (keep2), the LED frame's send
| after the views composed it; %a2 is the LedManager
        .globl  core_key_leds_site
core_key_leds_site:
        move.l  %a2,-(%sp)
        jsr     core_key_leds
        addq.l  #4,%sp
        jmp     0x4007594c

| ---- ev_pattern: 0x40099c84, the UI loop's case 9 (a pattern boundary):
| `jsr (%a3); move.l %d0,-(%sp); jsr 0x400a7966` (10 bytes); the pushed
| argument stays for stock's own pop
        .globl  core_pattern_site
core_pattern_site:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        jsr     core_pattern_msg
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        jsr     (%a3)
        move.l  %d0,-(%sp)
        jsr     0x400a7966
        jmp     0x40099c8e

| ---- ev_mute: 0x400aeb8e(settings, track, muted, send), its entry: `link.w
| %fp,#-48; movem.l %d2-%d6/%a2-%a5,(%sp)` (8 bytes)
        .globl  core_mute_site
core_mute_site:
        move.l  12(%sp),-(%sp)            | muted
        move.l  12(%sp),-(%sp)            | track
        jsr     core_mute_pre
        addq.l  #8,%sp
        tst.l   %d0
        bne.s   1f
        link.w  %fp,#-48
        movem.l %d2-%d6/%a2-%a5,(%sp)
        jmp     0x400aeb96
1:      moveq   #0,%d0
        rts

| ---- ev_knob_turn: 0x40025b78 in the page view's turn (0x4002598c), past
| the step and the lockout: `move.l 0x74(%a2),-(%sp); jsr 0x400adbd2`
| (10 bytes). %a2 the view, %d2 the id, %d3 the encoder, %d5 the delta.
| Taken: 0x40025d32, the view's "handled" return.
        .globl  core_knob_site
core_knob_site:
        move.l  %d3,-(%sp)
        move.l  %d5,-(%sp)
        move.l  %d2,-(%sp)
        move.l  %a2,-(%sp)
        jsr     core_knob_turn
        lea     16(%sp),%sp
        tst.l   %d0
        bne.s   1f
        move.l  0x74(%a2),-(%sp)
        jsr     0x400adbd2
        jmp     0x40025b82
1:      jmp     0x40025d32

| ---- ev_clip: 23 routines' entries. Each stub asks core_clip_pre(code, a1,
| a2); unless a handler took it, it calls the routine through a copy of its
| first instructions with four of its arguments, then core_clip_post(code, a1,
| a2), and returns the routine's %d0. code = kind | op << 8 | flags << 16
| (ui.c; analog-four-mk2/ui.h). Every routine starts with a link.w, so the
| copied instructions do not depend on where they run.
        .macro  CLIP name, addr, len, code, w0, w1, w2, w3, w4=0
        .globl  \name
\name:
        move.l  8(%sp),-(%sp)             | a2
        move.l  8(%sp),-(%sp)             | a1
        move.l  #\code,-(%sp)
        jsr     core_clip_pre
        tst.l   %d0
        bne.s   9f
        move.l  28(%sp),-(%sp)            | a4
        move.l  28(%sp),-(%sp)            | a3
        move.l  28(%sp),-(%sp)            | a2
        move.l  28(%sp),-(%sp)            | a1
        jsr     \name\()_stock
        lea     16(%sp),%sp
        move.l  %d0,-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     core_clip_post
        lea     12(%sp),%sp
        move.l  (%sp)+,%d0
        lea     12(%sp),%sp
        rts
9:      lea     12(%sp),%sp
        lsr.l   #1,%d0                    | taken: 1 -> 0, 3 -> 1
        rts
\name\()_stock:
        .short  \w0, \w1, \w2, \w3
        .if     \len == 10
        .short  \w4
        .endif
        jmp     \addr + \len
        .endm

| code: kind | op << 8 | flags << 16; kinds 0 pattern, 1 track, 2 kit,
| 3 sound, 4 song, 5 track sound, 6 page, 7 trigs, 8 chain; ops 0 copy,
| 1 paste, 2 clear; flags 1 a confirm callback (YES in its second argument),
| 2 the second argument is the track, 4 the sequencer view's virtual, acting
| only in grid record, 8 a view's virtual that always returns 1 (handled)
        CLIP    core_clip_track_copy,   0x40027e14, 8,  0x00040001, 0x4e56, 0xffe4, 0x48d7, 0x040c
        CLIP    core_clip_track_paste,  0x40027e94, 8,  0x00040101, 0x4e56, 0xffe4, 0x48d7, 0x0c04
        CLIP    core_clip_track_clear,  0x40027f78, 8,  0x00040201, 0x4e56, 0xffe4, 0x48d7, 0x0c04
        CLIP    core_clip_kit_copy,     0x4002e828, 8,  0x00000002, 0x4e56, 0xffe0, 0x48d7, 0x1c04
        CLIP    core_clip_kit_paste,    0x4002e906, 8,  0x00010102, 0x4e56, 0xffe4, 0x206e, 0x0008
        CLIP    core_clip_kit_clear,    0x4002e9b8, 8,  0x00010202, 0x4e56, 0xffe4, 0x206e, 0x0008
        CLIP    core_clip_kitmenu_clear,0x400d254c, 8,  0x00010202, 0x4e56, 0xffe4, 0x206e, 0x0008
        CLIP    core_clip_sound_copy,   0x4004f3fe, 8,  0x00000003, 0x4e56, 0xffe0, 0x48d7, 0x041c
        CLIP    core_clip_sound_paste,  0x4004f4f2, 8,  0x00010103, 0x4e56, 0xfff0, 0x206e, 0x0008
        CLIP    core_clip_sound_clear,  0x4004f598, 8,  0x00010203, 0x4e56, 0xfff0, 0x206e, 0x0008
        CLIP    core_clip_song_copy,    0x40042fa6, 8,  0x00000004, 0x4e56, 0xffe0, 0x48d7, 0x1c04
        CLIP    core_clip_song_paste,   0x40043084, 8,  0x00010104, 0x4e56, 0xfff0, 0x206e, 0x0008
        CLIP    core_clip_song_clear,   0x40043114, 8,  0x00010204, 0x4e56, 0xfff0, 0x206e, 0x0008
        CLIP    core_clip_songmenu_clear,0x40046648, 8, 0x00010204, 0x4e56, 0xfff0, 0x206e, 0x0008
        CLIP    core_clip_tsound_copy,  0x400c86f8, 8,  0x00020005, 0x4e56, 0xffd0, 0x7003, 0x2f0b
        CLIP    core_clip_tsound_clear, 0x400c8818, 10, 0x00020205, 0x4e56, 0xffa8, 0x7003, 0x48d7, 0x1c1c
        CLIP    core_clip_tsound_paste, 0x400c89ec, 10, 0x00020105, 0x4e56, 0xffe4, 0x7003, 0x48d7, 0x1c00
        CLIP    core_clip_pattern_clear, 0x4002a440, 8, 0x00000200, 0x4e56, 0xffb8, 0x206e, 0x0008
        CLIP    core_clip_pattern_paste, 0x4002a55e, 8, 0x00000100, 0x4e56, 0xffd8, 0x206e, 0x0008

| ---- ev_view: the view stack. Each stub reports (op, ctrl, view) through
| core_view_report, keeping the scratch registers the code around it holds.
| push: 0x40062a60 `jsr 0x40180992` (keep2), the funnel's list insert (two
| stacked args); %a2 the controller, (%a3) the new view. Reported after the
| insert, before the relayout and the view's on-push.
        .globl  core_view_push_site
core_view_push_site:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     0x40180992
        addq.l  #8,%sp
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  (%a3),-(%sp)
        move.l  %a2,-(%sp)
        clr.l   -(%sp)                    | VIEW_PUSH
        jsr     core_view_report
        lea     12(%sp),%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        rts

| pop (close sweep): 0x40062c7c `movea.l 0x20(%a0),%a0; jsr (%a0)` (jsr
| here; 6 bytes): %a4 the view, %a3 the controller, %a0 its vtable; its
| on-remove's args (view, ctrl) are stacked under the return address
| 0x40062c82, so the stub ends in a jmp to it
        .globl  core_view_sweep_site
core_view_sweep_site:
        move.l  %a0,-(%sp)
        move.l  %a4,-(%sp)
        move.l  %a3,-(%sp)
        moveq   #2,%d0                    | VIEW_POP
        move.l  %d0,-(%sp)
        jsr     core_view_report
        lea     12(%sp),%sp
        movea.l (%sp)+,%a0
        movea.l 0x20(%a0),%a0
        jmp     (%a0)

| pop (remove one): 0x40062b00 `movea.l 0x20(%a1),%a0; moveq #1,%d3` (jsr
| here); %a0 the view, %a1 its vtable, %a3 the controller; the on-remove's
| jsr (%a0) follows at 0x40062b06
        .globl  core_view_remove_site
core_view_remove_site:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  %a0,-(%sp)
        move.l  %a3,-(%sp)
        moveq   #2,%d0                    | VIEW_POP
        move.l  %d0,-(%sp)
        jsr     core_view_report
        lea     12(%sp),%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        movea.l 0x20(%a1),%a0
        moveq   #1,%d3
        rts

| pop (replace, the old view): 0x400633c8 `movea.l 0x20(%a1),%a0; jsr
| (%a0)` (jsr here); %a0 the old view, %a1 its vtable, %a3 the controller;
| ends in a jmp to its on-remove as the sweep's
        .globl  core_view_replace_old_site
core_view_replace_old_site:
        move.l  %a1,-(%sp)
        move.l  %a0,-(%sp)
        move.l  %a3,-(%sp)
        moveq   #2,%d0                    | VIEW_POP
        move.l  %d0,-(%sp)
        jsr     core_view_report
        lea     12(%sp),%sp
        movea.l (%sp)+,%a1
        movea.l 0x20(%a1),%a0
        jmp     (%a0)

| push (replace, the new view): 0x400633fc `movea.l (%a0),%a1; movea.l
| 0x1c(%a1),%a1` (jsr here); %a0 the new view, %a3 the controller; its
| on-push follows at 0x40063402
        .globl  core_view_replace_new_site
core_view_replace_new_site:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l  %a0,-(%sp)
        move.l  %a3,-(%sp)
        clr.l   -(%sp)                    | VIEW_PUSH
        jsr     core_view_report
        lea     12(%sp),%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        movea.l (%a0),%a1
        movea.l 0x1c(%a1),%a1
        rts

| ---- ev_pattern pre 1: the requests, at each routine's entry (jmp). The
| C side (core_pattern_*) takes the routine's first argument; nonzero takes
| the request and the routine returns at once. The scratch registers are kept,
| as some callers reach these through a register.
        .macro  PREQ name, fn
        .globl  \name
\name:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  16(%sp),-(%sp)
        jsr     \fn
        addq.l  #4,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        tst.l   %d0
        bne.s   9f
        .endm

| the pattern view's select 0x40029b9e(view, pattern): `lea -28(%sp),%sp;
| moveq #127,%d0` (6 bytes). Reported before it writes the row; unless taken
| the stub runs it (through a copy of those two instructions) with its two
| arguments, then lets the routines below report again.
        .globl  core_pattern_view_site
core_pattern_view_site:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  20(%sp),-(%sp)            | pattern
        move.l  20(%sp),-(%sp)            | view
        jsr     core_pattern_view
        addq.l  #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        tst.l   %d0
        bne.s   9f
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     core_pattern_view_stock
        addq.l  #8,%sp
        jsr     core_pattern_view_end
9:      rts
core_pattern_view_stock:
        lea     -28(%sp),%sp
        moveq   #127,%d0
        jmp     0x40029ba4

| queue 0x4007c6e4(row): `lea 4(%sp),%a0; move.l (%a0),0x41281756; rts`
| (12 bytes, the whole routine)
        PREQ    core_pattern_queue_site, core_pattern_queue
        lea     4(%sp),%a0
        move.l  (%a0),0x41281756
9:      rts

| jump now 0x4007c5fc(row, offset): `lea -16(%sp),%sp; movem.l
| %d2-%d4/%a2,(%sp)` (8 bytes)
        PREQ    core_pattern_jump_site, core_pattern_jump
        lea     -16(%sp),%sp
        movem.l %d2-%d4/%a2,(%sp)
        jmp     0x4007c604
9:      rts

| set 0x4007c186(pattern, flag): `move.l %d2,-(%sp); moveq #1,%d2; move.l
| 8(%sp),%d0` (8 bytes)
        PREQ    core_pattern_set_site, core_pattern_set
        move.l  %d2,-(%sp)
        moveq   #1,%d2
        move.l  8(%sp),%d0
        jmp     0x4007c18e
9:      rts

| restart 0x40079eaa(pattern): `moveq #1,%d0; cmp.l 0x80006a88,%d0` (8
| bytes); a bne.b on its flags follows
        PREQ    core_pattern_start_site, core_pattern_start
        moveq   #1,%d0
        cmp.l   0x80006a88,%d0
        jmp     0x40079eb2
9:      rts

| song position 0x4007c3bc(position): `link.w %fp,#-24; moveq #1,%d0` (6
| bytes)
        PREQ    core_pattern_spp_site, core_pattern_spp
        link.w  %fp,#-24
        moveq   #1,%d0
        jmp     0x4007c3c2
9:      rts

| ---- ev_knob_turn, the LEVEL encoder: 0x400bedb2 `jsr 0x400589ee` (keep2),
| MainScreenView's accelerator step (five stacked args), before it tests the
| event is the LEVEL encoder. %a2 the view, %d2 the event, %d3 the active
| track, %d4.b the external/frozen flag. Returns the delta stock goes on with.
| Taken (core_level_turn returns 0 for a nonzero step): straight to the
| routine's common exit 0x400bef76 (`moveq #1,%d0`, then the frame's movem and
| unlk, so the stack depth there doesn't matter), past the set and its popup:
| the external/frozen path (0x400bee36) has no delta test and would set Ext.
| Vol. by 0 and show its popup.
        .globl  core_level_site
core_level_site:
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        move.l  20(%sp),-(%sp)
        jsr     0x400589ee
        lea     20(%sp),%sp
        tst.l   %d0
        beq.s   1f
        move.l  %d0,-(%sp)
        move.l  %d4,-(%sp)
        move.l  %d3,-(%sp)
        move.l  %d2,-(%sp)
        move.l  %a2,-(%sp)
        jsr     core_level_turn
        lea     20(%sp),%sp
        tst.l   %d0
        bne.s   1f
        addq.l  #4,%sp
        jmp     0x400bef76
1:      rts

| ---- ev_knob_turn, the mixer page: 0x400c543a `jsr 0x4009dc3a` (keep2),
| its level set (settings, track, value, send); %a2 the view. Not taken: the
| set, returning to 0x400c5440 as stock. Taken: past the set and the "Tn
| LEVEL" popup to 0x400c547a (`lea 0x24(%sp),%sp`), where the caller's stack
| is the same 9 longs as after the popup's call.
        .globl  core_mixer_site
core_mixer_site:
        lea     -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l  24(%sp),-(%sp)
        move.l  24(%sp),-(%sp)
        move.l  24(%sp),-(%sp)
        move.l  %a2,-(%sp)
        jsr     core_mixer_level
        lea     16(%sp),%sp
        movem.l (%sp),%d1/%a0-%a1
        lea     12(%sp),%sp
        tst.l   %d0
        bne.s   1f
        jmp     0x4009dc3a
1:      addq.l  #4,%sp
        jmp     0x400c547a

| ---- ev_analog_frame: 0x40103da6 `jsr 0x400fc614` (keep2), the CV builder
| at the end of the audio interrupt's block, just before the frame (0x80003d6c)
| is packed (0x40103db4)
        .globl  core_analog_frame_site
core_analog_frame_site:
        jsr     0x400fc614
        tst.l   ev_analog_frame
        beq.s   1f
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        jsr     core_analog_frame
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
1:      rts

| the pattern's copy 0x400a2aa4(pattern): the pattern view's FUNC+REC
| (0x400c0920) and the pattern select view's REC with one pattern selected
| (0x4002b9b4's lea, called at 0x4002b9d8) are its only callers
        CLIP    core_clip_pattern_copy, 0x400a2aa4, 8,  0x00000000, 0x4e56, 0xffdc, 0x48d7, 0x1c0c
| the sequencer view's keys (0x4002805c): PAGE held + REC copies the active
| track's page, 0x400b933c(pattern track) (called at 0x40028c88); trigs held
| + REC copies them, 0x400b8e66(pattern track, held set) (at 0x40028d70)
        CLIP    core_clip_page_copy,    0x400b933c, 8,  0x00000006, 0x4e56, 0xffd8, 0x48d7, 0x3c0c
        CLIP    core_clip_trigs_copy,   0x400b8e66, 8,  0x00000007, 0x4e56, 0xff84, 0x48d7, 0x3cfc
| the SONG editor's copy virtual (+0x34), FUNC+REC: the row under the cursor
        CLIP    core_clip_chain_copy,   0x40044844, 8,  0x00080008, 0x4e56, 0xfff0, 0x2f0a, 0x2f02

| ---- ev_clip, the PROJECT list's copy. Its handler 0x4003c380(pm, key)
| (link.w %fp,#-44; movem.l %d2/%a2-%a3,(%sp): 8 bytes) queues a "Project
| Copy" job and always returns 1 (handled): pre here, with the slot under the
| cursor; a take returns 1 and queues nothing. The job's done callback
| 0x4003ea96(closure, ok) runs in the UI task once the copy ended (link.w
| %fp,#-36; movem.l %d2-%d3/%a2,(%sp): 8 bytes): post there, after it.
        .globl  core_clip_project_copy
core_clip_project_copy:
        move.l  4(%sp),-(%sp)             | pm
        jsr     core_clip_project_pre
        addq.l  #4,%sp
        tst.l   %d0
        bne.s   1f
        .short  0x4e56, 0xffd4, 0x48d7, 0x0c04
        jmp     0x4003c388
1:      moveq   #1,%d0
        rts

        .globl  core_clip_project_done
core_clip_project_done:
        move.l  8(%sp),-(%sp)             | ok
        move.l  8(%sp),-(%sp)             | closure
        jsr     core_clip_project_done_stock
        jsr     core_clip_project_post
        addq.l  #8,%sp
        rts
core_clip_project_done_stock:
        .short  0x4e56, 0xffdc, 0x48d7, 0x040c
        jmp     0x4003ea9e
