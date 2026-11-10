// SPDX-License-Identifier: GPL-2.0-or-later
// tests/test_dn2_dispatch.py's kernels: each records what the dispatcher gave it in probe_out.:
// words 0..6 its input, 7 R8, 8 the state's first word before the call (0xdead: no state), 9 the
// calls, 16..55 the array its input points at (an SDK machine's knob, an mk2 synth's param); an SDK
// machine's also 56 its input word 7 (cc) and 59 cc[9].
// Then it marks the state and writes 1.0 to the first sample. The overrun kernels break their stack's
// guard.
.SECTION/DM seg_dmda;
.GLOBAL probe_out.;
.GLOBAL sharc_voice_state_bytes.;
.GLOBAL sharc_filter_state_bytes.;
.VAR sharc_voice_state_bytes. = 12;
.VAR sharc_filter_state_bytes. = 8;
.VAR probe_out. = 0;
.ALIGN 66;

.SECTION/SW seg_swco;
.GLOBAL probe_sdk.;
.GLOBAL sharc_voice_render.;
.GLOBAL sharc_filter_render.;
.GLOBAL probe_overrun_voice.;
.GLOBAL probe_overrun_filter.;
probe_sdk.:
I0=R4;
R0=probe_out.;
R1=224;
R0=R0+R1;
I1=R0;                              // probe_out word 56
R0=DM(7,I0);                        // cc
DM(0,I1)=R0;
I2=R0;
R0=DM(9,I2);                        // cc[9]: the value, or -1
DM(3,I1)=R0;
R9=0;
R10=5;
JUMP probe;
sharc_voice_render.:
R9=4;
R10=5;
JUMP probe;
sharc_filter_render.:
R9=-1;
R10=6;
probe:
I0=R4;
I1=probe_out.;
R1=7;
copy:
R0=DM(I0,M6);
DM(I1,M6)=R0;
R1=R1-1;
IF NE JUMP copy;
I1=probe_out.;
DM(7,I1)=R8;
R9=PASS R9;
IF LT JUMP pointed;
R2=LSHIFT R9 BY 2;
R2=R2+R4;
I0=R2;
R0=DM(0,I0);
I0=R0;
R2=probe_out.;
R3=64;
R2=R2+R3;
I1=R2;
R1=40;
array:
R0=DM(I0,M6);
DM(I1,M6)=R0;
R1=R1-1;
IF NE JUMP array;
pointed:
I1=probe_out.;
R0=0xdead;
DM(8,I1)=R0;
R2=LSHIFT R10 BY 2;
R2=R2+R4;
I0=R2;
R0=DM(0,I0);
R0=PASS R0;
IF EQ JUMP stateless;
I0=R0;
R1=DM(0,I0);
DM(8,I1)=R1;
R1=0x5a5a5a5a;
DM(0,I0)=R1;
stateless:
R1=DM(9,I1);
R1=R1+1;
DM(9,I1)=R1;
I0=R8;
R0=0x3f800000;
DM(0,I0)=R0;
I12=DM(M7,I6);
JUMP(M14,I12)(DB);
RFRAME;
NOP;
probe_overrun_voice.:
R0=0;
DM(0x2de000)=R0;
I12=DM(M7,I6);
JUMP(M14,I12)(DB);
RFRAME;
NOP;
probe_overrun_filter.:
R0=0;
DM(0x2df000)=R0;
I12=DM(M7,I6);
JUMP(M14,I12)(DB);
RFRAME;
NOP;
.ENDSEG;
