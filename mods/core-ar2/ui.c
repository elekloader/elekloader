/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.17: the standard events (std1) at new sites, the UI and the
 * system (README, "Standard events (0.17)"; analog-rytm-mk2/ui.h, leds.h).
 * OS 1.74; the sites are in ui.s and mod.json. core-a4mk2's ui.c is the
 * model; what differs on the Rytm is said where it does.
 *
 *   ev_boot_done  std_boot_done  the init task, once, after it spawned the UI
 *                                task (0x40100a8c), before the idle loop
 *   ev_idle       std_idle       the init task's idle loop (prio 1, the
 *                                lowest but the root's), over and over
 *   ev_crash      std_crash      core's fault handler in place of stock's
 *                                printer (installed at ev_boot_done), which it
 *                                calls after
 *   ev_usb        std_usb        the UI task's USB message (0x26 type 1,
 *                                0x400a5356), when the configured level
 *                                changes; the boot's level once at ev_boot_done
 *   ev_key_leds   std_key_leds   after the views and stock's defaults composed
 *                                the LEDs, before the frame's flush (0x400a0d32)
 *   ev_mode       std_mode       the UI state's mode bytes, compared each tick
 *   ev_param_set  std_param_set  the parameter sets' value setter (0x400a906c),
 *                                queued and run at the next tick in the UI task
 *   ev_clip       std_clip       pattern copy, paste and clear (the pattern
 *                                view's and the pattern select's), track copy,
 *                                paste and clear (grid record), kit paste and
 *                                clear (the kit list's confirm)
 *   ev_pattern    std_pattern    the UI task's pattern message (0x1e,
 *                                0x400a4fb6), when the pattern changed
 *   ev_mute       std_mute       the mute setter's entry (0x400b78d8)
 *   ev_view       std_view       the view stack's push (0x40079588) and its
 *                                removes: close, sweep, replace, pop last
 */

#include <stdint.h>
#include <std1.h>
#include "analog-rytm-mk2/leds.h"
#include "analog-rytm-mk2/params.h"
#include "analog-rytm-mk2/ui.h"

typedef void (*fn_void)(void);
typedef void (*fn_frame)(const void *frame);
typedef void (*fn_u)(unsigned);
typedef void (*fn_ptr)(void *);
typedef void (*fn_mode)(int, int, int);
typedef void (*fn_set)(unsigned, unsigned, int32_t);
typedef int (*fn_clip)(unsigned, const struct core_clip *);
typedef int (*fn_pattern)(unsigned, unsigned, unsigned);
typedef int (*fn_mute)(unsigned, unsigned, unsigned);
typedef int (*fn_view)(int, void *, void *, void *);

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
extern const fn_view std_view[];

int core_current_track(void);           /* moments.c */

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

/* ================================ ev_crash ================================= */

/* The fault stub (0x40000d6e, every RAM vector) masks interrupts, keeps the
 * stack pointer (0x413da654) and calls *0x413da650(frame) when it is set;
 * the init task sets it to stock's printer (0x4007a40e, "EXCEPTION AR ...",
 * which halts) through 0x400014a2. That runs before core can hook it, so at
 * ev_boot_done core keeps the printer it finds there and puts core_crash in
 * its place. A fault before ev_boot_done goes to stock's printer alone. */
typedef void (*crash_set_t)(fn_frame);
#define CRASH_SET ((crash_set_t)0x400014a2u)
#define CRASH_SLOT (*(fn_frame volatile *)0x413da650u)

static fn_frame crash_stock;

static void core_crash(const void *frame)
{
    const fn_frame *f;
    for (f = std_crash; *f; f++)
        (*f)(frame);
    if (crash_stock)
        crash_stock(frame);
}

static void crash_install(void)
{
    fn_frame now = CRASH_SLOT;
    if (now == core_crash)
        return;
    crash_stock = now;
    CRASH_SET(core_crash);
}

/* ================================= ev_usb ================================== */

/* usb_notify (0x4000387a) fills one static message (0x413db2a0: +8 the USB
 * mode, +9 the connection) and posts it to the UI queue on VBUS attach and
 * detach and on SET_CONFIGURATION; two posts before the UI reads it collapse
 * into the last. So core reports the level stock keeps (0x413db2bc, set on
 * SET_CONFIGURATION, cleared on detach and reset) as it reads when the
 * message is handled, once per change. */
#define USB_CONFIGURED (*(volatile int32_t *)0x413db2bcu)
#define USB_MSG_MODE (*(volatile uint8_t *)0x413db2a8u)

static unsigned usb_last = 0xffu;
static volatile uint8_t usb_mode;

static void usb_report(void)
{
    const fn_u *f;
    unsigned configured = USB_CONFIGURED ? 1u : 0u;
    usb_mode = USB_MSG_MODE;
    if (configured == usb_last)
        return;
    usb_last = configured;
    for (f = std_usb; *f; f++)
        (*f)(configured);
}

void core_usb_msg(const uint8_t *msg)
{
    (void)msg;
    usb_report();
}

unsigned core_usb_mode(void)
{
    return usb_mode;
}

/* ============================== ev_boot_done =============================== */

void core_boot_done(void)
{
    const fn_void *f;
    crash_install();
    usb_report();
    for (f = std_boot_done; *f; f++)
        (*f)();
}

/* ================================ ev_idle ================================== */

/* ui.s's core_idle_start calls this over and over in the init task's idle
 * loop; with no handler it is stock's spin */
void core_idle_run(void)
{
    const fn_void *f;
    for (f = std_idle; *f; f++)
        (*f)();
}

/* =============================== ev_key_leds =============================== */

/* 0x400a0cec(ui, mask): the views (0x4007917a) and stock's defaults
 * (0x400a0768) set the LEDs through 0x40095fc0, then 0x40095c50 sends what
 * changed; handlers run between the two, so an LED a handler sets stays over
 * the view's. mgr is the UI object; core_led_set does not need it. */
void core_key_leds(void *mgr)
{
    const fn_ptr *f;
    for (f = std_key_leds; *f; f++)
        (*f)(mgr);
}

/* stock's LED set (0x40095fc0(led, colour)), under its LED mutex: led a
 * logical LED (0..192, as pads.c's core_led), colour the panel's palette */
typedef void (*led_set_t)(int32_t led, int32_t colour);
#define LED_SET ((led_set_t)0x40095fc0u)
#define LEDS 193

void core_led_set(void *mgr, unsigned led, unsigned colour)
{
    (void)mgr;
    if (led >= LEDS)
        return;
    LED_SET((int32_t)led, (int32_t)(colour & 0xffu));
}

/* ================================= ev_mode ================================= */

/* the UI state (0x401816f4 makes it once and keeps it at 0x419f28d8): its
 * mode bytes, as the setters write them (0x400362da grid record, 0x4003638a
 * live record, 0x400363cc the +0x16a mode, which also sets grid record) */
#define UI_STATE (*(uint8_t *volatile *)0x419f28d8u)
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

/* 0x400a906c(set, id, value, track, rec, notify): every class of the four
 * parameter sets (vtables 0x401dcda8, 0x401dce24, 0x401dcea0, 0x401dcf1c)
 * sets a value through it: the panel's turn (+0x2c), MIDI's NRPN (+0x60) and
 * CC (+0x64) and the other edits. It clamps the value, stores it when it
 * changed (+0x24, +0x70), sends PARAM OUTPUT when notify is set (+0x5c) and
 * returns the stored value (+0x28). It runs in the UI task and the MIDI
 * task, so a set is queued and its handlers run at the next tick in the UI
 * task. A set that becomes a lock (a trig held, live recording: the sets'
 * +0x30) does not come here. */
typedef int32_t (*pset_t)(void *set, int32_t id, int32_t value, int32_t track, int32_t rec, int32_t notify);
extern int32_t core_pset_stock(void *set, int32_t id, int32_t value, int32_t track, int32_t rec, int32_t notify);

#define SETS 64                         /* a power of two */
static struct { uint8_t track; uint16_t id; int32_t value; } sets[SETS];
static volatile unsigned set_head, set_tail, set_dropped;

static void set_queue(unsigned track, int32_t id, int32_t value)
{
    unsigned short sr;
    unsigned h;
    if (!std_param_set[0] || id < 0 || track >= TRACK_COUNT)
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

int32_t core_pset(void *set, int32_t id, int32_t value, int32_t track, int32_t rec, int32_t notify)
{
    int32_t r = core_pset_stock(set, id, value, track, rec, notify);
    set_queue((unsigned)track, id, r);
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

/* Each site is a call of stock's own routine (keep2 on a jsr or on the lea
 * that loads it), so pre comes before it and post only when it acted (a
 * nonzero byte in %d0). A take returns 0: stock shows nothing. Undo (UNDO
 * PASTE, UNDO CLEAR) does not go through these routines and brings none.
 *
 * Patterns: the pattern view's COPY/PASTE/CLEAR (0x400cc2aa, 0x400cc416,
 * 0x400cc4ea) and the pattern select's, once per selected pattern (copy in
 * 0x40040056, paste 0x4003f296, clear 0x4003f178): copy 0x400abe4c(pattern),
 * paste 0x400ac062(pattern, 1), clear 0x400ac188(pattern, 1, 1).
 * Tracks (grid record only, the active track): copy 0x400c2f68(track
 * pattern), paste 0x400c47c0(tp, 1), clear 0x400c49b6(tp, 1).
 * Kits (the kit list's PASTE and CLEAR, on YES): paste 0x400a806c(kits,
 * slot), clear 0x400a6cdc(kits, slot); slot 0..127. */
typedef uint32_t (*clipfn_t)(uint32_t, uint32_t, uint32_t);
#define PATTERN_COPY ((clipfn_t)0x400abe4cu)
#define PATTERN_PASTE ((clipfn_t)0x400ac062u)
#define PATTERN_CLEAR ((clipfn_t)0x400ac188u)
#define TRACK_COPY ((clipfn_t)0x400c2f68u)
#define TRACK_PASTE ((clipfn_t)0x400c47c0u)
#define TRACK_CLEAR ((clipfn_t)0x400c49b6u)
#define KIT_PASTE ((clipfn_t)0x400a806cu)
#define KIT_CLEAR ((clipfn_t)0x400a6cdcu)

static int clip_pre(const struct core_clip *c)
{
    const fn_clip *f;
    for (f = std_clip; *f; f++)
        if ((*f)(1, c))
            return 1;
    return 0;
}

static void clip_post(const struct core_clip *c)
{
    const fn_clip *f;
    for (f = std_clip; *f; f++)
        (*f)(0, c);
}

/* noipa: gcc turns an inlined call of a constant address passed in as fn into
 * __builtin_unreachable */
__attribute__((noipa)) static uint32_t clip_op(clipfn_t fn, unsigned kind, unsigned op, unsigned track,
                                               unsigned slot, uint32_t a, uint32_t b, uint32_t c)
{
    struct core_clip k;
    uint32_t r;
    if (!std_clip[0])
        return fn(a, b, c);
    k.kind = (uint8_t)kind;
    k.op = (uint8_t)op;
    k.track = (uint8_t)track;
    k.slot = (uint8_t)slot;
    if (clip_pre(&k))
        return 0;
    r = fn(a, b, c);
    if (r & 0xffu)
        clip_post(&k);
    return r;
}

static unsigned clip_track(void)
{
    int t = core_current_track();
    return t >= 0 ? (unsigned)t : CLIP_NONE;
}

static unsigned clip_kit(uint32_t slot)
{
    return slot < 128 ? slot : CLIP_NONE;
}

uint32_t core_clip_pcopy(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(PATTERN_COPY, CLIP_PATTERN, CLIP_COPY, CLIP_NONE, CLIP_NONE, a, b, c);
}

uint32_t core_clip_ppaste(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(PATTERN_PASTE, CLIP_PATTERN, CLIP_PASTE, CLIP_NONE, CLIP_NONE, a, b, c);
}

uint32_t core_clip_pclear(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(PATTERN_CLEAR, CLIP_PATTERN, CLIP_CLEAR, CLIP_NONE, CLIP_NONE, a, b, c);
}

uint32_t core_clip_tcopy(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(TRACK_COPY, CLIP_TRACK, CLIP_COPY, clip_track(), CLIP_NONE, a, b, c);
}

uint32_t core_clip_tpaste(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(TRACK_PASTE, CLIP_TRACK, CLIP_PASTE, clip_track(), CLIP_NONE, a, b, c);
}

uint32_t core_clip_tclear(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(TRACK_CLEAR, CLIP_TRACK, CLIP_CLEAR, clip_track(), CLIP_NONE, a, b, c);
}

uint32_t core_clip_kpaste(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(KIT_PASTE, CLIP_KIT, CLIP_PASTE, CLIP_NONE, clip_kit(b), a, b, c);
}

uint32_t core_clip_kclear(uint32_t a, uint32_t b, uint32_t c)
{
    return clip_op(KIT_CLEAR, CLIP_KIT, CLIP_CLEAR, CLIP_NONE, clip_kit(b), a, b, c);
}

/* =============================== ev_pattern ================================ */

/* The clock interrupt applies a queued change (0x40099edc) and posts message
 * 0x1e to the UI task; the UI's selects post it too. Core compares the
 * current pattern (0x412f59ff) with the last it reported. Only pre 0
 * (PATTERN_CHANGED) on the Rytm so far. */
#define PATTERN_BYTE (*(volatile uint8_t *)0x412f59ffu)
static unsigned pattern_last = 0x100u;

static void pattern_seed(void)
{
    if (pattern_last > 0xffu)
        pattern_last = PATTERN_BYTE;
}

void core_pattern_msg(void)
{
    unsigned now = PATTERN_BYTE;
    const fn_pattern *f;
    if (pattern_last <= 0xffu && now != pattern_last)
        for (f = std_pattern; *f; f++)
            if ((*f)(0, now, PATTERN_CHANGED))
                break;
    pattern_last = now;
}

/* ================================= ev_view ================================= */

void core_view_report(int op, void *mgr, void *view)
{
    const fn_view *f;
    for (f = std_view; *f; f++)
        (*f)(op, mgr, view, 0);
}

/* 0x400796ec(mgr) removes the stack's last node (mgr+0x18) and calls no
 * on-remove: reported before it, when the stack is not empty */
void core_view_pop_last(uint8_t *mgr)
{
    uint8_t *last;
    if (!std_view[0] || !mgr)
        return;
    last = *(uint8_t **)(mgr + 0x18);
    if (last && last != mgr + 0x14)
        core_view_report(VIEW_POP, mgr, *(void **)(last + 8));
}

/* ================================= ev_mute ================================= */

/* 0x400b78d8(obj, track 0..12, on, send): the track mutes (bit t of the
 * word at +0x20 of obj's +0x28 virtual, core_track_mutes' bits), from the
 * mute screens, MIDI (NRPN 0xe5, CC), Overbridge. With the MIDI config's mute
 * mode (0x400b584a) 2 a call that does not send changes nothing, and is not
 * reported. */
typedef uint32_t (*app_t)(void);
typedef uint32_t (*cfg_t)(uint32_t);
#define APP ((app_t)0x4017fa0au)
#define MIDI_CONFIG ((cfg_t)0x400ae49eu)
#define MUTE_MODE ((cfg_t)0x400b584au)

int core_mute_pre(int32_t track, int32_t on, int32_t send)
{
    const fn_mute *f;
    if (!std_mute[0] || (uint32_t)track >= TRACK_COUNT)
        return 0;
    if (!(send & 0xff) && MUTE_MODE(MIDI_CONFIG(APP())) == 2)
        return 0;
    for (f = std_mute; *f; f++)
        if ((*f)((unsigned)track, (on & 0xff) ? 1u : 0u, MUTE_SCOPE_TRACKS))
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
