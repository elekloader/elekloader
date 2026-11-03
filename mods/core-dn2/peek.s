// SPDX-License-Identifier: GPL-2.0-or-later
// DN2 OS 1.12 DSP peek: the end of the block handler (PC16 0x1c9e5f) answers the ColdFire's
// request with one halfword of DSP memory per block, in reply word 2, which stock never writes.
// The request is in the frame's tail (byte 0xa80), four DSP words:
//   magic 0xd5ee | seq << 16, address, count (words, 1..1024), check (the three xor 0x5a5a5a5a).
// The answer is tag << 16 | halfword, the tag (seq & 15) << 12 | the halfword's index; the index
// 0xfff refuses the request (no read has that many halfwords), 0 means none. Halfwords repeat
// from the first after the last.
// R1, R2, I4 and I6 are live here; R10, R11, R13..R15, I2 and I3 are the handler's, reloaded
// by its epilogue, so this uses only those. State: the last seq at 0x2e0600, the cursor at 0x2e0604;
// the busy measure's words from 0x2e0610.
// Right shifts take their count from a register: this Selache pin encodes a negative immediate
// count without its sign extension, which halts a real DSP (the emulator runs it).
.SECTION/SW seg_swco;
.GLOBAL dn2_peek_entry;
dn2_peek_entry:
DM(0x268a38)=R1;            // the store the hook displaced
// How busy the DSP is: every 1024 blocks, the window's cycles in FreeRTOS's idle task
// (dn2_idle_entry adds them) and its total cycles, in the record at 0x2e0630: seq, idle, total, the
// busiest window's idle and total, seq again (two different seqs: read across an update). Busy is
// not headroom: stock's audio task also holds the DSP while it waits.
R13=DM(0x2e0610);
R13=PASS R13;
IF EQ JUMP peek;            // no idle hook: leave the busy record zero
R13=DM(0x2e0618);
R13=R13+1;
DM(0x2e0618)=R13;
R14=1023;
R13=R13 AND R14;
IF NE JUMP peek;
R13=EMUCLK;
R14=DM(0x2e0620);
DM(0x2e0620)=R13;
R15=DM(0x2e0610);
R11=DM(0x2e061c);
DM(0x2e061c)=R15;
R14=PASS R14;
IF EQ JUMP peek;            // the first window has no start
R13=R13-R14;                // total cycles
R15=R15-R11;                // idle cycles
R14=DM(0x2e0630);
R14=R14+1;
DM(0x2e0630)=R14;
DM(0x2e0634)=R15;
DM(0x2e0638)=R13;
R11=DM(0x2e063c);
R11=PASS R11;
IF EQ JUMP busiest;
COMPU(R15,R11);
IF GE JUMP published;
busiest:
DM(0x2e063c)=R15;
DM(0x2e0640)=R13;
published:
DM(0x2e0644)=R14;
peek:
R10=DM(-3,I6);              // the frame the handler received
R11=0xa80;
R10=R10+R11;
I2=R10;
R13=DM(0,I2);
R11=0xffff;
R11=R13 AND R11;
R14=0xd5ee;
COMP(R11,R14);
IF NE JUMP idle;
R14=DM(1,I2);               // address
R15=DM(2,I2);               // words
R10=R13 XOR R14;
R10=R10 XOR R15;
R11=0x5a5a5a5a;
R10=R10 XOR R11;
R11=DM(3,I2);
COMP(R10,R11);
IF NE JUMP idle;
// A new seq starts from its first halfword.
R11=-16;
R10=LSHIFT R13 BY R11;
R11=DM(0x2e0600);
COMP(R10,R11);
IF EQ JUMP same;
DM(0x2e0600)=R10;
R11=0;
DM(0x2e0604)=R11;
same:
// 1..1024 aligned words, all inside one readable L1 or L2 range.
R11=R15-1;
R13=1023;
COMPU(R11,R13);
IF GT JUMP refuse;
R11=3;
R11=R14 AND R11;
IF NE JUMP refuse;
R11=LSHIFT R15 BY 2;
R11=R11+R14;                // the end
COMPU(R11,R14);
IF LE JUMP refuse;         // reject an end that wrapped past uint32
R13=0x240000;
COMPU(R14,R13);
IF LT JUMP refuse;
R13=0x270000;
COMPU(R11,R13);
IF LE JUMP readable;
R13=0x2c0000;
COMPU(R14,R13);
IF LT JUMP refuse;
R13=0x2ec000;               // L1 block 1, below its cache
COMPU(R11,R13);
IF LE JUMP readable;
R13=0x300000;
COMPU(R14,R13);
IF LT JUMP refuse;
R13=0x31c000;               // L1 block 2, below its cache
COMPU(R11,R13);
IF LE JUMP readable;
R13=0x380000;
COMPU(R14,R13);
IF LT JUMP refuse;
R13=0x39c000;               // L1 block 3, below its cache
COMPU(R11,R13);
IF LE JUMP readable;
R13=0x20000000;
COMPU(R14,R13);
IF LT JUMP refuse;
R13=0x200fa000;             // L2, below the special window
COMPU(R11,R13);
IF GT JUMP refuse;
readable:
R13=DM(0x2e0604);           // the halfword index
R11=-1;
R11=LSHIFT R13 BY R11;
R11=LSHIFT R11 BY 2;
R11=R11+R14;
I3=R11;
R11=DM(0,I3);
BTST R13 BY 0;
IF SZ JUMP low;
R14=-16;
R11=LSHIFT R11 BY R14;
low:
R14=0xffff;
R11=R11 AND R14;
R14=0xf;
R14=R10 AND R14;
R14=LSHIFT R14 BY 12;
R14=R14 OR R13;
R14=LSHIFT R14 BY 16;
R14=R14 OR R11;
DM(2,I4)=R14;
R13=R13+1;
R11=LSHIFT R15 BY 1;
COMP(R13,R11);
IF LT JUMP next;
R13=0;
next:
DM(0x2e0604)=R13;
JUMP 0x1c9e62;
refuse:
R14=0xf;
R14=R10 AND R14;
R14=LSHIFT R14 BY 12;
R13=0xfff;
R14=R14 OR R13;
R14=LSHIFT R14 BY 16;
DM(2,I4)=R14;
JUMP 0x1c9e62;
idle:
R14=0;
DM(2,I4)=R14;
JUMP 0x1c9e62;

// The idle task's loop (its jump back, PC16 0xb88abb) comes here once a pass. A gap shorter than
// 3000 cycles since the last pass was idle; a longer one was other work, a task or an interrupt.
// R0 and R1 are free: the loop's next call clobbers them.
.GLOBAL dn2_idle_entry;
dn2_idle_entry:
R0=EMUCLK;
R1=DM(0x2e0614);
DM(0x2e0614)=R0;
R0=R0-R1;
R1=3000;
COMPU(R0,R1);
IF GE JUMP 0xb88aab;
R1=DM(0x2e0610);
R1=R1+R0;
DM(0x2e0610)=R1;
JUMP 0xb88aab;
.ENDSEG;
