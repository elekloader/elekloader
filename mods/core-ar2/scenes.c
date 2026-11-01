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
#define KIT_DUMP ((int (*)(unsigned char *, unsigned char *, int))0x400088d6)
#define DUMP_PERF 2114                          /* the SysEx kit's lists: 4 bytes an entry */
#define DUMP_SCENE 2327

struct core_lock_param;
const struct core_lock_param *core_lock_by_id(int id, unsigned *code);
int core_lock_stock_takes(int id, const unsigned char *s);
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

/* a code's coarse shift (0 for a parameter up to 127) */
static int shift(int code)
{
    const struct core_lock_param *p = core_lock_by_code(code);
    return p ? core_lock_shift(p) : 0;
}

static const unsigned char *audio_kit(void)
{
    unsigned k = AUDIO_KIT;
    return k < 128 ? (const unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k : 0;
}

/* for smooth.c (core-ar2 0.9): drum track's sound in the kit the audio side
 * plays, or 0 */
const unsigned char *core_audio_sound(int track)
{
    const unsigned char *kit = audio_kit();
    return kit && (unsigned)track < 12 ? kit + 42 + 152 * track : 0;
}

int core_scene_value(int track, unsigned field)
{
    const unsigned char *kit = audio_kit(), *e;
    int code = core_lock_code(field), i;
    signed char scene;
    if (!kit || !code || (unsigned)track >= 12 || (scene = kit[ACTIVE_SCENE]) < 0)
        return -1;
    for (i = 0, e = kit + SCENE_LIST; i < ENTRIES; i++, e += ENTRY)
        if ((signed char)e[4] == scene && e[2] == track && e[3] == code)
            return e[0] << shift(code);
    return -1;
}

/* as stock adds a performance's lock (0x40120fde): depth times the pad's
 * pressure, a fraction of 0x8000 */
int core_perf_offset(int track, unsigned field)
{
    const unsigned char *kit = audio_kit(), *e;
    int code = core_lock_code(field), i, sum = 0;
    if (!kit || !code || (unsigned)track >= 12 || PERF_OFF)
        return 0;
    for (i = 0; i < 12 && !PRESSURE[i]; i++)
        ;
    if (i == 12)                                /* (0.9) no pad pressed: nothing to add */
        return 0;
    for (i = 0, e = kit + PERF_LIST; i < ENTRIES; i++, e += ENTRY)
        if (e[2] == track && e[3] == code && e[4] < 12)
            sum += (int16_t)(e[0] << 8 | e[1]) * PRESSURE[e[4]] >> 15;
    return sum << shift(code);
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

/* the knob's mod parameter on the view's track, with its code and sound
 * (midi.c too) */
const struct core_lock_param *core_lock_knob(void *view, int id, int *track, unsigned *code,
                                               unsigned char **s)
{
    unsigned k = (unsigned)WORKING_KIT_INDEX[0] << 24 | (unsigned)WORKING_KIT_INDEX[1] << 16 |
                 (unsigned)WORKING_KIT_INDEX[2] << 8 | WORKING_KIT_INDEX[3];
    const struct core_lock_param *p = core_lock_by_id(id, code);
    if (!p || k >= 128)
        return 0;
    *track = CURRENT_TRACK(*(unsigned *)((char *)view + 116));
    if ((unsigned)*track >= 12)
        return 0;
    *s = (unsigned char *)LIVE_KITS + LIVE_KIT_SIZE * k + 42 + 152 * *track;
    return core_lock_stock_takes(id, *s) ? 0 : p;
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

/* page_get_value (pages.s, before the mods' get): with a scene's or a
 * performance's pad held, a mod knob shows its lock there, as stock's knobs
 * do: a scene's lock, else (0) the sound's value; a performance's depth,
 * else 0. 1 and *value (8.8) when it answers. */
int core_held_get(void *view, int id, int *value)
{
    void *o = held(view);
    const struct core_lock_param *p;
    unsigned code;
    unsigned char *s;
    int track;
    if (!o || !(p = core_lock_knob(view, id, &track, &code, &s)))
        return 0;
    if ((unsigned char)METHOD(o, 28)(o, track, code))
        *value = ((int16_t)METHOD(o, 24)(o, track, code) >> 8 << core_lock_shift(p)) * 256;
    else if ((unsigned char)METHOD(o, 32)(o))
        *value = 0;
    else
        return 0;
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
