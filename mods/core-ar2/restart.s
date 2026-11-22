| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2: a restart a mod can ask for, and the working project kept before it
| (Analog Rytm MKII, OS 1.74), core-a4mk2's restart.s with the Rytm's
| addresses. ColdFire V4; beside finish.s, whose FINISH_PANEL, FINISH_UART and
| FINISH_DRAIN it uses. The device's mod.json gives:
|
|   STORE_PTR    the +Drive slot store's singleton (0 until it is built)
|   STORE_MUTEX  the offset of its mutex, held across each whole store
|                operation (a project save or load, the slots); RMUTEX_LOCK
|                locks it (the RTOS's recursive mutex)
|   DRIVE_MUTEX  ekFS's mutex, held inside each +Drive file operation
|   CARD_MUTEX   the eMMC's mutex, held across each raw sector transfer
|   MUTEX_LOCK   locks either of those two (the RTOS's plain mutex)
|   KEEP_PACK    the full pack of the live project into the working image
|   KEEP_WRITE   the working image written to the eMMC's next working slot
|   RESET_DDR, RESET_CTRL  what stock's restart clears and writes last
|
| ---- core_restart: reboot, from a task ----------------------------------------
| void core_restart(void). Waits for the +Drive to be idle: takes the store's
| mutex, then ekFS's, then the eMMC's (stock takes each of the first two before
| the third), and never gives them back, so
| no save, load or transfer starts or is cut short. Then lets the interrupts
| send what the panel link and the UART still hold, and does what every stock
| restart does: interrupts masked, the drain, the restart's last writes. The
| stock restart masks the interrupts first and then waits on counts only an
| interrupt moves, which can hang (README, "The OS upgrade's restart"); this
| does not.
|
| From a task, with interrupts on: it blocks on the mutexes. A mod that writes
| the +Drive under a lock of its own takes that lock before calling. A save
| stock has queued but not started is not waited for. From an interrupt or
| ev_crash use core_restart_now (no waiting).

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
1:      pea     DRIVE_MUTEX
        jsr     MUTEX_LOCK              | nor a file operation
        addq.l  #4, %sp
        pea     CARD_MUTEX
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

| ---- core_project_keep: the working project kept, from a task ------------------
| void core_project_keep(void). The Rytm keeps its working project as an image
| in RAM (0x405c8f20, README "The project save") that the power-fail handler
| writes to one of two eMMC working slots and the boot reads back. Stock keeps
| the image current with its write-through saves, and a change none of them
| makes (a mod's own field) never reaches it. This packs the whole image from
| the live project (KEEP_PACK, the boot's full pack) and writes it to the next
| slot at once (KEEP_WRITE, what stock's OS upgrade does before its reboot), so
| a restart after it boots on the project as it is now. KEEP_WRITE works once a
| boot and disarms the power-fail write: only a restart may follow. It takes
| the eMMC's mutex for each transfer: call it before core_restart, never with
| the +Drive held. The pack serializes the whole project, a pause. Returns.
        .globl  core_project_keep
core_project_keep:
        jsr     KEEP_PACK
        jsr     KEEP_WRITE
        rts

| ---- core_restart_now: reboot at once -----------------------------------------
| void core_restart_now(void), never returns: the stock restart's last steps
| without its drain, for an interrupt or an ev_crash handler.
        .globl  core_restart_now
core_restart_now:
        move.w  #0x2700, %sr
        clr.l   RESET_DDR
        .rept   27
        nop
        .endr
        moveq   #-128, %d0
        move.b  %d0, RESET_CTRL
4:      bra.s   4b
