| SPDX-License-Identifier: GPL-2.0-or-later
| drive-loader (Analog Rytm MKII, OS 1.74): the /mods route's site, its
| RouteTypeHandler, the thunks between stock's calls and route.c, and the
| staging buffer's floor (STAGE.md).
|
| Stock passes a struct result's address in %a0 (hidden) and its arguments
| on the stack; a std::function's invoker gets the functor's address first.
| The thunks hand %a0 to route.c as an explicit first argument and give the
| result's address back in %d0 and %a0.

        .text
        .align  2
        .globl  dl_route_site, dl_list_inv, dl_entry_inv, dl_pre_inv, dl_hdr_inv, dl_call_a0

| 0x400a005e, in the resolver's getter (0x4009febc) after its handlers are
| added, over `moveq #1,%d0; move.b %d0,(0x419438c8).l` (8 bytes, the
| guard's set). %d0-%d1/%a0-%a1 are dead here.
dl_route_site:
        moveq   #1,%d0                          | the displaced guard set
        move.b  %d0,0x419438c8
        pea     dl_holder
        pea     0x419438cc                      | the resolver
        jsr     0x4002da4a                      | add(resolver, &shared_ptr): register_route, then a copy into the handler list
        addq.l  #8,%sp
        rts

| (functor, arg) -> C(result, functor, arg)
        .macro  thunk2 name, fn
\name:
        move.l  8(%sp),-(%sp)
        move.l  8(%sp),-(%sp)
        move.l  %a0,-(%sp)
        jsr     \fn
        lea     12(%sp),%sp
        movea.l %d0,%a0
        rts
        .endm

        thunk2  dl_list_inv, dl_list            | /mods: (functor, captures)
        thunk2  dl_entry_inv, dl_entry          | /mods/*: (functor, captures)
        thunk2  dl_pre_inv, dl_pre              | entry +0x1c: (functor, entry)
        thunk2  dl_hdr_inv, dl_hdr              | entry +0x6c: (functor, header)

| the handler's name and count (vtable +8): (this) -> %a0
dl_name_slot:
        move.l  %a0,-(%sp)
        jsr     dl_name
        addq.l  #4,%sp
        movea.l %d0,%a0
        rts

dl_nop:
        moveq   #0,%d0
        suba.l  %a0,%a0
        rts

| dl_call_a0(fn, result, a, b): call a stock routine that returns a struct
| through %a0, with two arguments
dl_call_a0:
        movea.l 8(%sp),%a0
        movea.l 4(%sp),%a1
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     (%a1)
        addq.l  #8,%sp
        rts

        .data
        .align  4
| a RouteTypeHandler: its vtable {dtor, deleting dtor, name and count,
| register_route}; stock never destroys a handler
dl_vtable:
        .long   dl_nop, dl_nop, dl_name_slot, dl_register_route
        .long   dl_nop, dl_nop, dl_nop, dl_nop  | spare slots that return 0
| stock's add takes a shared_ptr<RouteTypeHandler> {object, control block};
| this one owns nothing (no control block), as the handler lives forever
dl_holder:
        .long   dl_handler, 0
dl_handler:
        .long   dl_vtable
        .space  0x44                            | zeros, as large as stock's ProjectHandler

        .bss
        .align  16
        .globl  drive_staging
drive_staging:
        .space  0x10000                         | the floor (fs.h, DL_STAGING_SIZE)
        .globl  drive_staging_size
        .set    drive_staging_size, 0x10000
| the build descriptor's flags bit 0 (ABI 1.3): the boot reads each file into the +Drive area
| itself (dl.h, dl_stage_top), so host tools lay a set out as the boot does
        .globl  drive_stage_top
        .set    drive_stage_top, 1
