| SPDX-License-Identifier: GPL-2.0-or-later
| core-dn2 (Digitone II, OS 1.12): the OS's static tasks (core-dt2's system.s).
        .text                           | after a core's C code: core-dn2 0.4's code keeps its place
| ---- core_tasks: for a loader that writes the OS's code ---------------------
| The OS's static tasks, for a loader that must know no task is stopped
| where it writes: {count, the init task's row, the TCB's saved-SP offset},
| then per task {TCB, stack's low end, stack size}, as each call of the task
| create (0x400012c8) pushes them (read in digikit's emulator, a cold boot of
| 1.12). The init task (row 1, 0x400cedfc) runs ev_card_ready; the tasks
| created after it (rows 4 on) have a saved SP outside their stacks until
| they are.
        .balign 4
        .globl  core_tasks
core_tasks:
        .long   14, 1, TCB_SP
        .long   0x424398ac, 0x42439900, 0x4000   | prio 0 (0x400ced18)
        .long   0x4243d900, 0x4243d954, 0x4000   | init, prio 1 (0x400cedfc)
        .long   0x4058cee4, 0x4058cf38, 0x1000   | prio 9 (0x40000ea0)
        .long   0x4058e604, 0x4058e658, 0x0800   | timers, prio 10 (0x40002a46)
        .long   0x42c46624, 0x42c42624, 0x4000   | prio 7 (0x400d3eea)
        .long   0x46679e7c, 0x44618484, 0x4000   | prio 8 (0x4012ac88)
        .long   0x445e7764, 0x445e77b8, 0x8000   | MIDI, prio 7 (0x40120a02)
        .long   0x4059e1c0, 0x4059ea34, 0x28000  | UI, prio 6 (0x4002ed34)
        .long   0x42c6a1d0, 0x42c6a224, 0x4000   | worker, prio 2 (0x400f27ce)
        .long   0x42c6e224, 0x42c6e278, 0x4000   | worker, prio 3 (0x400f28e6)
        .long   0x446245f4, 0x44636a68, 0x8000   | prio 5 (0x401337c4)
        .long   0x44624580, 0x4462da68, 0x8000   | prio 4 (0x40133918)
        .long   0x4462450c, 0x44624a68, 0x8000   | prio 4 (0x40133a3e)
        .long   0x4664bc48, 0x47fe0000, 0x20000  | the boot task, on the boot stack
