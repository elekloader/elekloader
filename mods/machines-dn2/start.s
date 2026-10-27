| SPDX-License-Identifier: GPL-2.0-or-later
| The OS entry's call of main, after core's boot copier and before every OS
| initializer: copy the stock parameter table that the repointed references
| read. machines.c appends the pages' rows to the copy.
    .text
    .globl dn2_start
dn2_start:
    lea STOCK_T1,%a0
    lea dn2_t1,%a1
    move.l #T1_ROWS*15,%d0
1:  move.l (%a0)+,(%a1)+
    subq.l #1,%d0
    bne.s 1b
    jmp OS_MAIN
