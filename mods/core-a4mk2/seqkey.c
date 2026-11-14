/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.8: ev_seq_key, a native event (analog-four-mk2/ui.h), and
 * its view's helpers. OS 1.56; the site is in seqkey.s and mod.json.
 *
 *   ev_seq_key   (native)   TrackSelectionView's key virtual (vtable slot 2,
 *                           0x400c8b90; one instance, built by the UI
 *                           manager's builder 0x400c1366), at its key-code
 *                           read (0x400c8ba2); UI task
 *
 * The helpers read what that handler's own code reads: the view's +104 (a
 * singleton whose +48 gives the current track) and +108 (the track keys'
 * singleton, its +0x1b0 the held mask: bit k = track k, set on key 0x20 + k's
 * press, cleared on its release), through stock's routines. */

#include <stdint.h>
#include "analog-four-mk2/ui.h"

extern const ev_seq_key_fn ev_seq_key[];

#define KEY_IS_PRESS ((uint8_t (*)(void *))0x4005b75eu)        /* event +16 bit 0 */
#define HELD_TRACKS ((uint32_t (*)(void *))0x40022014u)       /* (view +108): its +0x1b0 */
#define TRACK_OF ((int32_t (*)(void *))0x400adbd2u)           /* (view +104) + 48 */
/* stock returns pointers in d0, where this compiler looks for an int */
#define TRACK_PATTERN ((uintptr_t (*)(void *))0x400a5f80u)    /* (view +104): the track clamped to 0..5 */
#define VIEW_SEQ 104
#define VIEW_KEYS 108

static void *field(void *view, unsigned off)
{
    return *(void **)((uint8_t *)view + off);
}

int core_seq_key(void *view, void *event, int32_t key)
{
    const ev_seq_key_fn *f;
    int pressed = KEY_IS_PRESS(event) ? 1 : 0;
    for (f = ev_seq_key; *f; f++)
        if ((*f)(view, event, (int)key, pressed))
            return 1;
    return 0;
}

unsigned core_seq_held_tracks(void *view)
{
    return view ? HELD_TRACKS(field(view, VIEW_KEYS)) : 0;
}

int core_seq_track(void *view)
{
    void *seq = view ? field(view, VIEW_SEQ) : 0;
    int32_t t = seq ? TRACK_OF((uint8_t *)seq + 48) : -1;
    return (uint32_t)t <= 5 ? (int)t : -1;     /* stock's 0x400adbd2 does not range-check it */
}

void *core_seq_track_pattern(void *view)
{
    void *seq = view ? field(view, VIEW_SEQ) : 0;
    return seq ? (void *)TRACK_PATTERN(seq) : 0;
}
