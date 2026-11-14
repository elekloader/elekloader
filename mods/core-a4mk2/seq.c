/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: the sequencer's events and play state (README, "The
 * sequencer"; analog-four-mk2/seq.h), core-ar2 0.11's at the Analog
 * Four MKII's addresses. Every address is OS 1.56's.
 *
 * The tick (0x4007a9b0, vector 121) runs 96 times a quarter note, also while
 * stopped; the sequencer works on every 4th (24 a quarter note). On such a
 * tick, for each of the 6 tracks at a step boundary, the tick's loop calls
 * trig_fire (0x4007a1d2) for the track's next step: trig_fire fills the trig
 * record (0x42872474; its first word 0 for no trig, a failed condition, a
 * trigless step) and the tick copies it to the track's trig queue. Core's
 * sites:
 *
 *   - 0x4007b0e8 (seq.s), after the copy: the track stepped, and trigs if
 *     the record's first word isn't 0;
 *   - 0x4007badc, the tick's exit (stdmidi.s): core_std_seq_tick calls
 *     core_seq_exit, then ev_seq_tick. ev_transport when the play state
 *     changed, ev_seq_step(steps, trigs) when any track stepped this tick.
 *
 * PLAY from stop: the same tick sets every track's first-step flag
 * (0x80006b1a) and fires every track's first step, so the tick's own call
 * carries CORE_SEQ_FIRST. A resume (PLAY after a pause, a MIDI Continue,
 * 0x400797da) plays the step it stopped on from records stock made at the
 * pause (0x4007bafc, 0x428722bc + track x 0x44) and fires only the step
 * after it: core seeds that step as its own call first,
 * ev_seq_step(0x3f | CORE_SEQ_FIRST, trigs), trigs the tracks whose record
 * holds a trig. A pause sets the play state to 2.
 */

#include <stdint.h>

#define PLAY_STATE (*(volatile int *)0x80006a88)        /* 0 stopped, 1 playing, 2 paused */
#define FIRST_FLAGS (*(volatile unsigned short *)0x80006b1a) /* bit t: track t's first step after PLAY */
#define TRIG_RECORD (*(volatile unsigned *)0x42872474)
#define RESUME_RECORD(t) (*(volatile unsigned *)(0x428722bcu + 0x44u * (unsigned)(t)))
#define TRACKS 6
#define ALL ((1u << TRACKS) - 1)
#define SEQ_FIRST 0x80000000u                           /* in steps: the first step after PLAY */

typedef void (*ev_seq_step_t)(uint32_t steps, uint32_t trigs);
typedef void (*ev_transport_t)(int32_t state);
extern const ev_seq_step_t ev_seq_step[];
extern const ev_transport_t ev_transport[];

static uint32_t steps, trigs;                   /* this tick's, from the fired site */
static int state;                               /* the play state the last tick saw */
static int edged;                               /* this play's first tick was seen */
static uint32_t first_flag;                     /* SEQ_FIRST for the edge tick's own call */
static uint32_t seed_steps, seed_trigs;         /* a resume's seeded step */

/* the first tick after the play state became 1: a start fires every track's
 * first step itself (its first-step flag is still set at the fired site); a
 * resume (the edge seen at the exit, or at a fired site without the flag)
 * plays stock's records */
static void edge(int track)
{
    int t;
    uint32_t m = 0;
    if (edged || state == 1 || PLAY_STATE != 1)
        return;
    edged = 1;
    if (track >= 0 && (FIRST_FLAGS & (1u << track))) {
        first_flag = SEQ_FIRST;
        seed_steps = seed_trigs = 0;
        return;
    }
    first_flag = 0;
    for (t = 0; t < TRACKS; t++)
        if (RESUME_RECORD(t))
            m |= 1u << t;
    seed_steps = ALL | SEQ_FIRST;
    seed_trigs = m;
}

/* 0x4007b0e8 (seq.s): the tick made track's next step's trig record */
void core_seq_fired(int track)
{
    if ((unsigned)track >= TRACKS)
        return;
    edge(track);
    steps |= 1u << track;
    if (TRIG_RECORD)
        trigs |= 1u << track;
}

/* 0x4007badc, from core_std_seq_tick (stdmidi.c): the tick's exit, before
 * ev_seq_tick */
void core_midi_irq_drain(void);

void core_seq_exit(void)
{
    const ev_seq_step_t *s;
    const ev_transport_t *x;
    int now, first;
    uint32_t st, tr;
    edge(-1);
    now = PLAY_STATE;
    st = steps;
    tr = trigs;
    steps = trigs = 0;
    first = edged && state != 1;
    if (now != 1)
        edged = 0;
    if (now != state) {
        state = now;
        for (x = ev_transport; *x; x++)
            (*x)(now);
    }
    if (first && seed_steps)
        for (s = ev_seq_step; *s; s++)
            (*s)(seed_steps, seed_trigs);
    if (st) {
        if (first)
            st |= first_flag;
        for (s = ev_seq_step; *s; s++)
            (*s)(st, tr);
    }
    core_midi_irq_drain();              /* what waited in core_midi_try_send's ring (send.c) */
}

/* the play state, from any task or interrupt: 0 stopped, 1 playing, 2 paused */
int core_transport(void)
{
    return PLAY_STATE;
}

/* ---- getters, from any task or interrupt ---- */

#define PATTERN_NOW (*(volatile signed char *)0x80006acc)
#define PATTERN_RESOLVED (*(volatile signed char *)0x80006acd) /* the resolver's answer at the end */
#define RESOLVED (*(volatile int *)0x80006b58)          /* 1 from the resolve to the commit */
#define SWITCH_ASKED (*(volatile int *)0x80006b2c)      /* a direct switch asked, not yet timed */
#define SWITCH_COUNT (*(volatile int *)0x80006b28)      /* clocks to a timed direct switch */
#define SWITCH_TO (*(volatile int *)0x80006b20)
#define EL_PLAYING (*(volatile unsigned *)0x41281752)   /* the song's element playing */
#define EL_QUEUED (*(volatile int *)0x41281756)         /* the element queued, -1 none */
#define PLAY_MODE (*(volatile int *)0x412aa56a)         /* 0 pattern, 1 chain, 2 song */
#define EL_NEXT(e) (*(volatile short *)(0x4127fbd2u + 0x14u * (unsigned)(e)))
#define EL_ROW(e) (*(volatile short *)(0x4127fbd6u + 0x14u * (unsigned)(e)))
#define EL_PATTERN(e) (*(volatile unsigned char *)(0x4127fbe5u + 0x14u * (unsigned)(e)))
#define ROW_NEXT(r) (*(volatile short *)(0x41280fd4u + 0x1eu * (unsigned)(r)))
#define ROW_HEAD(r) (*(volatile unsigned short *)(0x41280fd6u + 0x1eu * (unsigned)(r)))
#define ROW_REPEATS(r) (*(volatile short *)(0x41280fdau + 0x1eu * (unsigned)(r)))
#define ROW_REPEATED (*(volatile int *)0x80006b30)
#define ROW_SKIP (*(volatile short *)0x4127f948)
#define TEMPO (*(volatile unsigned *)0x40214818)        /* BPM x 120 */
#define TRACK_STEP ((volatile signed char *)0x80006ab8) /* + track: the step sounding now */
#define TRACK_SPEED ((volatile signed char *)0x80006aed) /* + track: 0..6, as the tick plays it */
#define PATTERNS 0x4066ac36u                            /* live pattern p's object */
#define PATTERN_SIZE 0x16fdau
#define TRACK_REC 0x3b1u                                /* a track's record in the object */
#define TR_LENGTH 0x1c5u                                /* the track's length, a word (per-track scale) */
#define PAT_LENGTH 0x16ec6u                             /* the pattern's length, a word */
#define PAT_SCALE 0x16eccu                              /* scale mode: 1 per track */

int core_pattern_now(void)
{
    int p = PATTERN_NOW;
    return (unsigned)p < 128 ? p : -1;
}

static int pattern_of(int e)
{
    int p;
    if ((unsigned)e > 0xff)
        return -1;
    p = EL_PATTERN(e);
    return p < 128 ? p : -1;
}

/* what the resolver (0x400793e4) will answer at the pattern's end, read
 * without its side effects; a direct switch and an answer already made
 * come first */
int core_pattern_next(void)
{
    int q, e, r, n;
    unsigned pl;
    if (SWITCH_ASKED || SWITCH_COUNT > 0) {
        n = SWITCH_TO & 0xff;
        return n < 128 ? n : -1;
    }
    if (RESOLVED == 1) {
        n = PATTERN_RESOLVED;
        return (unsigned)n < 128 ? n : -1;
    }
    pl = EL_PLAYING;
    if ((pl & 0xffff) > 0xff)
        return -1;
    q = EL_QUEUED;
    e = (short)pl;
    if (q >= 0)
        return pattern_of((short)q);
    if (PLAY_MODE == 0)
        return pattern_of(e);
    n = EL_NEXT(e);
    if (n >= 0)
        return pattern_of(n);
    r = EL_ROW(e);
    if ((unsigned)r > 0xff)
        return -1;
    if (PLAY_MODE == 2 && ROW_REPEATS(r) <= ROW_REPEATED) {
        n = ROW_NEXT(r);
        if (n < 0)
            return -1;                          /* the song ends */
        if (ROW_SKIP == n) {
            n = ROW_NEXT(n);
            if (n < 0)
                return -1;
        }
        return pattern_of((short)ROW_HEAD(n));
    }
    return pattern_of((short)ROW_HEAD(r));
}

unsigned core_tempo(void)
{
    return TEMPO;
}

int core_track_speed(int track)
{
    return (unsigned)track < TRACKS ? TRACK_SPEED[track] : -1;
}

int core_track_step(int track)
{
    return (unsigned)track < TRACKS ? TRACK_STEP[track] : -1;
}

/* the length track's steps wrap at in the playing pattern's live object, as
 * the tick reads it: the track's in per-track scale mode, else the
 * pattern's. Odd track records sit at odd addresses: bytes only. */
int core_track_length(int track)
{
    volatile unsigned char *o;
    int p = PATTERN_NOW;
    if ((unsigned)track >= TRACKS || (unsigned)p >= 128)
        return -1;
    o = (volatile unsigned char *)(PATTERNS + PATTERN_SIZE * (unsigned)p);
    if (o[PAT_SCALE] == 1) {
        o += TRACK_REC * (unsigned)track + TR_LENGTH;
        return (short)(o[0] << 8 | o[1]);
    }
    return (short)(o[PAT_LENGTH] << 8 | o[PAT_LENGTH + 1]);
}
