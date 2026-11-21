| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.17: the sites of ui.c's events (std1's UI and system events),
| OS 1.74. Each stub keeps the registers the code around its site relies on:
| a C call keeps %d2-%d7 and %a2-%a6, and where %d0, %d1, %a0 or %a1 may be
| live the stub keeps them too. core-a4mk2's ui.s is the model.
        .text
        .align  2

| ---- ev_boot_done and ev_idle: the init task's last call, 0x40100a8c `jsr
| 0x400a34d4` (keep2), the UI task's spawn; stock then spins at 0x40100a7e,
| the idle loop (prio 1, below every task but the root's). core_idle_start
| spawns the UI task, runs core_boot_done once, then with no ev_idle handler
| jumps to stock's own spin, else calls core_idle_run over and over. Never
| returns. A boot with flag 0x80 (0x40272874) skips the spawn and this site.
        .globl  core_idle_start
core_idle_start:
        jsr     0x400a34d4
        jsr     core_boot_done
        tst.l   std_idle
        bne.s   1f
        jmp     0x40100a7e
1:      jsr     core_idle_run
        bra.s   1b

| ---- ev_usb: 0x400a5356, the UI loop's message 0x26 type 1 (usb_notify,
| 0x4000387a): `mvz.w 8(%a2),%d0; cmpi.l #0x600,%d0` (10 bytes); a bne.w
| follows, on these flags
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
        cmpi.l  #0x600,%d0
        jmp     0x400a5360

| ---- ev_key_leds: 0x400a0d32 `jsr 0x40095c50` (keep2), the LED frame's
| flush in 0x400a0cec(ui, mask), after the views (0x4007917a) and stock's
| default LEDs (0x400a0768) composed it; %a2 the UI object
        .globl  core_key_leds_site
core_key_leds_site:
        move.l  %a2,-(%sp)
        jsr     core_key_leds
        addq.l  #4,%sp
        jmp     0x40095c50

| ---- ev_pattern: 0x400a4fb6, the UI loop's message 0x1e (a pattern
| change, posted by the clock interrupt and the pattern selects): `jsr
| 0x4017fa0a` (keep2)
        .globl  core_pattern_site
core_pattern_site:
        lea     -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        jsr     core_pattern_msg
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea     16(%sp),%sp
        jmp     0x4017fa0a

| ---- ev_mute: 0x400b78d8(obj, track, on, send), its entry: `link.w
| %a6,#-0x30; movem.l %d2-%d6/%a2-%a5,(%sp)` (8 bytes)
        .globl  core_mute_site
core_mute_site:
        move.l  16(%sp),-(%sp)            | send
        move.l  16(%sp),-(%sp)            | on
        move.l  16(%sp),-(%sp)            | track
        jsr     core_mute_pre
        lea     12(%sp),%sp
        tst.l   %d0
        bne.s   1f
        link.w  %fp,#-0x30
        movem.l %d2-%d6/%a2-%a5,(%sp)
        jmp     0x400b78e0
1:      rts

| ---- ev_param_set: 0x400a906c(set, id, value, track, rec, notify), the
| parameter sets' value setter (clamp, store through +0x70, parameter out
| through +0x5c, returns the stored value through +0x28), its entry: `lea
| -0x28(%sp),%sp; movem.l %d2-%d7/%a2,(%sp)` (8 bytes). The site jumps to
| ui.c's core_pset, which calls stock through core_pset_stock.
        .globl  core_pset_stock
core_pset_stock:
        lea     -0x28(%sp),%sp
        movem.l %d2-%d7/%a2,(%sp)
        jmp     0x400a9074

| ---- ev_view: the view manager's stack (a list at mgr+0x14; each node +8
| the view). Each stub reports (op, mgr, view) through core_view_report.
| push: 0x40079588 `jsr 0x401b9264` (keep2), the push's list insert (two
| stacked args); %a2 the manager, (%a3) the new view. Reported after the
| insert, before the restack (0x4007937e) and the view's on-push.
        .globl  core_stack_push_site
core_stack_push_site:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     0x401b9264
        addq.l  #8,%sp
        move.l  (%a3),-(%sp)
        move.l  %a2,-(%sp)
        pea     0                         | VIEW_PUSH
        jsr     core_view_report
        lea     12(%sp),%sp
        rts

| pop (close, 0x400795c0): 0x40079628 `movea.l 0x20(%a1),%a0; moveq #1,%d3`
| (jsr here); the on-remove's args (view, mgr) are stacked under the return
| address; %a1 the view's vtable, %a3 the manager
        .globl  core_stack_close_site
core_stack_close_site:
        move.l  4(%sp),-(%sp)             | view
        move.l  %a3,-(%sp)
        pea     2                         | VIEW_POP
        jsr     core_view_report
        lea     12(%sp),%sp
        movea.l 4(%sp),%a1
        movea.l (%a1),%a1
        movea.l 0x20(%a1),%a0
        moveq   #1,%d3
        rts

| pop (sweep, 0x40079744): 0x400797a4 `movea.l 0x20(%a0),%a0; jsr (%a0)`
| (jsr here); (view, mgr) stacked under the return address, %a4 the view,
| %a3 the manager. The on-remove runs as a tail call, returning past the site.
        .globl  core_stack_sweep_site
core_stack_sweep_site:
        move.l  %a4,-(%sp)
        move.l  %a3,-(%sp)
        pea     2
        jsr     core_view_report
        lea     12(%sp),%sp
        movea.l (%a4),%a0
        movea.l 0x20(%a0),%a0
        jmp     (%a0)

| pop (replace, the old view, 0x40079f32): 0x40079fca `movea.l
| 0x20(%a1),%a0; jsr (%a0)` (jsr here); (view, mgr) stacked under the
| return address, %a3 the manager
        .globl  core_stack_replace_old_site
core_stack_replace_old_site:
        move.l  4(%sp),-(%sp)
        move.l  %a3,-(%sp)
        pea     2
        jsr     core_view_report
        lea     12(%sp),%sp
        movea.l 4(%sp),%a1
        movea.l (%a1),%a1
        movea.l 0x20(%a1),%a0
        jmp     (%a0)

| push (replace, the new view): 0x40079ffc `movea.l (%a5),%a0; movea.l
| (%a0),%a1; movea.l 0x1c(%a1),%a1` (8 bytes, jmp here); (%a5) the new view,
| %a3 the manager
        .globl  core_stack_replace_new_site
core_stack_replace_new_site:
        lea     -8(%sp),%sp
        movem.l %d0-%d1,(%sp)
        move.l  (%a5),-(%sp)
        move.l  %a3,-(%sp)
        pea     0
        jsr     core_view_report
        lea     12(%sp),%sp
        movem.l (%sp),%d0-%d1
        lea     8(%sp),%sp
        movea.l (%a5),%a0
        movea.l (%a0),%a1
        movea.l 0x1c(%a1),%a1
        jmp     0x4007a004

| pop (the last view, 0x400796ec(mgr)): its entry `lea -0x14(%sp),%sp;
| movem.l %d2/%a2-%a3,(%sp)` (8 bytes); it calls no on-remove
        .globl  core_stack_pop_site
core_stack_pop_site:
        move.l  4(%sp),-(%sp)
        jsr     core_view_pop_last
        addq.l  #4,%sp
        lea     -0x14(%sp),%sp
        movem.l %d2/%a2-%a3,(%sp)
        jmp     0x400796f4
