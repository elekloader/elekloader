| SPDX-License-Identifier: GPL-2.0-or-later
| core-a4mk2 0.1, mod storage (store.c): each hooked routine's first
| instructions (the bytes its jmp site replaces), then a jump to the next.
| Called like the stock routine. Analog Four MKII OS 1.56.
        .text
        .globl  st_sound_save_body, st_sound_load_body, st_pat_save_body, st_pat_load_body
        .globl  st_kit_save_body, st_kit_load_body, st_glob_save_body, st_glob_load_body
        .globl  st_pat_init_body, st_track_init_body, st_sound_init_body, st_kit_init_body
        .globl  st_proj_init_body, st_proj_load_body, st_proj_save_body
        .align  2
st_sound_save_body:                     | 0x400091bc
        lea     -20(%sp),%sp
        movem.l %d2/%a2-%a5,(%sp)
        jmp     0x400091c4
st_sound_load_body:                     | 0x40008808
        lea     -28(%sp),%sp
        movem.l %d2-%d3/%a2-%a6,(%sp)
        jmp     0x40008810
st_pat_save_body:                       | 0x4000a160
        lea     -20(%sp),%sp
        movem.l %d2-%d3/%a2-%a4,(%sp)
        jmp     0x4000a168
st_pat_load_body:                       | 0x40009f60
        lea     -32(%sp),%sp
        movem.l %d2-%d5/%a2-%a5,(%sp)
        jmp     0x40009f68
st_kit_save_body:                       | 0x4000962a
        lea     -28(%sp),%sp
        movem.l %d2-%d3/%a2-%a6,(%sp)
        jmp     0x40009632
st_kit_load_body:                       | 0x40008f2a
        lea     -40(%sp),%sp
        movem.l %d2-%d6/%a2-%a6,(%sp)
        jmp     0x40008f32
st_glob_save_body:                      | 0x4000837c
        lea     -12(%sp),%sp
        movem.l %d2/%a2-%a3,(%sp)
        jmp     0x40008384
st_glob_load_body:                      | 0x40007bc6
        lea     -24(%sp),%sp
        movem.l %d2-%d4/%a2-%a4,(%sp)
        jmp     0x40007bce
st_pat_init_body:                       | 0x40015d70
        lea     -32(%sp),%sp
        movem.l %d2-%d7/%a2-%a3,(%sp)
        jmp     0x40015d78
st_track_init_body:                     | 0x40015b74; the branch after it reads move's flags
        movea.l 4(%sp),%a0
        move.l  8(%sp),%d0
        jmp     0x40015b7c
st_sound_init_body:                     | 0x40016890
        lea     -28(%sp),%sp
        movem.l %d2/%a2-%a4,(%sp)
        jmp     0x40016898
st_kit_init_body:                       | 0x40015a58
        lea     -28(%sp),%sp
        movem.l %d2-%d4/%a2-%a5,(%sp)
        jmp     0x40015a60
st_proj_init_body:                      | 0x40016242
        lea     -24(%sp),%sp
        movem.l %d2-%d5/%a2-%a3,(%sp)
        jmp     0x4001624a
st_proj_load_body:                      | 0x40011054
        lea     -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x4001105c
st_proj_save_body:                      | 0x400a771a (0.6)
        lea     -48(%sp),%sp
        movem.l %d2-%d4/%a2-%a6,(%sp)
        jmp     0x400a7722
