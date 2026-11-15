/* SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TELOS for the Octatrack: what menu.c knows of the card's mods, for projset.c. */
#ifndef TELOS_OT_H
#define TELOS_OT_H
#include "../drive-loader-ar2/dl.h"

#define NMOD 32
#define TITLE 16
struct cmod {
    char name[64];                       /* the file, below /ELEKLOADER/MODS */
    char id[DL_ID_MAX];
    char title[TITLE];
    u32 crc;
    u32 hid, hver;                       /* dl_hash of the id and of the version */
    u8 boot_on, on;                      /* in active at boot; ticked now */
    u8 state, code;                      /* the boot's record (DL_ST_*, DL_R_*) */
};
extern struct cmod telos_cm[NMOD];
extern int telos_ncm;

void telos_post_active(void);            /* APPLY: the ticked files into active, on the engine task */
void telos_redraw_later(void);           /* redraw the MODS rows once a dialog has gone */
void telos_say(const char *a, const char *b, const char *c, void (*answer)(int));
void telos_proj_boot(void);              /* projset.c, after the boot's link and the scan */
void telos_proj_tick(void);

#endif
