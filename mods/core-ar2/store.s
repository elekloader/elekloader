| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.2, mod storage (store.c): each hooked routine's first
| instructions (the bytes its jmp site replaces), then a jump to the next.
| Called like the stock routine. The same in OS 1.73 (core 0.2) and 1.74.
        .text
        .globl  st_sound_save_body, st_sound_load_body, st_pat_save_body, st_pat_load_body
        .globl  st_kit_save_body, st_kit_load_body, st_glob_save_body, st_glob_load_body
        .globl  st_pat_init_body, st_track_init_body, st_sound_init_body, st_proj_init_body
        .globl  st_proj_load_body
        .align  2
st_sound_save_body:                     | 0x4000843a
        lea     -24(%sp),%sp
        movem.l %d2-%d4/%a2-%a4,(%sp)
        jmp     0x40008442
st_sound_load_body:                     | 0x400081f6
        lea     -24(%sp),%sp
        movem.l %d2-%d3/%a2-%a5,(%sp)
        jmp     0x400081fe
st_pat_save_body:                       | 0x4000937c
        lea     -24(%sp),%sp
        movem.l %d2-%d4/%a2-%a4,(%sp)
        jmp     0x40009384
st_pat_load_body:                       | 0x40009146
        lea     -32(%sp),%sp
        movem.l %d2-%d4/%a2-%a6,(%sp)
        jmp     0x4000914e
st_kit_save_body:                       | 0x400088d6
        lea     -28(%sp),%sp
        movem.l %d2-%d4/%a2-%a5,(%sp)
        jmp     0x400088de
st_kit_load_body:                       | 0x4000863e
        lea     -36(%sp),%sp
        movem.l %d2-%d5/%a2-%a6,(%sp)
        jmp     0x40008646
st_glob_save_body:                      | 0x40007a5c
        lea     -16(%sp),%sp
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp     0x40007a64
st_glob_load_body:                      | 0x4000763c
        lea     -16(%sp),%sp
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp     0x40007644
st_pat_init_body:                       | 0x40011ab8
        lea     -32(%sp),%sp
        movem.l %d2-%d7/%a2-%a3,(%sp)
        jmp     0x40011ac0
st_track_init_body:                     | 0x400118e8; the beq after it reads tst's flags
        movea.l 4(%sp),%a0
        tst.l   %a0
        jmp     0x400118ee
st_sound_init_body:                     | 0x400122b6
        lea     -40(%sp),%sp
        movem.l %d2-%d4/%a2-%a5,(%sp)
        jmp     0x400122be
st_proj_init_body:                      | 0x40011d38
        lea     -24(%sp),%sp
        movem.l %d2-%d5/%a2-%a3,(%sp)
        jmp     0x40011d40
st_proj_load_body:                      | 0x4000cd2e
        lea     -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x4000cd36
