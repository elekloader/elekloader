| SPDX-License-Identifier: GPL-2.0-or-later
| core for the Octatrack, OS 1.40C: rows mods add to PROJECT > SYSTEM > PERSONALIZE
| (core-ot 0.32). ColdFire V4 (MCF5445x); assemble with -mcpu=54455.
|
| PERSONALIZE keeps three parallel arrays of 16: labels 0x400b2a34, value texts
| (const char *get(void)) 0x400b2a74 and setters (void set(delta, press))
| 0x400b2ac0. Their addresses are instruction operands (ptr sites on ot_pers_label,
| ot_pers_get, ot_pers_set; core contributes stock's 16 to each, first). Its list's
| count is 15 on an MKI and 16 on an MKII (LED BRIGHTNESS):
|
|   0x40068fb2  moveq #15,d1 ; sub.l d0,d1 ; move.l d1,-(sp): the count, + the mods' rows
        .section .run, "ax"

        .globl  core_pers_count
core_pers_count:
        moveq   #15, %d1
        sub.l   %d0, %d1                | stock's: 15, or 16 on an MKII
        add.l   #ot_pers_get_n - 16, %d1        | and the rows mods added
        move.l  %d1, -(%sp)
        jmp     0x40068fb8
