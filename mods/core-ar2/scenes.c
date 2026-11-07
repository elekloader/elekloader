/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.8: mod parameters in scenes and performances (README, "Mod
 * p-locks"), the third part of the lock service. Every address is OS 1.74's.
 *
 * A kit keeps its scene locks and its performance locks in two lists of 48
 * entries, {value (8.8), track, code, scene or performance} (5 bytes; the
 * stored kit +2190 and +1950, the live kit the same), and stock edits them
 * through an object per list (vtables 0x401df974 and 0x401ddfe8: +20 set,
 * +24 get, +28 has, +32 relative, that is a performance's depth). A mod
 * parameter's lock is an entry with the parameter's lock code (42..127), so
 * it shares the 48 with stock's, and copy, paste, clear and the kit's copies
 * take it as they take stock's. Core:
 *
 *   - edits it: with a scene's or a performance's pad held, the turn and the
 *     push of a mod parameter's knob (0x400385f8, 0x400387ba; stock refuses
 *     the knob's id there) set the entry, from the sound's value for a scene
 *     and from 0 for a performance, as stock does a stock knob's;
 *   - keeps it out of stock's tables: the audio side's lists (0x4011f728,
 *     0x4011f7e6) index a parameter table by track * 42 + 26 + code, so an
 *     entry with a code above 41 would write another track's parameter; core
 *     makes stock skip it (scenes.s), as it skips an empty entry;
 *   - hands it to mods: core_scene_value and core_perf_offset read the
 *     active kit's lists;
 *   - keeps it on the unit: a kit's SysEx dump (0x4000f2a2) carries an empty
 *     entry in its place, and a kit loaded from a project of a layout this
 *     build does not read loses its mod entries (store.c), since codes are a
 *     build's.
 *
 * A parameter above 127 (lock.c, core_lock_shift) is locked and given a
 * depth in its coarse steps (value >> shift, 0..127), as stock's fine
 * parameters keep only whole steps here: the kit's file keeps an entry's
 * whole step only.
 *
 * The LFO destination list holds mod parameters (lock.c); the velocity and
 * aftertouch lists, built by the same routine, do not (lock.s), since their
 * routes index the same table.
 *
 * core-ar2 0.9: the FX track's (track 12) kit parameters too. Stock indexes
 * the FX track's entries as track 12's (12 x 42 + 26 + code), so the same
 * skip keeps them out of its table; the kit's save and load pass a code of
 * 42..127 with track 12 through (scenes.s); a FX knob's turn and push with a
 * pad held set the entry from the kit's value.
 */

#include <stdint.h>

#define FIRST_CODE 42
#define LAST_CODE 127
#define ENTRIES 48
#define ENTRY 5
#define PERF_LIST 1950
#define SCENE_LIST 2190
#define ACTIVE_SCENE 2430
#define LIVE_KITS 0x412558e3u
#define LIVE_KIT_SIZE 2531
#define WORKING_KIT_INDEX ((const volatile unsigned char *)0x405c8f20 + 0x19d3f7)
#define AUDIO_KIT (*(const volatile unsigned *)0x800067b8)   /* the kit the audio side plays */
#define PERF_OFF (*(const volatile unsigned *)0x8000ed74)    /* stock skips the performances */
#define PRESSURE ((const volatile int16_t *)0x8000f774)      /* each performance's pressure */
#define SCENE_VT 0x401df974u
#define PERF_VT 0x401ddfe8u
#define CURRENT_TRACK ((int (*)(unsigned))0x400b693e)
#define FX_TRACK 12
#define KIT_DUMP ((int (*)(unsigned char *, unsigned char *, int))0x400088d6)
#define DUMP_PERF 2114                          /* the SysEx kit's lists: 4 bytes an entry */
#define DUMP_SCENE 2327

struct core_lock_param;
const struct core_lock_param *core_lock_by_id_in(int id, unsigned *code, int kit);
int core_lock_stock_takes(int id, const unsigned char *s);
int core_lock_fx_takes(int id);
int core_lock_on_track(const struct core_lock_param *p, int track);
unsigned core_lock_field(const struct core_lock_param *p);
int core_lock_min(const struct core_lock_param *p);
int core_lock_max(const struct core_lock_param *p);
int core_lock_shift(const struct core_lock_param *p);
int core_lock_code(unsigned field);
const struct core_lock_param *core_lock_by_code(unsigned code);
unsigned core_store_get(unsigned field, void *item, int index);

static int mod_entry(const unsigned char *e)
{
    return e[2] <= 12 && e[3] >= FIRST_CODE && e[3] <= LAST_CODE;
}

/* ---- for mods ---- */

static const unsigned char *audio_kit(void)
{
    unsigned k = AUDIO_KIT;
    return k < 128 ? (const unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k : 0;
}

/* for smooth.c (core-ar2 0.9): drum track's sound in the kit the audio side
 * plays, or the kit itself for the FX track, or 0 */
const unsigned char *core_audio_sound(int track)
{
    const unsigned char *kit = audio_kit();
    return kit && (unsigned)track < 12 ? kit + 42 + 152 * track : kit && track == FX_TRACK ? kit : 0;
}

/* does the field's parameter belong on the track (lock.c)? */
static int on_track(int code, int track)
{
    return core_lock_on_track(core_lock_by_code(code), track);
}

/* (0.10) for smooth.c: the kit the audio side plays, or 0 */
const unsigned char *core_audio_kit(void)
{
    return audio_kit();
}

/* (0.10) the two lists by the code, for a parameter p already known to be on
 * the track, in the kit the audio side plays: core_param_value looks the code
 * and its parameter up once and asks these */
int core_scene_value_in(const unsigned char *kit, int track, int code, const struct core_lock_param *p)
{
    const unsigned char *e;
    int i;
    signed char scene;
    if (!kit || (scene = kit[ACTIVE_SCENE]) < 0)
        return -1;
    for (i = 0, e = kit + SCENE_LIST; i < ENTRIES; i++, e += ENTRY)
        if ((signed char)e[4] == scene && e[2] == track && e[3] == code)
            return e[0] << core_lock_shift(p);
    return -1;
}

/* as stock adds a performance's lock (0x40120fde): depth times the pad's
 * pressure, a fraction of 0x8000 */
int core_perf_offset_in(const unsigned char *kit, int track, int code, const struct core_lock_param *p)
{
    const unsigned char *e;
    int i, sum = 0;
    if (!kit || PERF_OFF)
        return 0;
    for (i = 0; i < 12 && !PRESSURE[i]; i++)
        ;
    if (i == 12)                                /* (0.9) no pad pressed: nothing to add */
        return 0;
    for (i = 0, e = kit + PERF_LIST; i < ENTRIES; i++, e += ENTRY)
        if (e[2] == track && e[3] == code && e[4] < 12)
            sum += (int16_t)(e[0] << 8 | e[1]) * PRESSURE[e[4]] >> 15;
    return sum << core_lock_shift(p);
}

int core_scene_value(int track, unsigned field)
{
    int code = core_lock_code(field);
    if (!code || !on_track(code, track))
        return -1;
    return core_scene_value_in(audio_kit(), track, code, core_lock_by_code(code));
}

int core_perf_offset(int track, unsigned field)
{
    int code = core_lock_code(field);
    if (!code || !on_track(code, track))
        return 0;
    return core_perf_offset_in(audio_kit(), track, code, core_lock_by_code(code));
}

/* ---- editing, with a scene's or a performance's pad held ---- */

typedef int (*method_t)();
#define METHOD(o, off) (*(method_t *)(*(char **)(o) + (off)))

/* the view's list object while a pad is held (0x40036636), if a scene's or
 * a performance's */
static void *held(void *view)
{
    void *o = *(void **)(*(char **)((char *)view + 108) + 440);
    unsigned vt = o ? *(unsigned *)o : 0;
    return vt == SCENE_VT || vt == PERF_VT ? o : 0;
}

/* the knob's mod parameter on the view's track, with its code and sound,
 * or (0.9) on the FX track a kit parameter's, with the kit (midi.c too) */
const struct core_lock_param *core_lock_knob(void *view, int id, int *track, unsigned *code,
                                               unsigned char **s)
{
    unsigned k = (unsigned)WORKING_KIT_INDEX[0] << 24 | (unsigned)WORKING_KIT_INDEX[1] << 16 |
                 (unsigned)WORKING_KIT_INDEX[2] << 8 | WORKING_KIT_INDEX[3];
    const struct core_lock_param *p;
    if (k >= 128)
        return 0;
    *track = CURRENT_TRACK(*(unsigned *)((char *)view + 116));
    if ((unsigned)*track >= 13 || !(p = core_lock_by_id_in(id, code, *track == FX_TRACK)))
        return 0;
    *s = (unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k + (*track == FX_TRACK ? 0 : 42 + 152 * *track);
    return (*track == FX_TRACK ? core_lock_fx_takes(id) : core_lock_stock_takes(id, *s)) ? 0 : p;
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* 0x400385f8 (view, id, delta 8.8), a turn: 1 when core took it, -1 for
 * stock. A scene's lock moves a whole (coarse) step at least, in the
 * parameter's range; a performance's depth the same, either way. */
int core_scene_turn(void *view, int id, int delta)
{
    void *o = held(view);
    const struct core_lock_param *p;
    unsigned code;
    unsigned char *s;
    int track, v, rel, step, sh;
    if (!o || !(p = core_lock_knob(view, id, &track, &code, &s)))
        return -1;
    rel = (unsigned char)METHOD(o, 32)(o);
    sh = core_lock_shift(p);
    step = delta > 0 ? (delta + 255) >> 8 : delta < 0 ? -((255 - delta) >> 8) : 0;
    if ((unsigned char)METHOD(o, 28)(o, track, code))
        v = (int16_t)METHOD(o, 24)(o, track, code) >> 8;
    else if (rel) {
        METHOD(o, 20)(o, track, code, 0);
        return 1;
    } else
        v = core_store_get(core_lock_field(p), s, 0) >> sh;
    if (rel) {
        int span = (core_lock_max(p) - core_lock_min(p)) >> sh;
        v = clamp(v + step, -(span > 127 ? 127 : span), span > 127 ? 127 : span);
    } else {
        v = clamp(v + step, core_lock_min(p) >> sh, core_lock_max(p) >> sh);
    }
    METHOD(o, 20)(o, track, code, v << 8);
    return 1;
}

/* 0x400387ba (view, id), a push: the lock, if the knob has none yet */
int core_scene_push(void *view, int id)
{
    void *o = held(view);
    const struct core_lock_param *p;
    unsigned code;
    unsigned char *s;
    int track;
    if (!o || !(p = core_lock_knob(view, id, &track, &code, &s)))
        return -1;
    if ((unsigned char)METHOD(o, 28)(o, track, code))
        return 0;
    METHOD(o, 20)(o, track, code,
                  (unsigned char)METHOD(o, 32)(o)
                      ? 0
                      : (int)(core_store_get(core_lock_field(p), s, 0) >> core_lock_shift(p)) << 8);
    return 1;
}

int core_scale_show(const struct core_lock_param *p, int v);       /* scale.c (0.11) */
int core_scale_depth(const struct core_lock_param *p, int d);
int core_scale_depths_own(const struct core_lock_param *p);

#define DTCN0 (*(volatile unsigned *)0xfc07000c)
#define DEPTH_TICKS 528000              /* 4 ms of DMA timer 0, as core_held_step's */

/* (0.11) the last get that answered a performance's depth: its id and
 * time, for the text drawn just after it (stock draws a knob's value, then
 * its text) */
static int depth_id = -1;
static unsigned depth_at;

/* page_get_value (pages.s, before the mods' get): with a scene's or a
 * performance's pad held, a mod knob shows its lock there, as stock's knobs
 * do: a scene's lock, else (0) the sound's value; a performance's depth,
 * else 0. 1 and *value (8.8) when it answers. (0.11) A scene's lock on the
 * dial's scale (core_param_scales), a depth in dial steps. */
int core_held_get(void *view, int id, int *value)
{
    void *o = held(view);
    const struct core_lock_param *p;
    unsigned code;
    unsigned char *s;
    int track, rel, v;
    if (!o || !(p = core_lock_knob(view, id, &track, &code, &s)))
        return 0;
    rel = (unsigned char)METHOD(o, 32)(o);
    if ((unsigned char)METHOD(o, 28)(o, track, code)) {
        v = (int16_t)METHOD(o, 24)(o, track, code) >> 8 << core_lock_shift(p);
        *value = rel ? core_scale_depth(p, v) << 8 : core_scale_show(p, v);
    } else if (rel)
        *value = 0;
    else
        return 0;
    if (rel) {
        depth_id = core_scale_depths_own(p) ? -1 : id;
        depth_at = DTCN0;
    }
    return 1;
}

/* (0.11) what the view's held pad is: 0 none (or a trig), CORE_HELD_SCENE,
 * CORE_HELD_PERF */
int core_held_kind(void *view)
{
    void *o = view ? held(view) : 0;
    if (!o)
        return 0;
    return (unsigned char)METHOD(o, 32)(o) ? 2 : 1;  /* CORE_HELD_PERF, _SCENE */
}

/* (0.11) whether the text being drawn for id is a performance's depth that
 * core writes (lock.c, and core_held_text) */
int core_held_depth_now(int id)
{
    return id == depth_id && DTCN0 - depth_at <= DEPTH_TICKS;
}

/* (0.11) param_value_text's first word (pages.s, before the mods' text): a
 * performance's depth on a mod parameter's knob, as a signed number, as
 * stock writes its own knobs' depths; 1 when it wrote buf */
int core_held_text(void *obj, int id, int value, char *buf)
{
    unsigned v, d;
    int n = 0, digits = 0;
    (void)obj;
    if (!core_held_depth_now(id))
        return 0;
    if (value < 0) {
        buf[n++] = '-';
        value = -value;
    }
    v = (unsigned)value >> 8;
    for (d = 100; d > 1; d /= 10)
        if (v >= d || digits) {
            buf[n++] = '0' + v / d % 10;
            digits = 1;
        }
    buf[n++] = '0' + v % 10;
    buf[n] = 0;
    return 1;
}

/* ---- keeping codes on the unit ---- */

/* 0x4000f2a2: a kit's SysEx dump (buffer, kit, a). Stock writes each entry
 * with the code turned into its SysEx number, which a code above 41 has
 * none of; core writes an empty entry in its place. */
int core_kit_dump_out(unsigned char *buf, unsigned char *kit, int a)
{
    int r = KIT_DUMP(buf, kit, a), i, l;
    unsigned char *d;
    if (buf && kit)
        for (l = 0; l < 2; l++)
            for (i = 0; i < ENTRIES; i++)
                if (mod_entry(kit + (l ? SCENE_LIST : PERF_LIST) + ENTRY * i)) {
                    d = buf + (l ? DUMP_SCENE : DUMP_PERF) + 4 * i;
                    d[0] = d[2] = d[3] = 0;
                    d[1] = 0xff;
                }
    return r;
}

/* a kit of another layout (store.c): its mod entries go, emptied as stock
 * empties one (0x400b8822) */
void core_kit_forget(unsigned char *kit)
{
    int i, l;
    unsigned char *e;
    for (l = 0; l < 2; l++)
        for (i = 0, e = kit + (l ? SCENE_LIST : PERF_LIST); i < ENTRIES; i++, e += ENTRY)
            if (mod_entry(e)) {
                e[0] = e[1] = 0;
                e[2] = e[3] = 0xff;
                e[4] = 0;
            }
}
