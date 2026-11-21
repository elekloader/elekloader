| SPDX-License-Identifier: GPL-2.0-or-later
| The system's moments, for a mod that links more mods on the unit (TELOS):
| core-dt2 1.2 and core-dn2 0.5, each with its own addresses (mod.json).
| ColdFire V4; assemble with -mcpu=54455. Beside core.s. The Digitakt mk1
| core 4.5's system.s: each routine here is the mk1's routine with the II's
| addresses (the init task's card and file system joins, the crash hook, the
| restart); the two IIs share the code byte for byte. The SysEx router is
| core-dn2's sysex.s, which both build; the task tables are each core's
| tasks.s.
|
|   CARD_STATUS   the init task's card status word (0: the +Drive is up)
|   FS_MOUNTED    the +Drive file system's mounted flag (the Digitakt II's;
|                 without it, no ev_fs_ready: the Digitone II has no file system)
|   BOOTFLAGS     the boot's flags word (bit 6: a format boot)
|   CRASH_SET     sets the default exception handler's RAM hook
|   FW_RESTART    the stock restart: interrupts off, the console drained, restart
|   SLOT_SAVE     the project slot write: (project, slot, progress) -> ok
|   PROJ_LOAD     the project load: (project, slot, progress) -> ok

| ---- ev_card_ready: the +Drive is up, nothing else is ------------------------
| At CARD_SITE (Digitakt II 0x400cccbe, Digitone II 0x400cf0f2) the init task stores the card bring-up's status,
| `move.l d0,CARD_STATUS`: every exit of the bring-up lands there, as on the
| mk1. The project, the UI task and the views do not exist yet. Handlers:
| f(int status): 0, the card is up. The init task: a handler must not take
| the firmware's heap and should be quick. d0-d1/a0-a1 come back as they went.
        .text                           | after a core's C code: core-dn2 0.4's code keeps its place
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
| At 0x400ccd6c the init task's paths join after the card bring-up: the file
| system mounted (0x4015a72c), formatted on a format boot, or neither. Still
| before the project and the UI task (the main OS task starts after). The
| site was `moveq #64,d0; and.l BOOTFLAGS,d0`, which the next instruction
| branches on: core_fs_ready ends with it. Handlers: f(int mounted).
        .ifdef  FS_MOUNTED
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
        .endif

| ---- project slots: written, saved, loaded ----------------------------------
| SLOT_SAVE (Digitakt II 0x40043a30, Digitone II 0x40040190) writes a project to a slot, (project, slot, progress)
| -> d0's low byte nonzero when written: every SAVE, SAVE AS, and the working
| copy's (slot 128) after each load and at boot. It runs in a background
| worker task. core_slot_save, at its entry, calls ev_project_slot_write's
| handlers f(void *project, int slot, int progress), runs the routine, then
| ev_project_saved's f(void *project, int slot, int ok). Slot 128 is the
| working copy: most handlers skip it.
        .globl  core_slot_save
core_slot_save:
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_slot_write, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        .rept   3
        move.l  (20+12)(%sp), -(%sp)    | progress, slot, project
        .endr
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
        .rept   3
        move.l  12(%sp), -(%sp)
        .endr
        jsr     slot_save_stock
        lea     12(%sp), %sp
        tst.l   ev_project_saved
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_saved, %a2
3:      move.l  (%a2)+, %d1
        beq.s   4f
        movea.l %d1, %a0
        moveq   #0, %d1
        move.b  3(%sp), %d1             | ok: the routine's d0, low byte
        move.l  %d1, -(%sp)
        move.l  (20+4+8)(%sp), -(%sp)   | slot
        move.l  (20+8+4)(%sp), -(%sp)   | project
        jsr     (%a0)
        lea     12(%sp), %sp
        bra.s   3b
4:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts
slot_save_stock:
        lea     -52(%sp), %sp           | the two instructions the site replaced
        movem.l %d2-%d4/%a2-%a6, (%sp)
        jmp     SLOT_SAVE + 8

| PROJ_LOAD (Digitakt II 0x40046c38, Digitone II 0x40042e6e) loads a project from a slot, (project, slot,
| progress) -> d0's low byte nonzero when loaded (then it saves the working
| copy and sets the current slot). A LOAD PROJECT, in a worker task; the
| boot's own load of the working copy does not come here. core_project_load
| runs it, then calls ev_project_loaded's f(int slot, int err): err 0, LOAD
| OK.
        .globl  core_project_load
core_project_load:
        .rept   3
        move.l  12(%sp), -(%sp)
        .endr
        jsr     proj_load_stock
        lea     12(%sp), %sp
        tst.l   ev_project_loaded
        beq.s   9f
        lea     -20(%sp), %sp
        movem.l %d0-%d1/%a0-%a2, (%sp)
        lea     ev_project_loaded, %a2
1:      move.l  (%a2)+, %d1
        beq.s   2f
        movea.l %d1, %a0
        moveq   #0, %d1
        tst.b   3(%sp)                  | the routine's result
        bne.s   3f
        moveq   #1, %d1
3:      move.l  %d1, -(%sp)             | err
        move.l  (20+4+8)(%sp), -(%sp)   | slot
        jsr     (%a0)
        addq.l  #8, %sp
        bra.s   1b
2:      movem.l (%sp), %d0-%d1/%a0-%a2
        lea     20(%sp), %sp
9:      rts
proj_load_stock:
        lea     -36(%sp), %sp           | the two instructions the site replaced
        movem.l %d2-%d5/%a2, (%sp)
        jmp     PROJ_LOAD + 8

| ---- ev_crash: an exception the firmware does not handle --------------------
| As on the mk1: every vector starts at the default handler (0x40001252),
| which masks the interrupts and calls a RAM hook with the exception frame.
| The OS installs a crash printer (Digitakt II 0x4011d4a2, Digitone II 0x4011ee16), CRASH_SET(printer) with the
| printer pushed. Core installs core_crash instead and keeps the printer.
| core_crash calls each handler of ev_crash, f(void *frame), then the printer.
| Interrupts are masked and the RTOS may be anywhere: no blocking, no heap.
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
| 0x40005608 on both): interrupts off, the console drained, DDR's word 0 cleared, the
| reset controller's soft reset. From a task. It does not return.
| void core_restart_now(void): the same without the drain, for an exception
| handler or code that runs with interrupts masked.
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

        .section .bss
        .align  4
core_crash_stock:
        .space  4
