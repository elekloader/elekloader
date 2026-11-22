/* SPDX-License-Identifier: GPL-2.0-or-later */
/* core-ar2 0.18: a restart a mod asks for, and the working project kept before
 * it (mods/core-ar2/README.md, "Restarts"); core-a4mk2's calls of the same names. */
#ifndef ELEKLOADER_AR2_SYSTEM_H
#define ELEKLOADER_AR2_SYSTEM_H

/* Reboot from a task, with interrupts on: waits until no +Drive write or
 * transfer runs and keeps any from starting, lets the panel link and the UART
 * send what they hold, then resets as stock's restart does. Never returns. A
 * mod writing the +Drive under a lock of its own takes it first. */
extern void core_restart(void);
/* The same at once, without waiting: for an interrupt. Never returns. */
extern void core_restart_now(void);
/* From a task: write the working project to the +Drive from the live project,
 * as stock's OS upgrade does before its reboot, so the next boot comes back on
 * the project as it is now, mod fields included. It works once a boot and
 * stops the power-fail write, so only core_restart may follow it. Returns;
 * never call it holding the +Drive. */
extern void core_project_keep(void);

#endif
