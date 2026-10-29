/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "digitone-mk1/core3.h"
static void open(void *brain, void *event, int32_t track)
{
    (void)brain; (void)event;
    core_sound_set(track, 66, 32 * 256); /* AMP attack, a persistent base edit */
}
const struct core_menu_item sound_edit = { .name = "SET ATTACK", .open = open };
