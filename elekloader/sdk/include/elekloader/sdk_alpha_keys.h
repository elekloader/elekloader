/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Private: the companions' and sdk_alpha_host's, not a machine's. The keys held on one track and what a
 * mono machine's .play and .prio (sdk_alpha_machine.h) make of each press and release, as the Digitone
 * II's PLAY MODE and MONO NOTE PRIO do, so every device carries out the same decisions. */
#ifndef ELEKLOADER_SDK_ALPHA_KEYS_H
#define ELEKLOADER_SDK_ALPHA_KEYS_H

#include "elekloader/sdk_alpha_machine.h"

#define SDK_ALPHA_HELD_MAX 16           /* the keys a track keeps; a press past them drops the oldest */

/* A key still down: pitch as elek_voice's, velocity 1..127. */
struct sdk_alpha_held { int32_t pitch, velocity; };

/* One track's keys. Zeroed: none held, none sounding; zero it again when the track's machine changes. */
struct sdk_alpha_keys {
    struct sdk_alpha_held held[SDK_ALPHA_HELD_MAX];   /* in press order, oldest first; each pitch once */
    uint32_t count;
    uint32_t sounds;                    /* nonzero while a note sounds at `pitch` */
    int32_t pitch;
};

/* What the companion does after a key: */
enum {
    SDK_ALPHA_KEEP,                     /* nothing: the note sounding (or the silence) goes on */
    SDK_ALPHA_PLAY,                     /* play the key, the AMP restarted and start set */
    SDK_ALPHA_LEGATO,                   /* move the note sounding to the key's pitch, the AMP running on */
    SDK_ALPHA_SILENCE,                  /* release the note sounding */
};

/* The index in t's keys of `pitch`, or -1. */
static int32_t sdk_alpha_held(const struct sdk_alpha_keys *t, int32_t pitch)
{
    for (uint32_t i = 0; i < t->count; ++i)
        if (t->held[i].pitch == pitch)
            return (int32_t)i;
    return -1;
}

static void sdk_alpha_unhold(struct sdk_alpha_keys *t, uint32_t i)
{
    for (--t->count; i < t->count; ++i)
        t->held[i] = t->held[i + 1];
}

/* A press (pressed nonzero) or a release of the key `pitch` on a track whose machine m is mono: records
 * it and returns what to do, with the key for PLAY and LEGATO in *key. The key that sounds is the newest
 * held (ELEK_PRIO_LAST), the lowest or the highest; a change while a note sounds is LEGATO with
 * ELEK_MONO_LEGATO, else PLAY. A release of a key not held is KEEP. */
static int32_t sdk_alpha_key(struct sdk_alpha_keys *t, const struct elek_machine *m, int32_t pitch,
                             int32_t velocity, int pressed, struct sdk_alpha_held *key)
{
    int32_t at = sdk_alpha_held(t, pitch);
    if (at >= 0)
        sdk_alpha_unhold(t, (uint32_t)at);      /* a press again moves the key to the newest */
    else if (!pressed)
        return SDK_ALPHA_KEEP;
    if (pressed) {
        if (t->count == SDK_ALPHA_HELD_MAX)
            sdk_alpha_unhold(t, 0);
        t->held[t->count].pitch = pitch;
        t->held[t->count++].velocity = velocity;
    }
    if (!t->count) {
        if (!t->sounds)
            return SDK_ALPHA_KEEP;
        t->sounds = 0;
        return SDK_ALPHA_SILENCE;
    }
    uint32_t i = t->count - 1;
    for (uint32_t j = 0; m->prio != ELEK_PRIO_LAST && j < t->count; ++j)
        if (m->prio == ELEK_PRIO_LOW ? t->held[j].pitch < t->held[i].pitch : t->held[j].pitch > t->held[i].pitch)
            i = j;
    *key = t->held[i];
    /* the key sounding stays, unless it is the one just pressed again */
    if (t->sounds && key->pitch == t->pitch && !(pressed && i == t->count - 1))
        return SDK_ALPHA_KEEP;
    int32_t legato = t->sounds && m->play == ELEK_MONO_LEGATO;
    t->sounds = 1;
    t->pitch = key->pitch;
    return legato ? SDK_ALPHA_LEGATO : SDK_ALPHA_PLAY;
}
#endif
