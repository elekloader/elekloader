/* SPDX-License-Identifier: GPL-2.0-or-later */
/* store-check: the smallest user of core-ar2's mod storage (core 0.3).
 *
 * mod.json declares a field in every scope, and elekloader gives each one a
 * symbol whose value is the field's descriptor. STORE CHECK in SETTINGS
 * writes a marker into every field of every item, through core_store_set:
 * the pattern tracks and patterns of the 128 live patterns (the working
 * pattern is left out: stock rebuilds it from the current pattern), the
 * sounds of every live kit and of the pool, every live kit, the four global
 * slots and the project. Seven columns at the bottom left of the screen, left
 * to right: pattern tracks, patterns, kit sounds, pool sounds, kits, globals,
 * project; each grows a pixel for every marker core_store_get no longer
 * reads back, up to 16. A 3x3 square at the right end of the same edge blinks while it runs.
 *
 * It also hears ev_project: to the right of the columns, a bar as long as the
 * number of project events so far (mod 16, two pixels each), and beside it a
 * dot when the last one was a load (none for CREATE NEW) and one dot for each
 * scope it dropped: global, track, pattern, project.
 */
#include "analog-rytm-mk2/store.h"

typedef void (*fillrect_t)(void *bmp, int x0, int y0, int x1, int y1, int colour);
#define FILLRECT ((fillrect_t)FILLRECT_AT)

#define LIVE 0x407c89cb
#define LIVE_PAT(p) ((void *)(LIVE + 24 + 85760 * (p)))
#define LIVE_KIT(k) ((unsigned char *)((k) < 0 ? LIVE + 0xa8cf18 : LIVE + 0xadc098 + 2531 * (k)))
#define LIVE_KIT_SOUND(k, s) ((void *)(LIVE_KIT(k) + 42 + 152 * (s)))
#define LIVE_POOL(i) ((void *)(LIVE + 0xb4b27c + 152 * (i)))

STORE_FIELD(sc_track);
STORE_FIELD(sc_pattern);
STORE_FIELD(sc_sound);
STORE_FIELD(sc_sound_b);
STORE_FIELD(sc_kit_a);
STORE_FIELD(sc_kit_b);
STORE_FIELD(sc_global_a);
STORE_FIELD(sc_global_b);
STORE_FIELD(sc_project_a);
STORE_FIELD(sc_project_b);

extern void core_additem(void *menu, const void *row);
extern void sc_label(void);

static volatile unsigned sc_ticks;
volatile unsigned sc_proj_n, sc_proj_how, sc_proj_dropped;

void sc_project(int how, unsigned dropped)
{
    sc_proj_how = how;
    sc_proj_dropped = dropped;
    sc_proj_n++;
}

void sc_tick(void *ctrl)
{
    if ((++sc_ticks & 7) == 0)
        *((volatile unsigned char *)ctrl + 0x20) = 1;
}

/* a marker for item n of a field, cut to the field's width, never 0 */
static unsigned mark(unsigned field, unsigned n)
{
    unsigned w = STORE_BITS(field), v = (n + 1) * 2654435761u ^ field;
    v = w < 32 ? v & ((1u << w) - 1) : v;
    return v ? v : 1;
}

/* mode 0 writes the markers, mode 1 counts the ones that read back wrong */
static int one(unsigned field, void *item, int index, unsigned n, int mode)
{
    if (mode == 0) {
        core_store_set(field, item, index, mark(field, n));
        return 0;
    }
    return core_store_get(field, item, index) != mark(field, n);
}

static int walk(int col, int mode)
{
    int a, b, bad = 0;
    switch (col) {
    case 0:
        for (a = 1; a < 129; a++)
            for (b = 0; b < 13; b++)
                bad += one(STORE(sc_track), LIVE_PAT(a), b, 13 * a + b, mode);
        break;
    case 1:
        for (a = 1; a < 129; a++)
            bad += one(STORE(sc_pattern), LIVE_PAT(a), 0, a, mode);
        break;
    case 2:
        for (a = -1; a < 128; a++)
            for (b = 0; b < 12; b++) {
                bad += one(STORE(sc_sound), LIVE_KIT_SOUND(a, b), 0, 12 * (a + 1) + b, mode);
                bad += one(STORE(sc_sound_b), LIVE_KIT_SOUND(a, b), 0, 12 * (a + 1) + b, mode);
            }
        break;
    case 3:
        for (a = 0; a < 128; a++) {
            bad += one(STORE(sc_sound), LIVE_POOL(a), 0, 2000 + a, mode);
            bad += one(STORE(sc_sound_b), LIVE_POOL(a), 0, 2000 + a, mode);
        }
        break;
    case 4:
        for (a = -1; a < 128; a++) {
            bad += one(STORE(sc_kit_a), LIVE_KIT(a), 0, a + 1, mode);
            bad += one(STORE(sc_kit_b), LIVE_KIT(a), 0, a + 1, mode);
        }
        break;
    case 5:
        for (a = 0; a < 4; a++) {
            bad += one(STORE(sc_global_a), 0, a, a, mode);
            bad += one(STORE(sc_global_b), 0, a, a, mode);
        }
        break;
    default:
        bad += one(STORE(sc_project_a), 0, 0, 0, mode);
        bad += one(STORE(sc_project_b), 0, 0, 0, mode);
        break;
    }
    return bad;
}

static void sc_select(void)
{
    int col;
    for (col = 0; col < 7; col++)
        walk(col, 0);
}

static int sc_nothing(void)
{
    return 0;
}

static const void *const sc_row[4] = {
    (const void *)sc_label, (const void *)sc_select,
    (const void *)sc_nothing, (const void *)sc_nothing,
};

void sc_settings(void *menu)
{
    core_additem(menu, sc_row);
}

void sc_draw(void *bmp, void *ctrl)
{
    int col, bad;
    (void)ctrl;
    FILLRECT(bmp, 0, 47, 60, 63, 0);
    for (col = 0; col < 7; col++) {
        bad = walk(col, 1);
        if (bad > 16)
            bad = 16;
        if (bad)
            FILLRECT(bmp, 8 * col, 64 - bad, 8 * col + 5, 63, 1);
    }
    FILLRECT(bmp, 64, 47, 100, 63, 0);
    if (sc_proj_n & 15)
        FILLRECT(bmp, 64, 62, 64 + 2 * (sc_proj_n & 15) - 1, 63, 1);
    if (sc_proj_how == CORE_PROJECT_LOADED)
        FILLRECT(bmp, 64, 58, 65, 59, 1);
    for (col = 0; col < 4; col++)
        if (sc_proj_dropped & STORE_DROPPED(col ? STORE_TRACK + col - 1 : STORE_GLOBAL))
            FILLRECT(bmp, 68 + 4 * col, 58, 69 + 4 * col, 59, 1);
    FILLRECT(bmp, 125, 61, 127, 63, (sc_ticks >> 4) & 1);
}
