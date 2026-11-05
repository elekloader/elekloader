| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn1 5.0: the system's moments (Digitone mk1). ColdFire V4; assemble
| with -mcpu=54455. Beside core.s.
|
|   CARD_STATUS   the init task's card status word (0: the +Drive is up)
|   ROUTER        the SysEx router's entry: route(msg, len, source)
|   CRASH_SET     sets the default exception handler's RAM hook
|   CARD_MUTEX    the +Drive's mutex; MUTEX_LOCK locks one (the RTOS's)
|   PANEL_PENDING bytes behind the panel link's DMA; UART_PENDING the UART's
|   DRAIN         the stock drain: waits for both, then for the panel link
|
| ---- ev_card_ready: the +Drive is up, nothing else is (5.0) ----------------
| At 0x4008d27c (0x4008d29c in 1.44) the init task stores the card bring-up's
| status, `move.l d0,CARD_STATUS`: every exit of the bring-up lands there.
| The project, the UI task, the views and the MIDI task do not exist yet;
| the C++ constructors have run. core_card_ready makes the store, then calls
| each handler of ev_card_ready, f(int status): 0, the card reads and writes
| (fw_card_read, fw_card_write); 1, 6-9, it failed. The init task, at
| priority 1, before the project loads: a handler may read the card, must
| not take the firmware's heap (a block kept from boot can stall a first
| boot), and should be quick. d0-d1/a0-a1 come back as they went.
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

| ---- ev_sysex: every SysEx message but F0 7E, at the router (5.0) ---------
| The router route(msg, len, source) at 0x400ea854 (0x400eaac8 in 1.44) is
| tail-called by the MIDI task's system-message dispatcher for each whole
| SysEx message, F0 to F7. Core takes its first two instructions (lea,
| movem: 8 bytes, by jmp) and first calls each handler of ev_sysex,
|   int f(const uint8_t *msg, int len, int source)
| source: 2 or 4 USB, 8 or 0x10 DIN. A handler that returns nonzero takes the
| message: the router does not see it. With none taking it, the two
| instructions, and the router goes on. The MIDI task (priority 7, above the
| UI task): a handler that blocks holds every MIDI input. Reply with
| fw_sysex_send in pieces of at most 144 bytes; it busy-waits while the USB
| ring is full. digihealth's site (0x400ea880) is further in: both coexist.
        .globl  core_sysex
core_sysex:
        tst.l   ev_sysex
        beq.s   9f
        lea     -16(%sp), %sp
        movem.l %d0-%d1/%a0/%a2, (%sp)
        lea     ev_sysex, %a2
1:      move.l  (%a2)+, %d0
        beq.s   2f
        movea.l %d0, %a0
        move.l  28(%sp), -(%sp)         | source
        move.l  28(%sp), -(%sp)         | len
        move.l  28(%sp), -(%sp)         | msg
        jsr     (%a0)
        lea     12(%sp), %sp
        tst.l   %d0
        beq.s   1b
        movem.l (%sp), %d0-%d1/%a0/%a2  | taken
        lea     16(%sp), %sp
        rts
2:      movem.l (%sp), %d0-%d1/%a0/%a2
        lea     16(%sp), %sp
9:      lea     -16(%sp), %sp           | the router's two instructions
        movem.l %d2-%d3/%a2-%a3, (%sp)
        jmp     ROUTER + 8

| ---- ev_crash: an exception the firmware does not handle (5.0) -------------
| Every vector starts at the default handler (0x40001696), which masks the
| interrupts and calls a hook in RAM with the stack pointer of the exception
| frame. At boot the init task installs the stock hook, a crash printer that
| ends in `halt`: at 0x400e7872 (0x400e7ae6 in 1.44), CRASH_SET(printer)
| with the printer pushed. Core installs core_crash instead and keeps the
| printer. core_crash calls each handler of ev_crash, f(void *frame): the
| frame is the format/status word and the PC, then the rest of the stack.
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
        move.l  %a2, -(%sp)             | the default handler keeps d0-d1/a0-a1 only
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

| ---- core_restart: reboot, from a task (5.0) ---------------------------------
| void core_restart(void). Waits for the +Drive to be idle (takes its mutex,
| which a transfer holds), lets the interrupts send what the panel link and
| the UART still hold, then does what every stock restart does: interrupts
| masked, DDR word 0 cleared through the uncached alias (the bootstrap reads
| it only for its console magic), 27 nops, RCM.RCR = 0x80. The stock routine
| (0x40005aa0) masks the interrupts first and then waits on counts only an
| interrupt moves, which can hang; this does not. Not from an interrupt:
| use core_restart_now there (no waiting).
        .globl  core_restart
core_restart:
        move.w  %sr, %d1
        move.l  %d1, -(%sp)             | the caller's interrupt mask
        pea     CARD_MUTEX
        jsr     MUTEX_LOCK              | no transfer runs past this; never unlocked
        addq.l  #4, %sp
1:      tst.l   PANEL_PENDING           | interrupts on: let them send it
        bgt.s   1b
        tst.l   UART_PENDING
        bgt.s   1b
        move.w  #0x2700, %sr
        tst.l   PANEL_PENDING           | queued since: send it and look again
        bgt.s   2f
        tst.l   UART_PENDING
        bgt.s   2f
        jsr     DRAIN                   | nothing pending: waits for the link only
        bra.w   core_restart_now
2:      move.l  (%sp), %d1
        move.w  %d1, %sr                | interrupts back on: send it
        bra.s   1b

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
