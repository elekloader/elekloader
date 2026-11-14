/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.8: core_sound_setup_toggle, a checkbox row in SOUND SETUP
 * (FUNC + OSC1) from an ev_sound_setup handler (analog-four-mk2/ui.h; the
 * sites are in sound.s). OS 1.56.
 *
 * The row is stock's own MenuItem (0x54 bytes, ITEM_CTOR), as F1 RESONANCE
 * BOOST's: a label, a YES toggle, a checkbox draw and an encoder adjust, each
 * a std::function with the plain pointer manager and the mod's struct
 * core_toggle as its payload (as core_setting_add's rows; stock calls each
 * with the functor's address first). The label is stdui.s's
 * core_setting_label (the name is the struct's first word). The checkbox is
 * drawn as BOOST's (0x400e5474): stock's icon for the value, 11 columns on. */

#include <stdint.h>
#include "analog-four-mk2/ui.h"

#define OP_NEW ((uintptr_t (*)(unsigned))0x400653b8)
#define ITEM_CTOR ((void (*)(void *, void *, void *, void *, void *, int, int))0x4005c7c4)
#define FN_MGR 0x40164940u
#define ICON_SET ((void *)0x4186fa58u)
#define ICON_FOR ((uintptr_t (*)(void *, int32_t, int32_t))0x4005b380)
#define DRAW_ICON ((void (*)(void *, uintptr_t, int32_t, int32_t, int32_t))0x4005aed4)
#define MENU_ADD_VT 8                   /* the menu's vtable: add a row */

struct fn { const void *payload[2]; uint32_t mgr; const void *invoke; };

extern void core_setting_label(void);   /* stdui.s */

static int toggle_get(const struct core_toggle *t)
{
    return t && t->get ? t->get(t) : -1;
}

static void toggle_yes(const struct core_toggle *const *f)
{
    const struct core_toggle *t = *f;
    int on = toggle_get(t);
    if (on >= 0 && t->set)
        t->set(t, !on);
}

static void toggle_adjust(const struct core_toggle *const *f, int32_t unused, int32_t step)
{
    const struct core_toggle *t = *f;
    int on = toggle_get(t);
    (void)unused;
    if (on >= 0 && step && t->set)
        t->set(t, step > 0);
}

/* stock's draw: (functor, -, a3, a4, a5), and BOOST's checkbox is
 * 0x4005aed4(a3, icon, a4 + 11, a5, 0) */
static void toggle_draw(const struct core_toggle *const *f, int32_t unused, void *a3, int32_t a4, int32_t a5)
{
    int on = toggle_get(*f);
    (void)unused;
    if (on < 0)
        return;
    DRAW_ICON(a3, ICON_FOR(ICON_SET, on ? 1 : 0, 0), a4 + 11, a5, 0);
}

void core_sound_setup_toggle(void *menu, const struct core_toggle *t)
{
    struct fn l = {{t, 0}, FN_MGR, (const void *)core_setting_label};
    struct fn c = {{t, 0}, FN_MGR, (const void *)toggle_yes};
    struct fn d = {{t, 0}, FN_MGR, (const void *)toggle_draw};
    struct fn h = {{t, 0}, FN_MGR, (const void *)toggle_adjust};
    void *it;
    if (!menu || !t || !t->name)
        return;
    it = (void *)OP_NEW(0x54);
    if (!it)
        return;
    ITEM_CTOR(it, &l, &c, &d, &h, -1, 8);
    (*(void (**)(void *, void *))(*(uint8_t **)menu + MENU_ADD_VT))(menu, it);
}
