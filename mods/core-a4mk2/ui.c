/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.7: the standard events (std1) at new sites, the UI and the
 * system (README, "Standard events (0.7)"), and the A4's own turn event,
 * ev_knob_turn. OS 1.56; the sites are in ui.s and mod.json.
 *
 *   ev_boot_done  std_boot_done  the UI task, once, after its init (0x4009995c)
 *   ev_idle       std_idle       main's task, after it started the UI task: the
 *                                lowest priority (1), over and over (0x40106302)
 *   ev_crash      std_crash      core's crash handler in place of stock's
 *                                printer (0x400638da), which it calls after
 *   ev_usb        std_usb        the UI task's USB message (case 0x1e, 0x4009a248),
 *                                when its configured byte changes
 *   ev_key_leds   std_key_leds   after the views composed the LEDs, before
 *                                the frame is sent (0x40096b42); mgr: the
 *                                LedManager, for core_led_set
 *   ev_mode       std_mode       the UI state's mode bytes, compared each tick
 *   ev_param_set  std_param_set  the three parameter sets' set virtuals
 *                                (vtable slots), queued and run at the next
 *                                tick in the UI task
 *   ev_clip       std_clip       18 copy, paste and clear routines (ui.s), the pattern view's 3 calls
 *   ev_pattern    std_pattern    the UI task's pattern boundary message
 *                                (case 9, 0x40099c84), when the pattern changed
 *   ev_mute       std_mute       the mute setter's entry (0x400aeb8e)
 *   ev_knob_turn  (native)       the page view's turn (0x4002598c) after its
 *                                step and lockout checks (0x40025b78)
 */

#include <stdint.h>
#include <std1.h>
#include "analog-four-mk2/keys.h"
#include "analog-four-mk2/leds.h"
#include "analog-four-mk2/pages.h"
#include "analog-four-mk2/params.h"
#include "analog-four-mk2/ui.h"

typedef void (*fn_void)(void);
typedef void (*fn_frame)(const void *frame);
typedef void (*fn_u)(unsigned);
typedef void (*fn_ptr)(void *);
typedef void (*fn_mode)(int, int, int);
typedef void (*fn_set)(unsigned, unsigned, int32_t);
typedef int (*fn_clip)(unsigned, const struct core_clip *);
typedef int (*fn_pattern)(unsigned, unsigned, unsigned);
typedef int (*fn_mute)(unsigned, unsigned, unsigned);

extern const fn_void std_boot_done[];
extern const fn_void std_idle[];
extern const fn_frame std_crash[];
extern const fn_u std_usb[];
extern const fn_ptr std_key_leds[];
extern const fn_mode std_mode[];
extern const fn_set std_param_set[];
extern const fn_clip std_clip[];
extern const fn_pattern std_pattern[];
extern const fn_mute std_mute[];
extern const ev_knob_turn_fn ev_knob_turn[];

static unsigned short irq_mask(void)
{
    unsigned short sr;
    __asm__ volatile("move.w %%sr,%0\n\tmove.w #0x2700,%%sr" : "=d"(sr) : : "memory");
    return sr;
}

static void irq_restore(unsigned short sr)
{
    __asm__ volatile("move.w %0,%%sr" : : "d"(sr) : "memory");
}

/* ============================== ev_boot_done =============================== */

void core_boot_done(void)
{
    const fn_void *f;
    for (f = std_boot_done; *f; f++)
        (*f)();
}

/* ================================ ev_idle ================================== */

/* ui.s's core_idle_start calls this over and over, after main started the
 * UI task; with no handler it is stock's spin */
void core_idle_run(void)
{
    const fn_void *f;
    for (f = std_idle; *f; f++)
        (*f)();
}

/* ================================ ev_crash ================================= */

/* The exception stub (0x40001906) masks interrupts, keeps the frame's place
 * (0x4133cf58) and calls *0x4133cf54(frame), which 0x400638d6 sets to stock's
 * printer at boot through 0x4000203a. core's install (ui.s) keeps that
 * printer and puts core_crash there instead. */
typedef void (*crash_set_t)(fn_frame);
#define CRASH_SET ((crash_set_t)0x4000203au)

static fn_frame crash_stock;

static void core_crash(const void *frame)
{
    const fn_frame *f;
    for (f = std_crash; *f; f++)
        (*f)(frame);
    if (crash_stock)
        crash_stock(frame);
}

void core_crash_install(fn_frame stock)
{
    crash_stock = stock;
    CRASH_SET(core_crash);
}

/* ================================= ev_usb ================================== */

/* the UI task's message 0x1e/1, one static record posted by 0x40004412 on
 * SET_CONFIGURATION, reset, suspend and detach: +8 the USB mode (4 USB MIDI,
 * 6 USB AUDIO/MIDI, 7 OVERBRIDGE), +9 configured. A level, so a handler
 * hears each change. */
static unsigned usb_last = 0xffu;
static volatile uint8_t usb_mode;

void core_usb_msg(const uint8_t *msg)
{
    const fn_u *f;
    unsigned configured = msg[9] ? 1u : 0u;
    usb_mode = msg[8];
    if (configured == usb_last)
        return;
    usb_last = configured;
    for (f = std_usb; *f; f++)
        (*f)(configured);
}

unsigned core_usb_mode(void)
{
    return usb_mode;
}

/* =============================== ev_key_leds =============================== */

/* 0x40096aee: the views compose the LEDs into the LedManager (0x40062652,
 * then 0x4009638c), then 0x4007594c sends each of the frame's 24 bytes that
 * changed. Handlers run between the two, so an LED a handler sets stays over
 * the view's. */
void core_key_leds(void *mgr)
{
    const fn_ptr *f;
    for (f = std_key_leds; *f; f++)
        (*f)(mgr);
}

/* the LedManager's set (0x4009624e): (mgr, led, mode, 0, colour); stock
 * switches an LED off with mode 0 and colour 15, and its plain "on" helper
 * (0x400962bc) passes mode 2 */
typedef void (*led_set_t)(void *mgr, int32_t led, int32_t mode, int32_t b, int32_t colour);
#define LED_SET ((led_set_t)0x4009624eu)

void core_led_set(void *mgr, unsigned led, unsigned colour)
{
    if (!mgr)
        return;
    if (colour == LED_OFF)
        LED_SET(mgr, (int32_t)led, 0, 0, 15);
    else
        LED_SET(mgr, (int32_t)led, 2, 0, (int32_t)colour);
}

/* ================================= ev_mode ================================= */

/* the UI state (0x4014b1f6 makes it once and keeps it at 0x418717f8): its
 * mode bytes, as each setter writes them (0x40021b38 keyboard, 0x40021d2c
 * grid record, 0x40021ddc live record, 0x40021e1e step record) */
#define UI_STATE (*(uint8_t *volatile *)0x418717f8u)
static const uint16_t mode_at[MODE_COUNT] = {0x160, 0x168, 0x169, 0x16a};
static uint8_t mode_last[MODE_COUNT];
static uint8_t mode_seen;

static void mode_poll(void)
{
    const uint8_t *ui = UI_STATE;
    int m;
    if (!ui)
        return;
    for (m = 0; m < MODE_COUNT; m++) {
        uint8_t now = ui[mode_at[m]];
        if (mode_seen && now != mode_last[m]) {
            const fn_mode *f;
            for (f = std_mode; *f; f++)
                (*f)(m, mode_last[m], now);
        }
        mode_last[m] = now;
    }
    mode_seen = 1;
}

/* ============================== ev_param_set =============================== */

/* the sets' value setters, their vtables' +0x30 and +0x38. +0x30 (set, id,
 * value, track, rec, notify) is the panel's: the turn (+0x34) and the other
 * edits end in it; it clamps value to the parameter's range, rec also records
 * it into the held or recording steps, and it returns the value it stored.
 * +0x38 (set, id, value, track, rec) is the other setter (ids up to 0x67).
 * Both run in the UI task or the MIDI task, so a set is queued and its
 * handlers run at the next tick in the UI task. */
typedef int32_t (*pset_t)(void *set, int32_t id, int32_t value, int32_t track, int32_t rec, int32_t notify);
#define PSET_SYNTH ((pset_t)0x400a0046u)
#define PSET_FX ((pset_t)0x400a0308u)
#define PSET_CV ((pset_t)0x400a05c2u)
#define PSET2_SYNTH ((pset_t)0x400a0cecu)
#define PSET2_FX ((pset_t)0x400a0e1eu)
#define PSET2_CV ((pset_t)0x400a0f08u)

#define SETS 64                         /* a power of two */
static struct { uint8_t track; uint16_t id; int32_t value; } sets[SETS];
static volatile unsigned set_head, set_tail, set_dropped;

static void set_queue(unsigned track, int32_t id, int32_t value)
{
    unsigned short sr;
    unsigned h;
    if (!std_param_set[0] || id < 0)
        return;
    sr = irq_mask();
    h = set_head;
    if (h - set_tail < SETS) {
        sets[h & (SETS - 1)].track = (uint8_t)track;
        sets[h & (SETS - 1)].id = (uint16_t)id;
        sets[h & (SETS - 1)].value = value;
        set_head = h + 1;
    } else {
        set_dropped++;
    }
    irq_restore(sr);
}

int32_t core_pset_synth(void *set, int32_t id, int32_t value, int32_t track, int32_t rec, int32_t notify)
{
    int32_t r = PSET_SYNTH(set, id, value, track, rec, notify);
    set_queue((unsigned)track, id, r);
    return r;
}

int32_t core_pset_fx(void *set, int32_t id, int32_t value, int32_t track, int32_t rec, int32_t notify)
{
    int32_t r = PSET_FX(set, id, value, track, rec, notify);
    set_queue(TRACK_FX, id, r);
    return r;
}

int32_t core_pset_cv(void *set, int32_t id, int32_t value, int32_t track, int32_t rec, int32_t notify)
{
    int32_t r = PSET_CV(set, id, value, track, rec, notify);
    set_queue(TRACK_CV, id, r);
    return r;
}

int32_t core_pset2_synth(void *set, int32_t id, int32_t value, int32_t track, int32_t rec)
{
    int32_t r = PSET2_SYNTH(set, id, value, track, rec, 0);
    set_queue((unsigned)track, id, value);
    return r;
}

int32_t core_pset2_fx(void *set, int32_t id, int32_t value, int32_t track, int32_t rec)
{
    int32_t r = PSET2_FX(set, id, value, track, rec, 0);
    set_queue(TRACK_FX, id, value);
    return r;
}

int32_t core_pset2_cv(void *set, int32_t id, int32_t value, int32_t track, int32_t rec)
{
    int32_t r = PSET2_CV(set, id, value, track, rec, 0);
    set_queue(TRACK_CV, id, value);
    return r;
}

static void set_drain(void)
{
    unsigned t, end = set_head;
    while ((t = set_tail) != end) {
        const fn_set *f;
        unsigned track = sets[t & (SETS - 1)].track;
        unsigned id = sets[t & (SETS - 1)].id;
        int32_t value = sets[t & (SETS - 1)].value;
        set_tail = t + 1;
        for (f = std_param_set; *f; f++)
            (*f)(track, id, value);
    }
}

/* ================================= ev_clip ================================= */

/* ui.s's 17 sites pass code = kind | op << 8 | flags << 16 and the routine's
 * first two arguments. A confirm callback (CLIP_F_CONFIRM) does its work only
 * when its second argument's low byte is set (YES on the dialog); otherwise
 * neither call comes. CLIP_F_TRACK: the second argument is the track. */
#define CLIP_F_CONFIRM 1u
#define CLIP_F_TRACK 2u
#define CLIP_F_GRID 4u                  /* the sequencer view's FUNC+REC/STOP/PLAY */

/* the sequencer view's copy, clear and paste virtuals act on the track only
 * in grid record (0x40021ca8: +0x161 and +0x168); otherwise they return 0
 * and the key goes on to the pattern's copy, clear and paste */
typedef uint8_t (*grid_rec_t)(void *);
#define GRID_REC ((grid_rec_t)0x40021ca8u)

static int clip_fill(struct core_clip *c, uint32_t code, uint32_t a1, uint32_t a2)
{
    unsigned fl = code >> 16;
    if ((fl & CLIP_F_CONFIRM) && !(a2 & 0xffu))
        return 0;
    if ((fl & CLIP_F_GRID) && !GRID_REC(*(void **)(a1 + 0x6c)))
        return 0;
    c->kind = (uint8_t)code;
    c->op = (uint8_t)(code >> 8);
    c->track = (fl & CLIP_F_TRACK) ? (uint8_t)a2 : CLIP_NONE;
    c->slot = CLIP_NONE;
    return 1;
}

/* 0: not taken. Taken: 1, the stub returns 0; 3 (a grid routine), the stub
 * returns 1, "handled", so the key does not go on to the pattern's. */
int core_clip_pre(uint32_t code, uint32_t a1, uint32_t a2)
{
    struct core_clip c;
    const fn_clip *f;
    if (!std_clip[0] || !clip_fill(&c, code, a1, a2))
        return 0;
    for (f = std_clip; *f; f++)
        if ((*f)(1, &c))
            return (code >> 16) & CLIP_F_GRID ? 3 : 1;
    return 0;
}

void core_clip_post(uint32_t code, uint32_t a1, uint32_t a2)
{
    struct core_clip c;
    const fn_clip *f;
    if (!std_clip[0] || !clip_fill(&c, code, a1, a2))
        return;
    for (f = std_clip; *f; f++)
        (*f)(0, &c);
}

/* FUNC+REC, STOP and PLAY outside grid record: the pattern view's copy,
 * paste and clear (0x400c0908, 0x400c0a74, 0x400c0b48; paste and clear only
 * when no undo is possible) call the pattern's own routines, hooked at those
 * calls (keep2): 0x400a2aa4(pattern), 0x400a2cba(pattern, 1) and
 * 0x400a2de0(pattern, whole, undo); a %d0 byte says each did it */
typedef uint8_t (*pop_t)(void *, int32_t, int32_t);
#define PATTERN_COPY ((pop_t)0x400a2aa4u)
#define PATTERN_PASTE ((pop_t)0x400a2cbau)
#define PATTERN_CLEAR ((pop_t)0x400a2de0u)

static uint32_t pattern_op(pop_t fn, unsigned op, void *pattern, int32_t a, int32_t b)
{
    const uint32_t code = CLIP_PATTERN | op << 8;
    uint8_t r;
    if (core_clip_pre(code, 0, 0))
        return 0;
    r = fn(pattern, a, b);
    core_clip_post(code, 0, 0);
    return r;
}

uint32_t core_clip_pcopy(void *pattern)
{
    return pattern_op(PATTERN_COPY, CLIP_COPY, pattern, 0, 0);
}

uint32_t core_clip_ppaste(void *pattern, int32_t a)
{
    return pattern_op(PATTERN_PASTE, CLIP_PASTE, pattern, a, 0);
}

uint32_t core_clip_pclear(void *pattern, int32_t whole, int32_t undo)
{
    return pattern_op(PATTERN_CLEAR, CLIP_CLEAR, pattern, whole, undo);
}

/* =============================== ev_pattern ================================ */

/* The sequencer posts message 9 to the UI task at every pattern boundary:
 * a queued change's commit, an immediate switch and a plain loop restart.
 * The pattern byte (0x80006acc) tells a change from a restart. Only the
 * change is reported, after it (pre 0, how 0); the request is not hooked. */
#define PATTERN_NOW (*(volatile uint8_t *)0x80006accu)
static unsigned pattern_last = 0x100u;

static void pattern_seed(void)
{
    if (pattern_last > 0xffu)
        pattern_last = PATTERN_NOW;
}

void core_pattern_msg(void)
{
    unsigned now = PATTERN_NOW;
    const fn_pattern *f;
    if (pattern_last <= 0xffu && now != pattern_last)
        for (f = std_pattern; *f; f++)
            if ((*f)(0, now, 0))
                break;
    pattern_last = now;
}

/* ================================= ev_mute ================================= */

/* 0x400aeb8e(settings, track 0..5, muted, send): the project's per-track
 * mute, from FUNC + a track key (UI task) and MIDI CC 94 and the mute NRPN
 * (the MIDI task). Nonzero takes it: stock sets nothing and sends nothing. */
int core_mute_pre(int32_t track, int32_t muted)
{
    const fn_mute *f;
    if ((uint32_t)track > TRACK_CV)
        return 0;
    for (f = std_mute; *f; f++)
        if ((*f)((unsigned)track, (muted & 0xff) ? 1u : 0u, MUTE_SCOPE_PROJECT))
            return 1;
    return 0;
}

/* ============================== ev_knob_turn =============================== */

/* 0x4002598c, the page view's turn, past its step (delta nonzero) and its
 * lockout: the same tests stock makes next, to say which edit the turn
 * becomes. A held track key (0x20..0x23, with neither 0x24 nor 0x25 held)
 * on a synth track edits every held track; else a held trig or live record
 * makes a p-lock; else the turn edits the sound (then ev_param_turn). */
typedef int32_t (*key_held_t)(int32_t key);
typedef int32_t (*track_of_t)(void *);
typedef int32_t (*lock_rec_t)(void *);    /* a pointer, returned in %d0 */
typedef uint8_t (*lock_live_t)(void *);
#define KEY_HELD ((key_held_t)0x400647a2u)
#define TRACK_OF ((track_of_t)0x400adbd2u)
#define LOCK_REC ((lock_rec_t)0x400220beu)
#define LOCK_LIVE ((lock_live_t)0x400221e8u)

static int turn_how(void *view)
{
    void *seq = *(void **)((uint8_t *)view + 0x6c);
    int32_t track = TRACK_OF(*(void **)((uint8_t *)view + 0x74));
    if (track <= 3 && (KEY_HELD(0x20) || KEY_HELD(0x21) || KEY_HELD(0x22) || KEY_HELD(0x23))
        && !KEY_HELD(0x24) && !KEY_HELD(0x25))
        return TURN_MULTI;
    if (LOCK_REC(seq) || LOCK_LIVE(seq))
        return TURN_LOCK;
    return TURN_SOUND;
}

int core_knob_turn(void *view, int32_t id, int32_t delta, int32_t knob)
{
    const ev_knob_turn_fn *f;
    int how;
    if (!ev_knob_turn[0])
        return 0;
    how = turn_how(view);
    for (f = ev_knob_turn; *f; f++)
        if ((*f)(view, id, delta, knob, how))
            return 1;
    return 0;
}

/* ================================ the tick ================================= */

/* ev_tick (native), after core_std_tick: the queued sets and the modes */
void core_ui_tick(void *ctrl)
{
    (void)ctrl;
    pattern_seed();
    if (std_param_set[0])
        set_drain();
    if (std_mode[0])
        mode_poll();
}
