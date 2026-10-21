| SPDX-License-Identifier: GPL-2.0-or-later
| core-ar2 0.3, mod storage (store.c): the sound scope's room in the
| velocity routes.
|
| A live sound (0x98 B) holds four velocity routes at +0x70, 3 B each: a
| big-endian amount word, then the destination. The amount is the word's high
| byte; stock only ever writes the low byte as 0 (the route load's byte << 8,
| the defaults, the menu's whole steps) and saves only the high byte, so the
| four low bytes (+0x71, +0x74, +0x77, +0x7a) are the sound scope's bytes 3..6
| (store.c, sound_at). Stock reads them in only two places that matter, the
| push to the engine and the VELOCITY MOD menu's getAmount; there they read as
| 0 again. The menu's setAmount and setRoute (paste, undo) would write them,
| so they keep them. Both menus' setRoute store only the amount's high byte:
| a route copied from a velocity route into an aftertouch route (+0x8c) keeps
| the aftertouch low byte at 0, as stock has it. The menu's reset keeps them
| (store.c, st_vel_defaults); the sound save, load and init copy them.
|
| Each stub is a jmp site's displaced instructions, reimplemented; the
| resume addresses and SOUND_CHANGED (SoundConfigChangedInfo's vtable) come
| from mod.json's defsym (OS 1.74).
        .text
        .globl  st_vel_push, st_vel_get, st_vel_set, st_vel_route, st_at_route
        .align  2

| The push to the engine (0x4011f6a8): `movew %a1@(112),%a0@(1216)`,
| the velocity route's amount word to the engine's copy. d3 is free: it is
| set again two instructions on.
st_vel_push:
        move.w  112(%a1),%d3
        and.l   #0xffffff00,%d3
        move.w  %d3,1216(%a0)
        jmp     VEL_PUSH_RET

| getAmount (0x400c5326): `lea %a3@(0,%a3:l:2),%a0; addql #4,%sp`, then the
| read of route a3's amount word at routes d0 and the branch to the exit.
st_vel_get:
        lea     (0,%a3,%a3.l*2),%a0
        addq.l  #4,%sp
        mvs.w   (%a0,%d0.l),%d0
        and.l   #0xffffff00,%d0
        jmp     VEL_GET_RET

| setAmount (0x400c52b2): `movew %d1,%a0@(0,%d0:l); movel #SOUND_CHANGED,%d0`
| with d1 the new (clamped, whole-step) amount; the route's low byte goes
| back into it first, with interrupts masked from its read to the write, as
| core_store_set writes it: a field the audio interrupt sets meanwhile is
| not lost.
st_vel_set:
        lea     -8(%sp),%sp
        movem.l %d2-%d3,(%sp)
        move.w  %sr,%d3
        move.w  #0x2700,%sr
        mvz.b   1(%a0,%d0.l),%d2
        and.l   #0xffffff00,%d1
        or.l    %d2,%d1
        move.w  %d1,(%a0,%d0.l)
        move.w  %d3,%sr
        movem.l (%sp),%d2-%d3
        lea     8(%sp),%sp
        move.l  #SOUND_CHANGED,%d0      | and its CCR, as stock
        jmp     VEL_SET_RET

| setRoute (0x400c51b4 velocity, 0x4003391e aftertouch): `movew
| %a1@,%a0@(0,%d0:l); lea %fp@(19),%a1`, a whole route's amount word from a
| copy at a1. Only its high byte is stored: the route keeps its low byte.
st_vel_route:
        move.b  (%a1),(%a0,%d0.l)
        lea     19(%fp),%a1
        jmp     VEL_ROUTE_RET

st_at_route:
        move.b  (%a1),(%a0,%d0.l)
        lea     19(%fp),%a1
        jmp     AT_ROUTE_RET
