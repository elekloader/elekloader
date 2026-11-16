/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-a4mk2 0.9: a restart a mod asks for (mods/core-a4mk2/README.md,
 * "Restarts"); core-dn1 5.0's calls of the same names. */
#ifndef ELEKLOADER_A4MK2_SYSTEM_H
#define ELEKLOADER_A4MK2_SYSTEM_H

/* Reboot from a task, with interrupts on: waits until no +Drive save, load
 * or transfer runs and keeps any from starting, lets the panel link and the
 * UART send what they hold, then resets as stock's restart does. Never
 * returns. A mod writing the +Drive under a lock of its own takes it first. */
extern void core_restart(void);
/* The same at once, without waiting: for an interrupt. Never returns. */
extern void core_restart_now(void);
/* core-a4mk2 0.10, from a task: repack the working project's image from the live project and
 * write it to the +Drive, as stock's OS upgrade does before its reboot, so the
 * next boot comes back on the project as it is now, mod fields included.
 * Returns; call it before core_restart, never holding the store or card. */
extern void core_project_keep(void);

#endif
