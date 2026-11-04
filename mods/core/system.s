| SPDX-License-Identifier: GPL-2.0-or-later
| core 5.0 (Digitakt mk1): the system's moments. ColdFire V4; assemble with
| -mcpu=54455. Beside core.s.
|
|   CARD_STATUS   the init task's card status word (0: the +Drive is up)
|   UI_SPAWN      creates and starts the UI task
|   CRASH_SET     sets the default exception handler's RAM hook
|   FW_RESTART    the stock restart: interrupts off, drain the links, restart
|
| ---- ev_card_ready: the +Drive is up, nothing else is (5.0) ----------------
| At 0x400691d2 the init task stores the card bring-up's status,
| `move.l d0,CARD_STATUS`: every exit of the bring-up lands there. The
| project, the UI task and the views do not exist yet. core_card_ready makes
| the store, then calls each handler of ev_card_ready, f(int status): 0, the
| card is up; 1, 6-9, it failed. The init task, at the RTOS's lowest
| priority, before the project loads: a handler must not take the
| firmware's heap (a block kept from boot can stall a first boot) and
| should be quick. d0-d1/a0-a1 come back as they went.
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

| ---- ev_boot_ui, ev_idle: the end of the boot, then idle (5.0) ------------
| ev_boot_ui: f(void). The init task has loaded the working project and
| started the other tasks, and is about to start the UI task (0x4006933c,
| the call of the UI spawner): the end of the boot. The views come next,
| in the UI task.
| ev_idle: f(void), over and over in the init task once the UI task is up:
| the lowest priority task, so only when no other task is ready. With no
| handler the stub returns to the stock spin (0x4006932e). A handler must
| not wait for anything; it never runs at interrupt level.
        .globl  core_boot_ui
core_boot_ui:
        tst.l   ev_boot_ui
        beq.s   7f
        lea     ev_boot_ui, %a2         | the init task keeps nothing in a2 here
8:      move.l  (%a2)+, %d1
        beq.s   7f
        movea.l %d1, %a0
        jsr     (%a0)
        bra.s   8b
7:      jsr     UI_SPAWN
        tst.l   ev_idle
        beq.s   9f
7:      lea     ev_idle, %a2
8:      move.l  (%a2)+, %d1
        beq.s   7b
        movea.l %d1, %a0
        jsr     (%a0)
        bra.s   8b
9:      rts

| ---- ev_crash: an exception the firmware does not handle (5.0) -------------
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

| ---- core_restart: reboot (5.0) ----------------------------------------------
| void core_restart(void): the firmware's own restart (FW_RESTART,
| 0x40005864, what its reboot command runs): interrupts off, the panel link
| and the UART drained, then the restart. From a task. It does not return.
| void core_restart_now(void): the same without the drain, for an exception
| handler or code that already runs with interrupts masked.
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
