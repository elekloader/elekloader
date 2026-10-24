/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * core-ot 0.8: the edits a mod's own data must follow (docs/ADAPTING.md, "Edits, Parts and the screen
 * on the Octatrack"; the declarations are in elekloader/sdk/include/octatrack/core.h, the sites in edit.s).
 *
 * - ev_clip: a pattern or an audio track between the current bank and the clipboard or the undo
 *   buffer, before and after; a PASTE or an UNDO may be refused.
 * - ev_part_select: the panel's Part change, in three steps.
 *
 * The routines and what their arguments are come from octabam's KITS, PLOCKS P2, VARISPEED, CXOSC,
 * BATCH BUGFIXES and MIDI SCENES (MIT), and were read again from the OS 1.40C image; the code is
 * our own.
 */
#include <stdint.h>
#include "octatrack/core.h"

#define U8(a)           (*(volatile uint8_t *)(uintptr_t)(a))
#define U32(a)          (*(volatile uint32_t *)(uintptr_t)(a))

#define UNDO_OP         U32(0x460c80f4)     /* the undo buffer's descriptor: [0] why, [1] pattern, [2] track */

/* edit.s's sites */
enum { CL_PCOPY, CL_PSNAP, CL_PSTORE, CL_TCOPY, CL_TSTORE };

/* Where a store's refusal may go: the return addresses of stock's own paste and undo calls of
 * the two store routines (edit.s takes the "nothing done" exits of exactly these). */
#define PSTORE_UNDO_RET 0x4002ba82u
#define PSTORE_PASTE_RET 0x4002bab8u
#define TSTORE_UNDO_RET 0x4002b96cu
#define TSTORE_PASTE_RET 0x4002b9a8u

extern int (*const ev_clip[])(unsigned, const struct ot_clip *);
extern void (*const ev_part_select[])(unsigned, const struct ot_part_select *);

static struct ot_clip clip;
static struct ot_part_select psel;

/* edit.s: before (pre 1) or after (0) one of the five routines; args -> its first argument, the
 * return address the long below it. Returns nonzero when a handler refuses a PASTE or an UNDO
 * that stock itself asked for. */
int ote_clip(unsigned site, unsigned pre, const uint32_t *args)
{
    if (pre) {
        uint32_t ret = args[-1];
        int may = 0;
        clip.bank = U8(0x80000002);
        clip.op = 0;
        switch (site) {
        case CL_PCOPY:                      /* (pattern) */
            clip.what = OT_CLIP_PATTERN, clip.how = OT_CLIP_COPY;
            clip.pattern = (uint8_t)args[0], clip.track = 0xff, clip.buf = OT_CLIPBOARD;
            break;
        case CL_PSNAP:                      /* (op, pattern): the descriptor is written next */
            clip.what = OT_CLIP_PATTERN, clip.how = OT_CLIP_SNAPSHOT, clip.op = (uint8_t)args[0];
            clip.pattern = (uint8_t)args[1], clip.track = 0xff, clip.buf = OT_UNDO_BUFFER;
            break;
        case CL_PSTORE:                     /* (src, pattern) */
            clip.what = OT_CLIP_PATTERN, clip.buf = (void *)args[0];
            clip.pattern = (uint8_t)args[1], clip.track = 0xff;
            may = ret == PSTORE_UNDO_RET || ret == PSTORE_PASTE_RET;
            break;
        case CL_TCOPY:                      /* (dst, pattern, track) */
        case CL_TSTORE:                     /* (src, pattern, track) */
            clip.what = OT_CLIP_TRACK, clip.buf = (void *)args[0];
            clip.pattern = (uint8_t)args[1], clip.track = (uint8_t)args[2];
            may = site == CL_TSTORE && (ret == TSTORE_UNDO_RET || ret == TSTORE_PASTE_RET);
            break;
        default:
            return 0;
        }
        if (site == CL_PSTORE || site == CL_TSTORE)
            clip.how = clip.buf == OT_UNDO_BUFFER ? OT_CLIP_UNDO : OT_CLIP_PASTE;
        else if (site == CL_TCOPY)
            clip.how = clip.buf == OT_UNDO_BUFFER ? OT_CLIP_SNAPSHOT : OT_CLIP_COPY;
        if (clip.how == OT_CLIP_UNDO || (site == CL_TCOPY && clip.how == OT_CLIP_SNAPSHOT))
            clip.op = (uint8_t)UNDO_OP;
        for (int (*const *f)(unsigned, const struct ot_clip *) = ev_clip; *f; ++f)
            if ((*f)(1, &clip) && may)
                return 1;
        return 0;
    }
    for (int (*const *f)(unsigned, const struct ot_clip *) = ev_clip; *f; ++f)
        (*f)(0, &clip);
    return 0;
}

/* edit.s: the Part change's steps; `to` at BEGIN only, the old Part's machine bytes after it. */
void ote_psel(unsigned phase, unsigned to, const int8_t *machines)
{
    if (phase == OT_PSEL_BEGIN) {
        psel.from = U8(0x80000003);
        psel.to = (uint8_t)to;
        psel.machines = 0;
    } else {
        psel.machines = machines;
    }
    for (void (*const *f)(unsigned, const struct ot_part_select *) = ev_part_select; *f; ++f)
        (*f)(phase, &psel);
}
