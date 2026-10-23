/* SPDX-License-Identifier: GPL-2.0-or-later */
/* seq-events' device layer for the Octatrack, OS 1.40C: what the shared part (../seq-events/seq.c)
 * needs to know of the firmware, as static inline functions so the one translation unit inlines
 * them. The Octatrack does not fire a step from the pattern: its sequencer builds each step's record
 * one step ahead (fire.c), from masks, lock bytes and a timing halfword of the pattern. A "step
 * word" here is the step's trig bits, read from the masks; the notes and the locks are the step's
 * 32 lock bytes. The offsets are the firmware's (octabam's docs/firmware/STEP_LOCKS.md and
 * PARTS.md, and the two builders themselves, 0x4009d1e8 and 0x4009cf4c). */
#ifndef SEQ_EVENTS_DEV_H
#define SEQ_EVENTS_DEV_H

#include "octatrack/seq.h"

/* the project in RAM: 16 banks, each 16 patterns then the Parts */
#define BANK_BASE       0x400e21e0u
#define BANK_STRIDE     0x9b340u
#define PAT_STRIDE      0x8ed8u
#define PAT_MASTER      0x8e50          /* s16: the master length, -1 INF (per-track scale) */
#define PAT_LENGTH      0x8e53          /* u8: the pattern's length (normal scale) */
#define PAT_SCALE_MODE  0x8e55          /* u8: 1 per track */
#define PAT_PART        0x8e57          /* u8: the Part the pattern plays, 0-3 */
#define PART_BASE       0x8ed80u        /* from the bank: the working Parts */
#define PART_STRIDE     0x18b2u
#define PART_MIDI       0x3e2           /* from a Part: the MIDI tracks' page 1, 32 bytes a track */
/* an audio track's record: T1-T8 in the pattern */
#define A_STRIDE        0x91au
#define A_LEN           0x50            /* u8: its length (per-track scale) */
#define A_LOCKS         0x59            /* 64 steps x 32 bytes */
#define A_TIMING        0x89a           /* 64 big-endian halfwords: condition, length, timing */
/* a MIDI track's record */
#define M_BASE          0x48d0u
#define M_STRIDE        0x8b0u
#define M_LEN           0x28
#define M_LOCKS         0x30
#define M_TIMING        0x830
/* the step masks, 8 bytes each at the head of a record: bit s of a big-endian 64-bit value */
#define MASK_TRIG       0x00            /* trigger trigs */
#define MASK_TRIGLESS   0x08            /* trigless trigs */
#define MASK_LOCK       0x10            /* lock trigs (a locked step with no trig) */
#define MASK_ONE        0x18            /* one-shot trigs (audio tracks) */
/* the firmware's current bank and pattern, and whether it plays */
#define FW_BANK         0x800065bdu
#define FW_PATTERN      0x800065beu
#define FW_NEXT_PATTERN 0x800065c1u
#define FW_NEXT_BANK    0x800065c2u
#define FW_PLAYING      0x800065b8u     /* a long, 1 while the sequencer plays */

/* a step's trig bits (our word) */
#define W_TRIG          1u              /* trigger trig: plays the sample, or the MIDI note */
#define W_TRIGLESS      2u              /* trigless trig: its locks and envelopes, no sample */
#define W_LOCK          4u              /* lock trig: its locks only */
#define W_ONE           8u              /* one-shot trig */
#define W_ALL           15u
/* a MIDI track's lock slots (the NOTE page) and their defaults in the Part */
#define SL_NOTE         0
#define SL_VEL          1
#define SL_NOT2         3               /* NOT2-NOT4: 64 + the interval from NOTE, 64 none */
#define NOLOCK          0xffu

#define DEV_IN_DDR(p) ((uint32_t)(p) >= 0x40000000u && (uint32_t)(p) < 0x48000000u)

/* the step a build reads (fire.c): the virtual record the handlers work on, and the stage that
 * writes the differences into the pattern for the one build and puts them back */
struct dev_step {
    const uint8_t *pat;                 /* the pattern */
    int32_t track, step;                /* a track of seq-events; the step the build reads */
    uint32_t word0, word;               /* the step's trig bits: stored, and as the handlers left them */
    int32_t generated;                  /* nothing stored: a handler made the trig */
    int32_t silent;                     /* the notes stage left no note: the velocity is not asked */
    uint8_t locks[32];                  /* the step's lock bytes as they will be */
};

/* the pattern each track's last build read (fire.c): its fires follow the pattern they read, at a
 * switch too */
extern const uint8_t *volatile sq_fire_pat[SQ_TRACKS];

static inline uint8_t dev_u8(const uint8_t *p)
{
    return *(const volatile uint8_t *)p;
}

static inline int32_t dev_s16(const uint8_t *p)
{
    return (int16_t)((p[0] << 8) | p[1]);
}

/* a bank's blob, or 0 */
static inline const uint8_t *dev_bank(int32_t bank)
{
    return (uint32_t)bank < 16 ? (const uint8_t *)(BANK_BASE + BANK_STRIDE * (uint32_t)bank) : 0;
}

/* the bank blob a pattern pointer is in */
static inline const uint8_t *dev_bank_of(const uint8_t *pat)
{
    return pat - ((uint32_t)pat - BANK_BASE) % BANK_STRIDE;
}

/* a track's record in a pattern */
static inline const uint8_t *dev_rec(const uint8_t *pat, int32_t track)
{
    return track < SQ_AUDIO ? pat + A_STRIDE * (uint32_t)track
                            : pat + M_BASE + M_STRIDE * (uint32_t)(track - SQ_AUDIO);
}

/* a track's step data: the lock bytes of a step (32), the timing halfword */
static inline uint8_t *dev_locks(const uint8_t *pat, int32_t track, int32_t step)
{
    return (uint8_t *)dev_rec(pat, track) + (track < SQ_AUDIO ? A_LOCKS : M_LOCKS) + 32 * (uint32_t)step;
}

static inline uint8_t *dev_timing(const uint8_t *pat, int32_t track, int32_t step)
{
    return (uint8_t *)dev_rec(pat, track) + (track < SQ_AUDIO ? A_TIMING : M_TIMING) + 2 * (uint32_t)step;
}

/* the long that holds a step's bit in mask kind (an offset above), and the bit */
static inline uint32_t *dev_mask(const uint8_t *pat, int32_t track, int32_t kind, int32_t step)
{
    return (uint32_t *)((uint8_t *)dev_rec(pat, track) + kind + (step < 32 ? 4 : 0));
}

/* the pattern sq_track_info and sq_stored read, or 0: the one the sequencer plays */
static inline const uint8_t *dev_pattern(void)
{
    int32_t bank = (int8_t)*(const volatile uint8_t *)FW_BANK, ptn = (int8_t)*(const volatile uint8_t *)FW_PATTERN;
    const uint8_t *b = dev_bank(bank);
    return b && (uint32_t)ptn < 16 ? b + PAT_STRIDE * (uint32_t)ptn : 0;
}

/* the fire's step load: the pattern this build reads */
static inline void dev_fire_begin(int32_t track, const uint8_t *pat)
{
    sq_fire_pat[track] = pat;
}

/* the pattern track plays: pat, or the one its last build read while that is the queued one (a
 * switch the current pattern byte has not caught up with) */
static inline const uint8_t *dev_track_pat(const uint8_t *pat, int32_t track)
{
    const uint8_t *fired = sq_fire_pat[track];
    int32_t bank = (int8_t)*(const volatile uint8_t *)FW_NEXT_BANK, ptn = (int8_t)*(const volatile uint8_t *)FW_NEXT_PATTERN;
    const uint8_t *b = dev_bank(bank);
    if (fired != pat && b && (uint32_t)ptn < 16 && fired == b + PAT_STRIDE * (uint32_t)ptn)
        return fired;
    return pat;
}

static inline int32_t dev_track_len(const uint8_t *pat, int32_t track, int32_t *master)
{
    int32_t per_track, all, len, m;
    pat = dev_track_pat(pat, track);
    per_track = dev_u8(pat + PAT_SCALE_MODE) == 1;
    all = dev_u8(pat + PAT_LENGTH);
    len = per_track ? dev_u8(dev_rec(pat, track) + (track < SQ_AUDIO ? A_LEN : M_LEN)) : all;
    m = dev_s16(pat + PAT_MASTER);
    *master = !per_track ? all : m < 1 ? 0 : m;
    return len >= 1 && len <= SQ_STEPS ? len : -1;
}

/* a MIDI track's page 1 in the Part its pattern plays: the values a step without a lock plays */
static inline const uint8_t *dev_part(const uint8_t *pat, int32_t track)
{
    return dev_bank_of(pat) + PART_BASE + PART_STRIDE * (uint32_t)(dev_u8(pat + PAT_PART) & 3) + PART_MIDI +
           32 * (uint32_t)(track - SQ_AUDIO);
}

static inline int32_t dev_track_info(const uint8_t *pat, int32_t track, struct sq_track *out)
{
    if ((out->len = dev_track_len(pat, track, &out->master)) < 0)
        return -1;
    pat = dev_track_pat(pat, track);
    out->note = track < SQ_AUDIO ? 60 : dev_part(pat, track)[SL_NOTE] & 127;
    out->root = SQ_ROOT_NONE;           /* the Octatrack keeps no keyboard key */
    out->scale = SQ_SCALE_NONE;
    out->pad = 0;
    return 0;
}

/* a step's trig bits from the masks */
static inline uint32_t dev_step_word(const uint8_t *pat, int32_t track, int32_t step)
{
    uint32_t w = 0, bit = 1u << (step & 31);
    pat = dev_track_pat(pat, track);
    if (*dev_mask(pat, track, MASK_TRIG, step) & bit)
        w |= W_TRIG;
    if (*dev_mask(pat, track, MASK_TRIGLESS, step) & bit)
        w |= W_TRIGLESS;
    if (*dev_mask(pat, track, MASK_LOCK, step) & bit)
        w |= W_LOCK;
    if (track < SQ_AUDIO && (*dev_mask(pat, track, MASK_ONE, step) & bit))
        w |= W_ONE;
    return w;
}

/* trig bits as SQ_TRIG (it plays the sample or the note) or SQ_LOCK (locks only), and back: SQ_TRIG
 * wins, a trig a handler adds is a trigger trig, and a lock a handler takes the sound from is a
 * lock trig; what stays as it is stays stored as it is */
static inline uint32_t dev_trig_in(uint32_t word)
{
    return (word & (W_TRIG | W_ONE)) ? SQ_TRIG : (word & (W_TRIGLESS | W_LOCK)) ? SQ_LOCK : 0;
}

static inline uint32_t dev_trig_out(uint32_t word, uint32_t trig)
{
    if (trig == dev_trig_in(word))
        return word;
    if (!trig)
        return word & ~W_ALL;
    if (trig & SQ_TRIG)
        return word | W_TRIG;
    return (word & ~(W_TRIG | W_ONE)) | W_LOCK;
}

/* The notes stage (MIDI tracks). src: the step (struct dev_step). A generated trig plays NOTE
 * alone, as the API says: the Part's, with no chord; a stored step plays its locks, or the Part's
 * where it has none. */
static inline int32_t dev_generated(int32_t track, int32_t step, const void *src)
{
    (void)track;
    (void)step;
    return ((const struct dev_step *)src)->generated;
}

static inline void dev_notes_in(int32_t track, int32_t step, const void *src, int32_t generated,
                                int8_t notes[SQ_NOTES])
{
    const struct dev_step *s = (const struct dev_step *)src;
    const uint8_t *part = dev_part(s->pat, track);
    int32_t i, v;
    (void)step;
    for (i = 0; i < SQ_NOTES; i++)
        notes[i] = 0;
    notes[0] = (int8_t)((generated || s->locks[SL_NOTE] == NOLOCK ? part[SL_NOTE] : s->locks[SL_NOTE]) & 127);
    if (generated)
        return;
    for (i = 1; i < SQ_NOTES; i++) {
        v = s->locks[SL_NOT2 + i - 1] == NOLOCK ? part[SL_NOT2 + i - 1] : s->locks[SL_NOT2 + i - 1];
        notes[i] = (int8_t)(v == 64 || v > 127 ? 0 : v - 64);
    }
}

/* -> src with what the build gets written into it, or 0: the stored notes as they are */
static inline const void *dev_notes_out(int32_t track, int32_t step, const void *src,
                                        int32_t generated, const int8_t notes[SQ_NOTES])
{
    struct dev_step *s = (struct dev_step *)src;
    int8_t was[SQ_NOTES];
    int32_t i, v;
    if (!generated) {
        dev_notes_in(track, step, src, 0, was);
        for (i = 0; i < SQ_NOTES && notes[i] == was[i]; i++)
            ;
        if (i == SQ_NOTES)
            return 0;
    }
    if (notes[0] < 0) {                 /* no note: a velocity of 0 is not sent */
        s->locks[SL_VEL] = 0;
        s->silent = 1;
        return s;
    }
    s->locks[SL_NOTE] = (uint8_t)notes[0];
    for (i = 1; i < SQ_NOTES; i++) {
        v = 64 + notes[i];              /* an interval beyond the knob's 64 either way is cut */
        s->locks[SL_NOT2 + i - 1] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
    }
    return s;
}

/* A lock into the step's lock bytes (list: struct dev_step's locks); the byte is the 8.8 value's
 * integer part */
static inline void dev_lock_set(void *list, uint32_t slot, int32_t value)
{
    int32_t v = value >> 8;
    ((uint8_t *)list)[slot] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
}

static inline int32_t dev_list_ok(const void *list)
{
    return list != 0;
}

static inline int32_t dev_playing(void)
{
    return *(const volatile uint32_t *)FW_PLAYING == 1;
}

/* the Octatrack builds a track's first step at the transport start, whatever changed: nothing to ask */
static inline void dev_rearm(int32_t track)
{
    (void)track;
}

#endif
