/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-loader-a4mk2: the MODS page's calls and state until the page (page.c, Phase 3) lands. The
 * boot's tick calls dl_page_tick and dl_page_shown; a .mv waits for the page to take the last
 * move (dl_move_seen), which with no page is at once. page.c replaces this file. */
extern volatile int dl_move_seen;
static unsigned ticks;

unsigned dl_page_now(void)
{
    return ticks;
}

void dl_page_tick(void *ctrl)
{
    (void)ctrl;
    ticks++;
    dl_move_seen = 1;
}

int dl_page_shown(void)
{
    return 0;
}

/* the page's background check, as route.c's .status reads it: none */
volatile int dl_check_n = -1;
volatile int dl_check_all;
char dl_check_dir[2];
