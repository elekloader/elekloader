| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2: a restart a mod can ask for (Analog Four MKII), core-dn1
| 5.0's core_restart with the A4's addresses. ColdFire V4; beside finish.s,
| whose FINISH_PANEL, FINISH_UART and FINISH_DRAIN it uses.
|
|   STORE_PTR    the +Drive slot store's singleton (0 until it is built)
|   STORE_MUTEX  the offset of its mutex, held across each whole store
|                operation (a save, a load, FORMAT +DRIVE); RMUTEX_LOCK
|                locks it (the RTOS's recursive mutex)
|   CARD_MUTEX   the card's mutex, held across each raw sector transfer;
|                MUTEX_LOCK locks it (the RTOS's plain mutex)
|
| ---- core_restart: reboot, from a task ----------------------------------------
| void core_restart(void). Waits for the +Drive to be idle: takes the store's
| mutex, then the card's, in the order stock takes them, and never gives
| them back, so no save, load or transfer starts or is cut short. Then lets
| the interrupts send what the panel link and the UART still hold, and does
| what every stock restart does: interrupts masked, the drain, DDR word 0
| cleared, a short pause, the reset controller's software reset. The stock
| restart masks the interrupts first and then waits on counts only an
| interrupt moves, which can hang; this does not.
|
| From a task, with interrupts on: it blocks on the mutexes. A mod that
| writes the +Drive under a lock of its own takes that lock before calling.
| A save stock has queued but not started is not waited for. From an
| interrupt or ev_crash use core_restart_now (no waiting).

        .section .run, "ax"
        .globl  core_restart
core_restart:
        move.w  %sr, %d1
        move.l  %d1, -(%sp)             | the caller's interrupt mask
        move.l  STORE_PTR, %d0
        beq.s   1f                      | no store yet: nothing of it runs
        addi.l  #STORE_MUTEX, %d0
        move.l  %d0, -(%sp)
        jsr     RMUTEX_LOCK             | no store operation runs past this
        addq.l  #4, %sp
1:      pea     CARD_MUTEX
        jsr     MUTEX_LOCK              | nor a raw transfer; never unlocked
        addq.l  #4, %sp
2:      tst.l   FINISH_PANEL            | interrupts on: let them send it
        bgt.s   2b
        tst.l   FINISH_UART
        bgt.s   2b
        move.w  #0x2700, %sr
        tst.l   FINISH_PANEL            | queued since: send it and look again
        bgt.s   3f
        tst.l   FINISH_UART
        bgt.s   3f
        jsr     FINISH_DRAIN            | nothing pending: waits for the link only
        bra.w   core_restart_now
3:      move.l  (%sp), %d1
        move.w  %d1, %sr                | interrupts back on: send it
        bra.s   2b

| ---- core_restart_now: reboot at once -----------------------------------------
| void core_restart_now(void), never returns: the stock restart's last steps
| without its drain, for an interrupt or an ev_crash handler.
        .globl  core_restart_now
core_restart_now:
        move.w  #0x2700, %sr
        clr.l   0x48000000
        .rept   27
        nop
        .endr
        moveq   #-128, %d0
        move.b  %d0, 0xEC090000
4:      bra.s   4b
