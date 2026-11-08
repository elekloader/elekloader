// SPDX-License-Identifier: GPL-2.0-or-later
// SDK Sine's sound on the Digitone II's SHARC: sine.c's C render, step for step in integers, so the
// samples are the same. sine.c declares it under ELEK_TARGET_SHARC:
//   void sine_render(const struct elek_voice *v, elek_sample *out);
// R4 = v: +0 knob (FINE, cents, at knob[0]), +8 start, +12 pitch (16.16), +20 state, +28 cc (each CC's
// value since the last block, or -1); R8 = out (32 floats).
// The state's first word is the phase, 2^32 a cycle. It writes only R0, R1, R3-R7, I4 and I12 (the C
// ABI's scratch registers), and reads its constants from its own data.
.SECTION/DM seg_dmda;
.VAR sine_cents. = 0x4423d70a;      // 655.36: FINE * 65536 / 100
.VAR sine_steps. = 731558;          // elek_phase_inc's table: note 0 upward, a quarter semitone apart
.VAR = 742199;
.VAR = 752994;
.VAR = 763947;
.VAR = 775059;
.VAR = 786332;
.VAR = 797770;
.VAR = 809374;
.VAR = 821146;
.VAR = 833090;
.VAR = 845208;
.VAR = 857501;
.VAR = 869974;
.VAR = 882628;
.VAR = 895466;
.VAR = 908491;
.VAR = 921705;
.VAR = 935112;
.VAR = 948713;
.VAR = 962513;
.VAR = 976513;
.VAR = 990717;
.VAR = 1005127;
.VAR = 1019747;
.VAR = 1034579;
.VAR = 1049628;
.VAR = 1064895;
.VAR = 1080384;
.VAR = 1096099;
.VAR = 1112042;
.VAR = 1128217;
.VAR = 1144627;
.VAR = 1161276;
.VAR = 1178167;
.VAR = 1195304;
.VAR = 1212690;
.VAR = 1230329;
.VAR = 1248225;
.VAR = 1266381;
.VAR = 1284800;
.VAR = 1303488;
.VAR = 1322448;
.VAR = 1341683;
.VAR = 1361199;
.VAR = 1380998;
.VAR = 1401085;
.VAR = 1421464;
.VAR = 1442140;
.VAR = 1463116;

.SECTION/SW seg_swco;
.GLOBAL sine_render.;
sine_render.:
    I4=R4;
    R0=DM(I4,M5);                   // knob
    R1=8;
    R1=R4+R1;
    I4=R1;
    R3=DM(I4,M5);                   // start
    // cc[9] >= 0, CC 9 since the last block, restarts the wave as a note's start does.
    R1=28;
    R1=R4+R1;
    I4=R1;
    R1=DM(I4,M5);                   // cc
    R5=36;
    R1=R1+R5;
    I4=R1;
    R1=DM(I4,M5);                   // cc[9]
    R1=PASS R1;
    IF LT JUMP sine_cc_done;        // -1: none came
    R3=1;
sine_cc_done:
    R1=12;
    R1=R4+R1;
    I4=R1;
    R5=DM(I4,M5);                   // pitch
    R1=20;
    R1=R4+R1;
    I4=R1;
    R6=DM(I4,M5);
    I12=R6;                         // state: I12 holds it until the return loads I12
    I4=R0;
    R0=DM(I4,M5);                   // FINE
    F0=FLOAT R0;
    F1=DM(sine_cents.);
    F0=F0*F1;
    R0=TRUNC F0;                    // FINE * 65536 / 100, toward zero as C divides (exact: |FINE| <= 50)
    R5=R5+R0;
    R0=0;                           // elek_phase_inc: notes 0..127
    R5=MAX(R5,R0);
    R0=0x7f0000;
    R5=MIN(R5,R0);
    R1=-14;                         // (shifts by a register: Selache mis-encodes a negative immediate)
    R0=LSHIFT R5 BY R1;             // q, quarter semitones
    R1=0x3fff;
    R5=R5 AND R1;                   // f
    R1=1366;
    R1=R0*R1 (SSI);
    R7=-16;
    R1=LSHIFT R1 BY R7;             // q / 48, exact for q <= 508
    R7=48;
    R7=R1*R7 (SSI);
    R0=R0-R7;                       // q % 48
    R0=LSHIFT R0 BY 2;
    R7=sine_steps.;
    R0=R0+R7;
    I4=R0;
    R7=DM(I4,M5);                   // step[k]
    R4=4;
    R0=R0+R4;
    I4=R0;
    R4=DM(I4,M5);                   // step[k + 1]
    R4=R4-R7;
    R4=R4*R5 (SSI);
    R0=-14;
    R4=LSHIFT R4 BY R0;
    R7=R7+R4;
    R7=LSHIFT R7 BY R1;             // inc, a sample
    R1=0;                           // each note, and CC 9, start the wave at zero
    R3=PASS R3;
    IF NE JUMP sine_go;
    I4=R6;
    R1=DM(I4,M5);                   // else the phase the last block left
sine_go:
    R4=R8;                          // out
    R5=32;
sine_next:
    R6=-16;                         // (ASHIFT by a register: Selache encodes ASHIFT by an immediate as OR LSHIFT)
    R0=ASHIFT R1 BY R6;             // x = (int32_t)p >> 16
    R3=ABS R0;
    R6=32768;
    R3=R6-R3;
    R0=R0*R3 (SSI);
    R6=-13;
    R0=ASHIFT R0 BY R6;             // y = (x * (32768 - |x|)) >> 13, a parabola
    R3=ABS R0;
    R3=R0*R3 (SSI);
    R6=-15;
    R3=ASHIFT R3 BY R6;
    R3=R3-R0;
    R6=7373;
    R3=R3*R6 (SSI);
    R6=-15;
    R3=ASHIFT R3 BY R6;
    R0=R0+R3;                       // y += (7373 * ((y * |y| >> 15) - y)) >> 15
    R6=32767;
    R0=MIN(R0,R6);
    R6=-32767;
    R0=MAX(R0,R6);
    R6=-16;
    F0=FLOAT R0 BY R6;              // y / 32768 at half scale: headroom for the filter's resonance
    I4=R4;
    DM(I4,M5)=F0;
    R6=4;
    R4=R4+R6;
    R1=R1+R7;
    R5=R5-1;
    IF NE JUMP sine_next;
    R0=I12;
    I4=R0;
    DM(I4,M5)=R1;                   // the phase, for the next block
    I12=DM(M7,I6);
    JUMP(M14,I12) (DB);
    RFRAME;
    NOP;
