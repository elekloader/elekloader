| SPDX-License-Identifier: GPL-2.0-or-later
| core 4.5 (Digitakt mk1): the system's moments, for a mod that links more
| mods on the unit (TELOS). ColdFire V4; assemble with -mcpu=54455. Beside
| core.s. From the core 5.0 prototype (system.s, events.s), with the card's
| own routines, the OS's static tasks and a copy of the OS image as it was
| before it ran.
|
|   CARD_STATUS   the init task's card status word (0: the +Drive is up)
|   CRASH_SET     sets the default exception handler's RAM hook
|   FW_RESTART    the stock restart: interrupts off, drain the links, restart
|   SYSEX_IN      the system switch's SysEx case (the MIDI task)
|   CORE_STOCK_COPY, CORE_IMAGE_LEN, CORE_TASK_SHIFT: below
|
| Addresses are 1.53's; each OS's port in mod.json gives its own.

| ---- ev_card_ready: the +Drive is up, nothing else is ------------------------
| At 0x400691d2 the init task (0x40068eb6) stores the card bring-up's status,
| `move.l d0,CARD_STATUS`: every exit of the bring-up lands there. The
| project, the UI task and the views do not exist yet, and no sample has
| loaded. core_card_ready makes the store, then calls each handler of
| ev_card_ready, f(int status): 0, the card is up; nonzero, it failed. The
| init task, at the RTOS's priority 1, before the project loads: a handler
| must not take the firmware's heap and should be quick. d0-d1/a0-a1 come
| back as they went.
        .section .run, "ax"
        .globl  core_card_ready
core_card_ready:
        move.l  %d0, CARD_STATUS        | the store this replaced
        tst.l   ev_card_ready
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_card_ready, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  (%sp), -(%sp)           | the status
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts

| ---- ev_fs_ready: the +Drive's file system is mounted ----------------------
| At 0x40069264 the init task's paths join after the card bring-up: ekFS
| mounted (0x4006925c), formatted on a format boot, or neither when the card
| failed. Still before the samples, the project and the UI task. Handlers:
| f(int mounted): nonzero when ekFS is up (FS_MOUNTED), so fw_fs_* work. The
| site was `moveq #64,d0; and.l BOOTFLAGS,d0`, a test the next instruction
| branches on: core_fs_ready ends with it, so the flags it leaves are stock's.
        .globl  core_fs_ready
core_fs_ready:
        tst.l   ev_fs_ready
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d1/%a0-%a2, (%sp)
        lea     ev_fs_ready, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        move.l  FS_MOUNTED, -(%sp)
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movem.l (%sp), %d1/%a0-%a2
        lea     16(%sp), %sp
9:      moveq   #64, %d0
        and.l   BOOTFLAGS, %d0
        rts

| ---- project slots: written, saved, loaded (from the core 5.0 draft) --------
| ev_project_slot_write: f(void *fs, int slot, int ctx). A project slot is
| about to be written (0-127; 128 the working copy), at the slot-write
| funnel's entry (SLOT_CARD_WRITE, 0x400923b4; 1.54 0x400923f8): a SAVE,
| and MANAGE PROJECTS > CLEAR, which writes a new project there. The UI side.
        .globl  core_slot_write
core_slot_write:
        tst.l   ev_project_slot_write
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_slot_write, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        .rept   3
        move.l  (20+12)(%sp), -(%sp)    | ctx, slot, fs
        .endr
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      lea     -32(%sp), %sp           | the two instructions this replaced
        movem.l %d2-%d4, (%sp)
        jmp     SLOT_CARD_WRITE + 8

| ev_project_saved: f(void *block, int slot, int ok). A SAVE's slot write
| returned (0x40016a02, the save's call of the funnel): ok nonzero, written.
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

| ev_project_loaded: f(int slot, int err). A project load finished, at the
| load job's completion (LOAD_DONE, 0x4004f054; err its second argument, 0:
| "LOAD OK"): the working project is the loaded one, and slot is the
| project's slot (CUR_SLOT, the setting the load wrote). The UI side.
        .globl  core_project_loaded
core_project_loaded:
        tst.l   ev_project_loaded
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_loaded, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  (20+8)(%sp), -(%sp)     | err
        move.b  CUR_SLOT, %d0
        extb.l  %d0
        move.l  %d0, -(%sp)             | slot
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      link.w  %fp, #-32
        movea.l 8(%fp), %a0
        jmp     LOAD_DONE + 8

| ---- ev_crash: an exception the firmware does not handle --------------------
| Every vector starts at the default handler (0x40001536), which masks the
| interrupts and calls a hook in RAM with the exception frame. At boot the
| init task installs the stock hook, a crash printer ("EXCEPTION") that ends
| in a halt: at 0x400cbf0a (0x400cc132 in 1.54), CRASH_SET(printer) with
| the printer pushed. Core installs core_crash instead and keeps the
| printer. core_crash calls each handler of ev_crash, f(void *frame).
| Interrupts are masked and the RTOS may be anywhere: no blocking, no heap.
| A handler that wants the unit back calls core_restart_now (it does not
| return). After the handlers, the stock printer, and the unit stops.
        .globl  core_crash_install
core_crash_install:
        move.l  4(%sp), %d0
        move.l  %d0, core_crash_stock   | the stock printer
        pea     core_crash
        jsr     CRASH_SET
        addq.l  #4, %sp
        rts

        .globl  core_crash
core_crash:
        move.l  %a2, -(%sp)
        lea     ev_crash, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  8(%sp), -(%sp)          | the frame
        jsr     (%a0)
        addq.l  #4, %sp
        bra.s   1b
2:      movea.l (%sp)+, %a2
        movea.l core_crash_stock, %a0
        jmp     (%a0)                   | the printer, with the frame at 4(sp)

| ---- core_restart: reboot ---------------------------------------------------
| void core_restart(void): the firmware's own restart (FW_RESTART,
| 0x40005864, what its reboot command runs): interrupts off, the panel link
| and the UART drained, then the restart. From a task. It does not return.
| void core_restart_now(void): the reset controller's soft reset at once,
| for an exception handler or code that runs with interrupts masked.
        .globl  core_restart
core_restart:
        jmp     FW_RESTART

        .globl  core_restart_now
core_restart_now:
        move.w  #0x2700, %sr
        clr.l   0x48000000
        .rept   27
        nop
        .endr
        moveq   #-128, %d0
        move.b  %d0, 0xEC090000
3:      bra.s   3b

| ---- ev_sysex: each whole SysEx message -------------------------------------
| ev_sysex: f(const uint8_t *msg, int len, int source), may take. Each whole
| SysEx message, F0 to F7, at the system switch's SysEx case (SYSEX_IN
| 0x400d52c4), F0 7E included (the stock answers device inquiries after it).
| A handler that returns nonzero takes it: the stock does not see it.
| digihealth's site (0x400d52f0) is further in: both coexist. The MIDI
| task: a handler that blocks holds every MIDI input. Reply with
| fw_sysex_send.
        .globl  core_sysex
core_sysex:
        tst.l   ev_sysex
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_sysex, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        .rept   3
        move.l  (20+12)(%sp), -(%sp)    | source, len, msg
        .endr
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0-%a2  | taken: the routine returns at once
        lea     20(%sp), %sp
        rts
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      lea     -16(%sp), %sp           | the two instructions this replaced
        movem.l %d2-%d3/%a2-%a3, (%sp)
        jmp     SYSEX_IN + 8

| ---- core_stock_info, core_tasks: for a loader that writes the OS's code ------
| core_stock_info: {where, bytes} of the OS image's copy core_stock_copy made
| before the OS ran (0x40000400 on): a loader on the unit (TELOS) reads stock
| bytes from it, where the OS has since changed its data. The copy lies in
| the top of the sample pool's 64 MB (0x47800000), which nothing uses before
| the first sample loads, after ev_card_ready: read it there, not later.
        .balign 4
        .globl  core_stock_info
core_stock_info:
        .long   CORE_STOCK_COPY, CORE_IMAGE_LEN

| core_tasks: the OS's sixteen static tasks, for a loader that must know no
| task is stopped where it writes: {count, the init task's row, the TCB's
| saved-SP offset}, then per task {TCB, stack's low end, stack size}, as
| each call of the task create (0x400015ac) and the BgWorker's (0x4008dfe0)
| pushes them. The init task (row 4, 0x40068eb6) runs ev_card_ready; a task
| not created yet has a saved SP outside its stack. Every address moves by
| CORE_TASK_SHIFT on 1.54.
        .balign 4
        .globl  core_tasks
core_tasks:
        .long   16, 4, 72
        .long   0x4060d138+CORE_TASK_SHIFT, 0x4060d18c+CORE_TASK_SHIFT, 0x1000   | deferred, prio 9
        .long   0x4060e858+CORE_TASK_SHIFT, 0x4060e8ac+CORE_TASK_SHIFT, 0x0800   | timers, prio 10
        .long   0x4060fbd4+CORE_TASK_SHIFT, 0x40610448+CORE_TASK_SHIFT, 0x28000  | UI, prio 6
        .long   0x402e3e48+CORE_TASK_SHIFT, 0x402e3e9c+CORE_TASK_SHIFT, 0x4000   | prio 2
        .long   0x4198497c+CORE_TASK_SHIFT, 0x419849d0+CORE_TASK_SHIFT, 0x4000   | init, prio 1
        .long   0x41980928+CORE_TASK_SHIFT, 0x4198097c+CORE_TASK_SHIFT, 0x4000   | prio 0
        .long   0x4198cbf4+CORE_TASK_SHIFT, 0x41988bf4+CORE_TASK_SHIFT, 0x4000   | prio 7
        .long   0x419a29dc+CORE_TASK_SHIFT, 0x419a2a30+CORE_TASK_SHIFT, 0x4000   | BgWorker, prio 2
        .long   0x421a00e0+CORE_TASK_SHIFT, 0x421a0134+CORE_TASK_SHIFT, 0x8000   | MIDI, prio 7
        .long   0x421a8d9c+CORE_TASK_SHIFT, 0x421a8df0+CORE_TASK_SHIFT, 0x8000   | MIDI async, prio 3
        .long   0x439caeb8+CORE_TASK_SHIFT, 0x421c8808+CORE_TASK_SHIFT, 0x4000   | prio 8
        .long   0x421cd4f8+CORE_TASK_SHIFT, 0x421cd54c+CORE_TASK_SHIFT, 0x4000   | prio 6
        .long   0x421d1f70+CORE_TASK_SHIFT, 0x421e43e4+CORE_TASK_SHIFT, 0x8000   | prio 5
        .long   0x421d1efc+CORE_TASK_SHIFT, 0x421db3e4+CORE_TASK_SHIFT, 0x8000   | prio 4
        .long   0x421d1e88+CORE_TASK_SHIFT, 0x421d23e4+CORE_TASK_SHIFT, 0x8000   | prio 4
        .long   0x421ed694+CORE_TASK_SHIFT, 0x421ee100+CORE_TASK_SHIFT, 0x4000   | prio 5

| ---- the stock copy, at boot ------------------------------------------------
| core_stock_copy: from .boot (core.s, STOCK_COPY), before the OS or the
| core's own copier writes anything: the OS image as the bootloader left it
| (with the flashed build's sites in it; a loader lays the build
| descriptor's shadow over those) to CORE_STOCK_COPY.
        .section .boot, "ax"
        .globl  core_stock_copy
core_stock_copy:
        lea     0x40000400, %a0
        movea.l #CORE_STOCK_COPY, %a1
        move.l  #CORE_IMAGE_LEN / 4, %d0
0:      move.l  (%a0)+, (%a1)+
        subq.l  #1, %d0
        bne.s   0b
        rts

        .section .bss
        .align  4
core_crash_stock:
        .space  4
