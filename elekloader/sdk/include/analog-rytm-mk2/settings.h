/* SPDX-License-Identifier: GPL-2.0-or-later
 * core-ar2 0.16: a submenu from a SETTINGS row (mods/core-ar2/submenu.s,
 * mods/core-ar2/SKIPS.md, "Submenus (0.16)").
 *
 * A row a mod adds in ev_settings with core_additem opens a menu of rows of
 * its own: from the row's select, call core_submenu_open(menu, &sm), where
 * menu is the select's payload's first word (the menu the row is in). The
 * submenu is stock's own checkbox menu view (as MIDI CONFIG > SYNC), with
 * sm.title in its header and sm.rows in it, each built with core_additem as
 * in SETTINGS: its select and change get the submenu as their payload, so a
 * row redraws it with View::invalidate(submenu + 0x38). NO goes back to the
 * parent. Core builds it the first time and keeps it, so the cursor stays
 * where it was; the rows' callbacks are asked again on every draw.
 *
 * sm must stay in writable memory for good: core keeps the view in its last
 * three words. Call from the UI task only (a row's select or change).
 */
#ifndef ELEKLOADER_AR2_SETTINGS_H
#define ELEKLOADER_AR2_SETTINGS_H

#include <stdint.h>

struct core_submenu {
    const char *title;              /* the header */
    const void *const *rows;        /* core_additem rows: label, select, draw, change */
    uint32_t n;                     /* how many rows */
    const void *icon;               /* the header's icon, a stock bitmap, or 0 for none */
    void *core[3];                  /* core's: the parent, and the view's shared_ptr; zero them */
};

void core_submenu_open(void *menu, struct core_submenu *sm);

#endif
