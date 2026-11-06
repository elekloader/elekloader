/* SPDX-License-Identifier: GPL-2.0-or-later */
/* drive-hello: the smallest +Drive mod with an export and an event entry.
 *
 * The Drive Loader links it at boot into the +Drive area: its ev_tick entry is
 * merged into core-ar2's table, and drive_hello_ticks, its one export, is a
 * name the mods linked after it may import (examples/drive-hello-user-ar2).
 * drive_init is its own: the loader calls it once, after the link. */

unsigned drive_hello_ticks;

__attribute__((used)) static void drive_hello_tick(void)
{
    drive_hello_ticks++;
}

__attribute__((used)) static void drive_init(void)
{
    drive_hello_ticks = 0;
}
