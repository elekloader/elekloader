/* SPDX-License-Identifier: GPL-2.0-or-later */
/* A probe for ev_step_built (tests/probe_octatrack_step_built.py): one handler that logs what it is
 * given and, by the configuration `bp_cfg` the probe pokes into the image's RAM before the sequencer
 * starts, does what PLOCKS P2, MIDI SCENES and REC TRIG MUTE do at the builders, on the record the
 * builder staged. Nothing when the configuration is zero. Not a mod to ship. */
#include "octatrack/seq.h"

struct cfg {
    uint32_t log;       /* the tracks whose builds are logged: bit t */
    uint32_t mask;      /* the tracks the edits below act on: bit t */
    int32_t cancel;     /* 1: *kind = 0 on a build that staged a record (the step plays nothing) */
    int32_t shift;      /* *time += shift on a staged record */
    int32_t lock;       /* != 0: the lock byte AMP VOL (audio, slot 15) or VEL (MIDI, slot 1) = lock on a staged record */
    int32_t mute;       /* 1: *recorder = 0 (REC TRIG MUTE) */
    int32_t scenes;     /* 1: a MIDI track's staged row is copied to bp_snap first (MIDI SCENES) */
    int32_t p2;         /* 1: an audio track's staged record fills bp_p2pend/bp_p2stage from a table (PLOCKS P2) */
    int32_t rtime;      /* *rec_time += rtime on a build with a recorder trig */
    int32_t nest;       /* != 0: a build with n >= 0 calls the stock audio builder for T8, step 1 (no trig: nothing is
                         * staged), n = -1, from inside its handler: a build nested in a build */
};
struct cfg bp_cfg;
/* calls; with a staged record; with a recorder trig; with n < 0; nested builds started; their events seen; of
 * which with the arguments they were started with; and without */
uint32_t bp_calls[8];

#define LOGMAX 64
struct ent {
    int8_t track, bank, pattern, step, n, seen, pad[2];  /* seen: the pattern's own byte for AMP VOL / VEL at the step */
    uint32_t kind, length, span;
    int32_t time;
    uint32_t recorder;
    int32_t rec_time;
    uint8_t locks[32];
    int8_t deltas[32];
    uint32_t ptr[7];   /* locks, deltas, kind, length, span, time, recorder: where rec pointed */
};
struct ent bp_log[LOGMAX];
uint32_t bp_n;

uint8_t bp_p2pend[3][8][12], bp_p2stage[8][12];   /* PLOCKS P2's P2PEND and P2STAGE */
uint8_t bp_snap[8][32];                           /* MIDI SCENES' snapshot of a staged MIDI row */

static const uint8_t *stored(int32_t track, int32_t bank, int32_t pattern, int32_t step)
{
    const uint8_t *pat = (const uint8_t *)(0x400e21e0u + 0x9b340u * (uint32_t)bank + 0x8ed8u * (uint32_t)pattern);
    return track < SQ_AUDIO ? pat + 0x91au * (uint32_t)track + 0x59 + 32 * step
                            : pat + 0x48d0 + 0x8b0u * (uint32_t)(track - SQ_AUDIO) + 0x30 + 32 * step;
}

typedef void (*builder_fn)(int32_t track, int32_t bank, int32_t pattern, int32_t step, int32_t n);
static volatile int32_t depth;

void bp_built(int32_t track, int32_t bank, int32_t pattern, int32_t step, int32_t n, const struct sq_built *rec)
{
    int32_t i, on = (bp_cfg.mask >> track) & 1;
    struct ent *e;
    uint8_t *row;
    bp_calls[0]++;
    if (*rec->kind)
        bp_calls[1]++;
    if (rec->recorder && *rec->recorder)
        bp_calls[2]++;
    if (n < 0)
        bp_calls[3]++;
    if (((bp_cfg.log >> track) & 1) && bp_n < LOGMAX) {
        e = &bp_log[bp_n++];
        e->track = (int8_t)track;
        e->bank = (int8_t)bank;
        e->pattern = (int8_t)pattern;
        e->step = (int8_t)step;
        e->n = (int8_t)n;
        e->seen = (int8_t)stored(track, bank, pattern, step)[track < SQ_AUDIO ? 15 : 1];
        e->kind = *rec->kind;
        e->length = *rec->length;
        e->span = (uint32_t)*rec->span;
        e->time = *rec->time;
        e->recorder = rec->recorder ? *rec->recorder : 0;
        e->rec_time = rec->rec_time ? *rec->rec_time : 0;
        for (i = 0; i < 32; i++) {
            e->locks[i] = rec->locks[i];
            e->deltas[i] = rec->deltas ? rec->deltas[i] : 0;
        }
        e->ptr[0] = (uint32_t)rec->locks;
        e->ptr[1] = (uint32_t)rec->deltas;
        e->ptr[2] = (uint32_t)rec->kind;
        e->ptr[3] = (uint32_t)rec->length;
        e->ptr[4] = (uint32_t)rec->span;
        e->ptr[5] = (uint32_t)rec->time;
        e->ptr[6] = (uint32_t)rec->recorder;
    }
    if (depth) {                        /* the event of the build this handler started */
        bp_calls[5]++;
        bp_calls[track == 7 && bank == 0 && pattern == 0 && step == 1 && n == -1 ? 6 : 7]++;
        return;
    }
    if (!on)
        return;
    if (bp_cfg.p2 && *rec->kind && track < SQ_AUDIO) {
        row = n < 0 ? bp_p2stage[track] : bp_p2pend[n][track];
        for (i = 0; i < 12; i++)
            row[i] = (uint8_t)(bank * 37 + pattern * 11 + track * 7 + step * 3 + i);
    }
    if (bp_cfg.scenes && *rec->kind && track >= SQ_AUDIO)
        for (i = 0; i < 32; i++)
            bp_snap[track - SQ_AUDIO][i] = rec->locks[i];
    if (bp_cfg.lock && *rec->kind)
        rec->locks[track < SQ_AUDIO ? 15 : 1] = (uint8_t)bp_cfg.lock;
    if (bp_cfg.shift && *rec->kind)
        *rec->time += bp_cfg.shift;
    if (bp_cfg.rtime && rec->recorder && *rec->recorder)
        *rec->rec_time += bp_cfg.rtime;
    if (bp_cfg.mute && rec->recorder)
        *rec->recorder = 0;
    if (bp_cfg.cancel && *rec->kind)
        *rec->kind = 0;
    if (bp_cfg.nest && n >= 0) {
        depth = 1;
        bp_calls[4]++;
        ((builder_fn)0x4009d1e8)(7, 0, 0, 1, -1);
        depth = 0;
    }
}
