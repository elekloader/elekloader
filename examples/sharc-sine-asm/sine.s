// SPDX-License-Identifier: GPL-2.0-or-later
// Same 64-phase table and uint32 phase accumulator as the C sine.
.SECTION/SW seg_swco;
.GLOBAL sine_init.;
.GLOBAL sine_sample.;
.GLOBAL sine_render.;

sine_init.:
    R0=0;
    DM(phase.)=R0;
    DM(increment.)=R4;
    I12=DM(M7,I6);
    JUMP(M14,I12) (DB);
    RFRAME;
    NOP;

sine_sample.:
    R7=-24;
    R1=0x7c;
    R3=LSHIFT R4 BY R7;
    R3=R3 AND R1;
    R1=table.;
    R3=R3+R1;
    I4=R3;
    R0=DM(I4,M5);
    R1=0x80000000;
    R1=R4 AND R1;
    R0=R0 XOR R1;
    I12=DM(M7,I6);
    JUMP(M14,I12) (DB);
    RFRAME;
    NOP;

sine_render.:
    R0=DM(phase.);
    R1=DM(increment.);
    R2=table.;
    R3=32;
    R7=-24;
render_sample:
    R5=LSHIFT R0 BY R7;
    R6=0x7c;
    R5=R5 AND R6;
    R5=R5+R2;
    I4=R5;
    R5=DM(I4,M5);
    R6=0x80000000;
    R6=R0 AND R6;
    R5=R5 XOR R6;
    I4=R4;
    DM(I4,M5)=R5;
    R6=4;
    R4=R4+R6;
    R0=R0+R1;
    R3=R3-1;
    IF NE JUMP render_sample;
    DM(phase.)=R0;
    I12=DM(M7,I6);
    JUMP(M14,I12) (DB);
    RFRAME;
    NOP;

.SECTION/DOUBLE32 seg_dmda;
.VAR table. = 0x00000000;
.VAR = 0x3c48bd36;
.VAR = 0x3cc7c5c2;
.VAR = 0x3d14a032;
.VAR = 0x3d43ef15;
.VAR = 0x3d715aea;
.VAR = 0x3d8e39da;
.VAR = 0x3da26799;
.VAR = 0x3db504f3;
.VAR = 0x3dc5e403;
.VAR = 0x3dd4db31;
.VAR = 0x3de1c598;
.VAR = 0x3dec835f;
.VAR = 0x3df4fa0b;
.VAR = 0x3dfb14be;
.VAR = 0x3dfec46d;
.VAR = 0x3e000000;
.VAR = 0x3dfec46d;
.VAR = 0x3dfb14be;
.VAR = 0x3df4fa0b;
.VAR = 0x3dec835f;
.VAR = 0x3de1c598;
.VAR = 0x3dd4db31;
.VAR = 0x3dc5e403;
.VAR = 0x3db504f3;
.VAR = 0x3da26799;
.VAR = 0x3d8e39da;
.VAR = 0x3d715aea;
.VAR = 0x3d43ef15;
.VAR = 0x3d14a032;
.VAR = 0x3cc7c5c2;
.VAR = 0x3c48bd36;
.GLOBAL sine_widths.;
.VAR sine_widths. = 1;
.VAR = 4;
.VAR = 4;
.VAR = 4;
.VAR phase. = 0;
.VAR increment. = 0;
