// SPDX-License-Identifier: GPL-2.0-or-later
// DN2 OS 1.12 machine dispatcher: the voice and filter hooks look the slot's machine ID up in their
// kind's table and call its kernel on the kind's L1 stack, or continue the stock path when the
// entry's PC16 is 0. The linker places this first in the machine pool, rebases it, fills the tables
// and points the stock hooks at dn2_voice_hook., dn2_filter_hook. and dn2_filter_capture.. Own code
// and data are reached only through labels, so it rebases.
//
// dn2_voice_table., dn2_filter_table.: 128 entries of 16 bytes, by machine ID (the frame's type
// halfword; 128 and above take the stock path): +0 kernel PC16 (0: stock), +4 state base (16 slots),
// +8 state bytes a slot, +12 an SDK machine's knob map (0: an mk2 machine). A knob map is the count,
// then per knob min, q and r (sharc.knob_map): knob = min + q * raw + (r * raw + 16256) / 32512 is
// sdk_alpha_map's value, raw the knob's frame halfword clamped to 32512.
//
// Each kernel is a C function called with R4 = its input and R8 = the voice buffer:
// - mk2 synth, struct dn2_voice_input (digitone-mk2/voice.h): output, slot, 8.8 pitch, flags
//   (START the block after a trigger edge, whose block is silent), param (40 words), state;
// - SDK machine, struct elek_voice (elekloader/sdk_alpha_machine.h): knob, track, start (as START),
//   16.16 pitch, velocity, state;
// - mk2 filter, struct dn2_filter_input (digitone-mk2/filter.h): samples, slot, flags (RESET on
//   entering the filter and on a trigger edge), cutoff, resonance, g, state.
// A slot's state is zeroed when its machine changes. A call that breaks its stack's guard word mutes
// that machine (its slots play silence) until the DSP reloads; the guard is rewritten, so the kind's
// other machines keep running.
//
// Right shifts take their count from a register: this Selache pin encodes a negative immediate
// count without its sign extension, which halts a real DSP. `.VAR` reserves one word only, so
// arrays are a labelled word and `.ALIGN n`, which pads with zeros to n words from the section's
// start, or continuation words.

.SECTION/DM seg_dmda;
.GLOBAL dn2_voice_table.;
.GLOBAL dn2_filter_table.;
.VAR dn2_voice_table. = 0;          // word 0
.ALIGN 512;
.VAR dn2_filter_table. = 0;         // word 512
.ALIGN 1024;

// A kind's block, by word: 0 table, 1 stack base (guard), 2 stack top, 3 kind (0 voice, 1 filter);
// this call's 5 buffer, 6 slot, 7 slot bit, 8 ID, 9 table entry, 10 state, 11 flags; across calls
// 12 the trigger bits seen last block, 13 pending START bits (voice), 16..19 muted IDs (bit ID % 32
// of word ID / 32), 32..47 per slot the last ID + 1 (0: stock).
.VAR kv = dn2_voice_table.;         // word 1024
.VAR = 0x2de000;
.VAR = 0x2deffc;
.VAR = 0;
.ALIGN 1088;
.VAR kf = dn2_filter_table.;        // word 1088
.VAR = 0x2df000;
.VAR = 0x2dfffc;
.VAR = 1;
.ALIGN 1152;
// Captured per slot: cutoff, resonance at +64 bytes, G at +128.
.VAR cap = 0;                       // word 1152
.ALIGN 1200;
.VAR input = 0;                     // word 1200: the kernel's input
.ALIGN 1208;
.VAR params = 0;                    // word 1208: an mk2 synth's 40 param words, an SDK machine's knobs
.ALIGN 1248;
.VAR cur = 0;                       // this call's kind block
.VAR cap_astat = 0;
.VAR sv_i7 = 0;
.VAR sv_b7 = 0;
.VAR sv_l7 = 0;
// The hooks' register save area.
// Words 0..31: PX1, PX2, I0, I1, I2, I3, I5, I12, I13,
// M0, M1, M2, M3, M4, M5, M6, M7, M13, M14, M15, L0, L1, L2, L3, L4, L5, LCNTR, MODE1, ASTATX, ASTATY, STKYX, STKYY; 32..63: R0..R15, each as PX1 and PX2.
// Selache encodes a system register in DM(address) moves with the wrong register code, so these go
// through I4, whose own absolute move is right.
.VAR sv_i4 = 0;
.VAR save_area = 0;
.ALIGN 1318;

.SECTION/SW seg_swco;
.GLOBAL dn2_voice_hook.;
.GLOBAL dn2_filter_hook.;
.GLOBAL dn2_filter_capture.;

// The voice hook (PC16 0xb8251b): R12 is the voice buffer, R15 counts the slots down from 16.
dn2_voice_hook.:
CALL save;
I2=kv;
// The second stock caller is not a voice-buffer call.
COMP(R8,R12);
IF NE JUMP leave;
R0=16;
R0=R0-R15;
IF LT JUMP leave;
R1=15;
COMP(R0,R1);
IF GT JUMP leave;
R1=LSHIFT R0 BY 7;
R2=0x804acf90;
R1=R1+R2;
COMP(R12,R1);
IF NE JUMP leave;
DM(5,I2)=R12;
R1=74;                              // the voice's type: frame halfword 74 + slot
R5=0;
JUMP common;

// The filter hook (0x28392b0e): I5 is the voice buffer, R2 the stock internal filter type.
dn2_filter_hook.:
CALL save;
I2=kf;
R6=R2;
R0=16;
R0=R0-R15;
IF LT JUMP leave;
R1=15;
COMP(R0,R1);
IF GT JUMP leave;
R1=LSHIFT R0 BY 7;
R2=0x804acf90;
R1=R1+R2;
R2=I5;
COMP(R1,R2);
IF NE JUMP leave;
DM(5,I2)=R2;
R1=90;                              // the filter's type: frame halfword 90 + slot
R5=R6;                              // stock until its internal type has settled to zero
JUMP common;

// R0 slot, R1 the type's halfword for slot 0, R5 nonzero for the stock path, I2 the kind's block.
common:
DM(6,I2)=R0;
R7=1;
R7=LSHIFT R7 BY R0;
R8=NOT R7;
DM(7,I2)=R7;
R1=R0+R1;
CALL halfword;
R5=PASS R5;
IF NE JUMP stock;
R3=128;
COMPU(R2,R3);
IF GE JUMP stock;
DM(8,I2)=R2;
R3=LSHIFT R2 BY 4;
R4=DM(0,I2);
R3=R3+R4;
DM(9,I2)=R3;
I3=R3;
R4=DM(0,I3);
R4=PASS R4;
IF EQ JUMP stock;
CALL muted;
R4=DM(0,I4);
R4=R4 AND R3;
IF NE JUMP silence;
// This slot's state: the base plus the slot times a slot's bytes (no state: 0).
R4=DM(2,I3);
R3=R0*R4(SSI);
R4=DM(1,I3);
R3=R3+R4;
DM(10,I2)=R3;
// A new machine in this slot: zero its state and forget the slot's trigger.
R11=0;
R3=R2+1;
R4=I2;
R10=LSHIFT R0 BY 2;
R4=R4+R10;
R10=128;
R4=R4+R10;
I4=R4;
R4=DM(0,I4);
COMP(R4,R3);
IF EQ JUMP edges;
DM(0,I4)=R3;
R4=DM(10,I2);
I4=R4;
R3=DM(2,I3);
R4=-2;
R3=LSHIFT R3 BY R4;
R4=0;
R3=PASS R3;
IF EQ JUMP zeroed;
zero:
DM(I4,M6)=R4;
R3=R3-1;
IF NE JUMP zero;
zeroed:
R4=DM(12,I2);
R4=R4 AND R8;
DM(12,I2)=R4;
R4=DM(13,I2);
R4=R4 AND R8;
DM(13,I2)=R4;
R11=1;
// A note-on (frame halfwords 0x11 and 0x13) this block that was not there the last one is an edge.
edges:
R9=DM(0x25c4ac);
R10=DM(0x25c4b0);
R9=R9 OR R10;
R10=-16;
R9=LSHIFT R9 BY R10;
R9=R9 AND R7;
R4=DM(12,I2);
R3=R4 AND R7;
R4=R4 AND R8;
R4=R4 OR R9;
DM(12,I2)=R4;
R4=DM(3,I2);
R4=PASS R4;
IF NE JUMP filter_flags;
// A synth's trigger block is silent; START comes with the next block's settled pitch.
COMP(R9,R3);
IF GT JUMP voice_edge;
R11=0;
R4=DM(13,I2);
R3=R4 AND R7;
IF EQ JUMP flagged;
R11=1;
R4=R4 AND R8;
DM(13,I2)=R4;
JUMP flagged;
voice_edge:
R4=DM(13,I2);
R4=R4 OR R7;
DM(13,I2)=R4;
JUMP silence;
// A filter resets on entering the slot and on each edge.
filter_flags:
COMP(R9,R3);
IF LE JUMP flagged;
R11=1;
flagged:
DM(11,I2)=R11;
I5=input;
R4=DM(3,I2);
R4=PASS R4;
IF NE JUMP filter_input;
R4=DM(3,I3);
R4=PASS R4;
IF NE JUMP sdk_input;

// An mk2 synth's input: the frame's pitch and its 40 knob words, frame halfwords 0x6d + 0x49 slot + i.
R4=DM(5,I2);
DM(0,I5)=R4;
DM(1,I5)=R0;
R1=R0+1;
CALL halfword;
DM(2,I5)=R2;
DM(3,I5)=R11;
R4=params;
DM(4,I5)=R4;
R4=DM(10,I2);
DM(5,I5)=R4;
CALL first_knob;
I1=params;
R5=40;
mk2_param:
CALL halfword;
DM(I1,M6)=R2;
R1=R1+1;
R5=R5-1;
IF NE JUMP mk2_param;
JUMP call;

// An SDK machine's struct elek_voice: knob[k] is sdk_alpha_map of knob k's frame halfword.
sdk_input:
R4=params;
DM(0,I5)=R4;
// The slot's track: the DSP's slot-to-track map, a halfword a slot.
R1=R0;
R6=0x257898;
CALL half;
DM(1,I5)=R2;
DM(2,I5)=R11;
R1=R0+1;
CALL halfword;
R2=LSHIFT R2 BY 8;
DM(3,I5)=R2;
R1=26;                              // the velocity, 8.8: frame halfword 26 + slot
R1=R0+R1;
CALL halfword;
R3=-8;
R2=LSHIFT R2 BY R3;
R3=1;
COMP(R2,R3);
IF GE JUMP velocity_low;
R2=R3;
velocity_low:
R3=127;
COMP(R2,R3);
IF LE JUMP velocity_high;
R2=R3;
velocity_high:
DM(4,I5)=R2;
R4=DM(10,I2);
DM(5,I5)=R4;
R4=DM(3,I3);
I0=R4;
R5=DM(I0,M6);
I1=params;
CALL first_knob;
R5=PASS R5;
IF EQ JUMP call;
knob:
CALL halfword;
R3=32512;
COMP(R2,R3);
IF LE JUMP clamped;
R2=R3;
clamped:
R9=DM(I0,M6);                       // min
R10=DM(I0,M6);                      // q
R12=DM(I0,M6);                      // r
R10=R10*R2(SSI);
R12=R12*R2(SSI);
R3=16256;
R12=R12+R3;
// (r * raw + 16256) / 32512 is t / 127 for t = (r * raw + 16256) >> 8, below 2 ** 22: four steps of
// q += t >> 7, t = (t & 127) + (t >> 7) leave t below 128, and t = 127 is one more. Shifts and adds only:
// this Selache pin encodes every multiply mode but SSI as one.
R3=-8;
R12=LSHIFT R12 BY R3;
R13=0;
R14=127;
R3=-7;
R4=LSHIFT R12 BY R3;
R13=R4+R13;
R12=R12 AND R14;
R12=R4+R12;
R4=LSHIFT R12 BY R3;
R13=R4+R13;
R12=R12 AND R14;
R12=R4+R12;
R4=LSHIFT R12 BY R3;
R13=R4+R13;
R12=R12 AND R14;
R12=R4+R12;
R4=LSHIFT R12 BY R3;
R13=R4+R13;
R12=R12 AND R14;
R12=R4+R12;
COMP(R12,R14);
IF LT JUMP divided;
R13=R13+1;
divided:
R12=R13;
R9=R9+R10;
R9=R9+R12;
DM(I1,M6)=R9;
R1=R1+1;
R5=R5-1;
IF NE JUMP knob;
JUMP call;

// An mk2 filter's input: this block's captured cutoff, resonance and G.
filter_input:
R4=DM(5,I2);
DM(0,I5)=R4;
DM(1,I5)=R0;
DM(2,I5)=R11;
R1=LSHIFT R0 BY 2;
R2=cap;
R1=R1+R2;
I4=R1;
R2=DM(0,I4);
DM(3,I5)=R2;
R2=DM(16,I4);
DM(4,I5)=R2;
R2=DM(32,I4);
DM(5,I5)=R2;
R4=DM(10,I2);
DM(6,I5)=R4;

// Call the kernel on the kind's own linear stack in L1 (the context switch saves through the PM
// bus, which reaches only L1). Writing B7 also loads I7, so the stack registers change with
// interrupts off: one taken between the writes would push where I7 then pointed.
call:
DM(cur)=I2;
R13=DM(1,I2);
R14=DM(2,I2);
R4=input;
R8=DM(5,I2);
I12=DM(0,I3);
DM(sv_i7)=I7;
DM(sv_b7)=B7;
DM(sv_l7)=L7;
BIT CLR MODE1 0x1000;
L7=0;
B7=R13;
I7=R14;
BIT SET MODE1 0x1000;
R2=I6;
I6=I7;
JUMP(M13,I12)(DB);
DM(I7,M7)=R2;
DM(I7,M7)=returned-1;
returned:
BIT CLR MODE1 0x1000;
B7=DM(sv_b7);
I7=DM(sv_i7);
L7=DM(sv_l7);
BIT SET MODE1 0x1000;
I2=DM(cur);
M6=1;
L2=0;
L3=0;
L4=0;
// A broken guard means this call overran: mute its ID and rewrite the guard.
R13=DM(1,I2);
I4=R13;
R0=DM(0,I4);
R1=0x57ac6a2d;
COMP(R0,R1);
IF EQ JUMP leave;
DM(0,I4)=R1;
R2=DM(8,I2);
CALL muted;
R4=DM(0,I4);
R4=R4 OR R3;
DM(0,I4)=R4;

silence:
R1=DM(5,I2);
I4=R1;
R0=0;
R1=32;
clear:
DM(I4,M6)=R0;
R1=R1-1;
IF NE JUMP clear;
JUMP leave;

// A stock machine in this slot: the next custom one is new to it.
stock:
R4=I2;
R10=LSHIFT R0 BY 2;
R4=R4+R10;
R10=128;
R4=R4+R10;
I4=R4;
R4=0;
DM(0,I4)=R4;

leave:
R0=DM(3,I2);
R0=PASS R0;
IF NE JUMP filter_leave;
CALL restore;
I7=MODIFY(I7,-18)(NW);              // the instruction the hook displaced
JUMP 0xb8251e;
filter_leave:
CALL restore;
I4=0x8052dbc0;                      // the load the hook displaced
JUMP 0x1c958a;

// ID R2's muted word in I4 and its bit in R3; uses R4.
muted:
R4=I2;
R3=64;
R4=R4+R3;
R3=-5;
R3=LSHIFT R2 BY R3;
R3=LSHIFT R3 BY 2;
R4=R4+R3;
I4=R4;
R4=31;
R4=R2 AND R4;
R3=1;
R3=LSHIFT R3 BY R4;
RTS;

// R1 = the frame halfword of slot R0's first knob, 0x6d + 0x49 slot; uses R2.
first_knob:
R1=LSHIFT R0 BY 6;
R2=LSHIFT R0 BY 3;
R1=R1+R2;
R1=R1+R0;
R2=0x6d;
R1=R1+R2;
RTS;

// Frame halfword R1 into R2; uses R3, R6 and I4.
halfword:
R6=0x25c48c;
// Halfword R1 of the table at byte R6 into R2, unsigned; uses R3 and I4.
half:
R2=0xfffe;
R2=R1 AND R2;
R2=LSHIFT R2 BY 1;
R2=R2+R6;
I4=R2;
R2=DM(0,I4);
R3=1;
R3=R1 AND R3;
IF EQ JUMP half_low;
R3=-16;
R2=LSHIFT R2 BY R3;
half_low:
R3=0xffff;
R2=R2 AND R3;
RTS;

// The filter setup table's internal type 0 entry: every voice with a custom filter comes here
// with its final normalized cutoff in F8, resonance at DM(-22,I5), G (slot 66) at DM(-24,I5) and
// its slot in R3. Keep the three for the slot, then continue as stock. The continuation
// overwrites R2, R4 and I4 before reading them.
dn2_filter_capture.:
R2=ASTATX;
DM(cap_astat)=R2;
R2=15;
COMPU(R3,R2);
IF GT JUMP captured;
R2=LSHIFT R3 BY 2;
R4=cap;
R2=R2+R4;
I4=R2;
DM(0,I4)=R8;
R2=DM(-22,I5);
DM(16,I4)=R2;
R2=DM(-24,I5);
DM(32,I4)=R2;
captured:
R2=DM(cap_astat);
ASTATX=R2;
JUMP 0x1c927e;

// Save the context (full-width PEx registers through PX, before anything touches the flags),
// then pin the C context.
save:
DM(sv_i4)=I4;
I4=save_area;
DM(0,I4)=PX1;
DM(1,I4)=PX2;
PX=R0;
DM(32,I4)=PX1;
DM(33,I4)=PX2;
PX=R1;
DM(34,I4)=PX1;
DM(35,I4)=PX2;
PX=R2;
DM(36,I4)=PX1;
DM(37,I4)=PX2;
PX=R3;
DM(38,I4)=PX1;
DM(39,I4)=PX2;
PX=R4;
DM(40,I4)=PX1;
DM(41,I4)=PX2;
PX=R5;
DM(42,I4)=PX1;
DM(43,I4)=PX2;
PX=R6;
DM(44,I4)=PX1;
DM(45,I4)=PX2;
PX=R7;
DM(46,I4)=PX1;
DM(47,I4)=PX2;
PX=R8;
DM(48,I4)=PX1;
DM(49,I4)=PX2;
PX=R9;
DM(50,I4)=PX1;
DM(51,I4)=PX2;
PX=R10;
DM(52,I4)=PX1;
DM(53,I4)=PX2;
PX=R11;
DM(54,I4)=PX1;
DM(55,I4)=PX2;
PX=R12;
DM(56,I4)=PX1;
DM(57,I4)=PX2;
PX=R13;
DM(58,I4)=PX1;
DM(59,I4)=PX2;
PX=R14;
DM(60,I4)=PX1;
DM(61,I4)=PX2;
PX=R15;
DM(62,I4)=PX1;
DM(63,I4)=PX2;
DM(2,I4)=I0;
DM(3,I4)=I1;
DM(4,I4)=I2;
DM(5,I4)=I3;
DM(6,I4)=I5;
DM(7,I4)=I12;
DM(8,I4)=I13;
DM(9,I4)=M0;
DM(10,I4)=M1;
DM(11,I4)=M2;
DM(12,I4)=M3;
DM(13,I4)=M4;
DM(14,I4)=M5;
DM(15,I4)=M6;
DM(16,I4)=M7;
DM(17,I4)=M13;
DM(18,I4)=M14;
DM(19,I4)=M15;
DM(20,I4)=L0;
DM(21,I4)=L1;
DM(22,I4)=L2;
DM(23,I4)=L3;
DM(24,I4)=L4;
DM(25,I4)=L5;
DM(26,I4)=LCNTR;
DM(27,I4)=MODE1;
DM(28,I4)=ASTATX;
DM(29,I4)=ASTATY;
DM(30,I4)=STKYX;
DM(31,I4)=STKYY;
// Pin the C context, keeping the register bank and the interrupt setup.
M5=0;
M6=1;
M7=-1;
M13=0;
M14=1;
M15=-1;
L0=0;
L1=0;
L2=0;
L3=0;
L4=0;
L5=0;
BIT CLR MODE1 0x20A000;
BIT SET MODE1 0x10000;
NOP;
RTS;

// Restore the context saved on entry.
restore:
I4=save_area;
I0=DM(2,I4);
I1=DM(3,I4);
I2=DM(4,I4);
I3=DM(5,I4);
I5=DM(6,I4);
I12=DM(7,I4);
I13=DM(8,I4);
M0=DM(9,I4);
M1=DM(10,I4);
M2=DM(11,I4);
M3=DM(12,I4);
M4=DM(13,I4);
M5=DM(14,I4);
M6=DM(15,I4);
M7=DM(16,I4);
M13=DM(17,I4);
M14=DM(18,I4);
M15=DM(19,I4);
L0=DM(20,I4);
L1=DM(21,I4);
L2=DM(22,I4);
L3=DM(23,I4);
L4=DM(24,I4);
L5=DM(25,I4);
LCNTR=DM(26,I4);
MODE1=DM(27,I4);
ASTATX=DM(28,I4);
ASTATY=DM(29,I4);
STKYX=DM(30,I4);
STKYY=DM(31,I4);
NOP;
PX1=DM(34,I4);
PX2=DM(35,I4);
R1=PX;
PX1=DM(36,I4);
PX2=DM(37,I4);
R2=PX;
PX1=DM(38,I4);
PX2=DM(39,I4);
R3=PX;
PX1=DM(40,I4);
PX2=DM(41,I4);
R4=PX;
PX1=DM(42,I4);
PX2=DM(43,I4);
R5=PX;
PX1=DM(44,I4);
PX2=DM(45,I4);
R6=PX;
PX1=DM(46,I4);
PX2=DM(47,I4);
R7=PX;
PX1=DM(48,I4);
PX2=DM(49,I4);
R8=PX;
PX1=DM(50,I4);
PX2=DM(51,I4);
R9=PX;
PX1=DM(52,I4);
PX2=DM(53,I4);
R10=PX;
PX1=DM(54,I4);
PX2=DM(55,I4);
R11=PX;
PX1=DM(56,I4);
PX2=DM(57,I4);
R12=PX;
PX1=DM(58,I4);
PX2=DM(59,I4);
R13=PX;
PX1=DM(60,I4);
PX2=DM(61,I4);
R14=PX;
PX1=DM(62,I4);
PX2=DM(63,I4);
R15=PX;
PX1=DM(32,I4);
PX2=DM(33,I4);
R0=PX;
PX1=DM(0,I4);
PX2=DM(1,I4);
I4=DM(sv_i4);
RTS;
.ENDSEG;
