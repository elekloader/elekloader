// SPDX-License-Identifier: GPL-2.0-or-later
// Two kernels sharing code: a data load, a pointer in data, a call and a jump to rebase.
.SECTION/DM seg_dmda;
.VAR level. = 0x3f000000;
.VAR self. = level.;
.SECTION/SW seg_swco;
.GLOBAL elek_voice_7.;
.GLOBAL elek_filter_9.;
elek_voice_7.:
    R0=DM(level.);
    CALL helper.;
    I12=DM(M7,I6);
    JUMP(M14,I12) (DB);
    RFRAME;
    NOP;
helper.:
    R1=self.;
    RTS;
elek_filter_9.:
    JUMP elek_voice_7.;
