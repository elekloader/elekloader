/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2: core-ar2 0.11's moments on the Analog Four MKII (README,
 * "Moments"; analog-four-mk2/moments.h). Every address is OS 1.56's.
 *
 *   - core_project_name: the live project's name.
 *   - the UI's current kit, track and pattern, with their items for
 *     core_store_get and core_store_set;
 *   - ev_kit: a kit made the UI's current one (the active-item setter
 *     0x4016796c on the kit object) and a whole kit pushed to the engine
 *     (0x401012ec), at the next ev_tick.
 */

#include <stdint.h>

/* the live project (the project model's vtable +0x28 returns it): its name
 * the first 16 bytes, which the file save (0x4001141e) copies to the
 * image's +8; the working pattern at +0x10, then the 128 patterns, the kits
 * as edited at +0xb95cea and as last saved at +0xbe0cea */
#define LIVE_PROJECT ((unsigned char *)0x40653c4c)
#define PROJECT_NAME ((const char *)LIVE_PROJECT)
#define LIVE_PATTERNS (LIVE_PROJECT + 0x10 + LIVE_PATTERN_SIZE) /* pattern 0, after the working one */
#define LIVE_PATTERN_SIZE 0x16fda
#define LIVE_KITS (LIVE_PROJECT + 0xb95cea)      /* the kits as edited */
#define LIVE_KIT_SIZE 0x960
#define LIVE_KIT_SOUNDS 0x1c
#define LIVE_SOUND_SIZE 0x152

/* the project model, in the shared_ptr its get-or-create (0x401494de) fills,
 * 0 until then; never call that from here. Its kit object at +0x16c (the
 * class whose vtable is 0x401a13a4) holds the UI's current kit at +0x44,
 * which the active-item setter writes (0x4016796c, vtable +0x68) and +0x6c
 * returns; +0x10 points to the kits as edited. Its selection object at +48
 * gives the current track (0x400adbd2: 0..3 the synth tracks, 4 FX, 5 CV). */
#define MODEL (*(unsigned char *volatile *)0x418717c8)
#define MODEL_KITS 0x16c
#define ACTIVE_INDEX 0x44
#define SELECTION 48
#define SELECTION_TRACK ((int (*)(void *))0x400adbd2)
/* the pattern view's model, made by its get-or-create (0x4014b292), 0
 * until then; 0x40020ffa(it) is the UI's pattern, 0..127: the sequencer's
 * (0x80006acc) or the playing song row's, as stock's RELOAD KIT reads it */
#define PATTERN_MODEL (*(void *volatile *)0x418717f0)
#define UI_PATTERN ((int (*)(void *))0x40020ffa)

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

/* the live project's name: 16 bytes, not always zero-terminated */
const char *core_project_name(void)
{
    return PROJECT_NAME;
}

/* ---- the current kit, track, pattern ---- */

int core_current_kit(void)
{
    unsigned char *m = MODEL;
    int k;
    if (!m)
        return -1;
    k = *(volatile int *)(m + MODEL_KITS + ACTIVE_INDEX);
    return (unsigned)k < 128 ? k : -1;
}

/* live kit k (0..127) as edited, the item core_handle_fields passes first
 * for it; 0 out of range */
void *core_kit_item(int k)
{
    return (unsigned)k < 128 ? LIVE_KITS + LIVE_KIT_SIZE * k : 0;
}

void *core_current_kit_item(void)
{
    return core_kit_item(core_current_kit());
}

int core_current_track(void)
{
    unsigned char *m = MODEL;
    int t;
    if (!m)
        return -1;
    t = SELECTION_TRACK(m + SELECTION);
    return (unsigned)t < 6 ? t : -1;
}

/* the current track's sound in the current kit; 0 on the FX and CV tracks
 * (their parameters are the kit's) */
void *core_current_sound_item(void)
{
    unsigned char *kit = core_current_kit_item();
    int t = core_current_track();
    return kit && t >= 0 && t < 4 ? kit + LIVE_KIT_SOUNDS + LIVE_SOUND_SIZE * t : 0;
}

int core_current_pattern(void)
{
    void *v = PATTERN_MODEL;
    int p;
    if (!v || !MODEL)
        return -1;
    p = UI_PATTERN(v);
    return (unsigned)p < 128 ? p : -1;
}

/* live pattern p (0..127), the item core_handle_fields passes for it; 0 out
 * of range */
void *core_pattern_item(int p)
{
    return (unsigned)p < 128 ? LIVE_PATTERNS + LIVE_PATTERN_SIZE * p : 0;
}

void *core_current_pattern_item(void)
{
    return core_pattern_item(core_current_pattern());
}

/* ---- ev_kit, delivered at ev_tick (UI task) ---- */

typedef void (*ev_kit_t)(int32_t kit, int32_t how);
extern const ev_kit_t ev_kit[];

#define KIT_SELECTED 1
#define KIT_PUSHED 2

static volatile int selected = -1;              /* the last select's kit, -1: none */
static volatile unsigned pushed[4];             /* the kits pushed since the last tick */
static volatile unsigned pushed_other;          /* a push of data in no kit slot */

/* 0x4016796c(this, index): the active-item setter, which three classes
 * share; only the project model's kit object is the UI's kit. The engine
 * gets the kit through the push below, not here. */
void core_kit_select_body(void *obj, int index);

void core_kit_select(void *obj, int index)
{
    unsigned char *m = MODEL;
    int now;
    core_kit_select_body(obj, index);
    if (!m || obj != (void *)(m + MODEL_KITS))
        return;
    now = *(volatile int *)(m + MODEL_KITS + ACTIVE_INDEX);
    if ((unsigned)now < 128)
        selected = now;
}

/* 0x401012ec(kit data): a whole kit to the engine (RELOAD KIT, project load,
 * CREATE NEW, a SysEx kit in, a kit change). Any task. */
void core_kit_push_body(const unsigned char *kit);

void core_kit_push(const unsigned char *kit)
{
    unsigned o = (unsigned)(kit - LIVE_KITS);
    unsigned short sr;
    core_kit_push_body(kit);
    sr = irq_mask();
    if (kit >= LIVE_KITS && o % LIVE_KIT_SIZE == 0 && o / LIVE_KIT_SIZE < 128)
        pushed[o / LIVE_KIT_SIZE / 32] |= 1u << (o / LIVE_KIT_SIZE % 32);
    else
        pushed_other = 1;
    irq_restore(sr);
}

/* ev_tick (order 2): the events, in the UI task */
void core_moments_tick(void *ctrl)
{
    const ev_kit_t *f;
    unsigned p[4], other, i;
    unsigned short sr;
    int k;
    (void)ctrl;
    sr = irq_mask();
    k = selected;
    selected = -1;
    for (i = 0; i < 4; i++) {
        p[i] = pushed[i];
        pushed[i] = 0;
    }
    other = pushed_other;
    pushed_other = 0;
    irq_restore(sr);
    if (k >= 0)
        for (f = ev_kit; *f; f++)
            (*f)(k, KIT_SELECTED);
    for (i = 0; i < 128; i++)
        if (p[i / 32] >> (i % 32) & 1)
            for (f = ev_kit; *f; f++)
                (*f)((int32_t)i, KIT_PUSHED);
    if (other)
        for (f = ev_kit; *f; f++)
            (*f)(-1, KIT_PUSHED);
}
