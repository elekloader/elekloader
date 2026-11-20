| SPDX-License-Identifier: GPL-2.0-or-later
| core-dt2 (Digitakt II, OS 1.17): the OS's static tasks (system.s's core_tasks).
        .text                           | after a core's C code: core-dn2 0.4's code keeps its place
| ---- core_tasks: for a loader that writes the OS's code ---------------------
| The OS's static tasks, for a loader that must know no task is stopped
| where it writes: {count, the init task's row, the TCB's saved-SP offset},
| then per task {TCB, stack's low end, stack size}, as each call of the task
| create (0x400012c8) pushes them (read in digikit's emulator, a cold boot of
| 1.17). The init task (row 1, 0x400cc9c8) runs ev_card_ready and
| ev_fs_ready; the tasks created after it (rows 4 on) have a saved SP
| outside their stacks until they are.
        .balign 4
        .globl  core_tasks
core_tasks:
        .long   15, 1, TCB_SP
        .long   0x42940a58, 0x42940aac, 0x4000   | prio 0 (0x400cc8e4)
        .long   0x42944aac, 0x42944b00, 0x4000   | init, prio 1 (0x400cc9c8)
        .long   0x4096369c, 0x409636f0, 0x1000   | prio 9 (0x40000ea0)
        .long   0x40964dbc, 0x40964e10, 0x0800   | timers, prio 10 (0x40002a46)
        .long   0x4314d558, 0x43149558, 0x4000   | prio 7 (0x400d1a12)
        .long   0x47de1e28, 0x44e3bc74, 0x4000   | prio 8 (0x4012c590)
        .long   0x44e0cf78, 0x44e0cfcc, 0x8000   | MIDI, prio 7 (0x4011f08e)
        .long   0x405a0ef0, 0x405ad15c, 0x4000   | prio 5 (0x40153f34)
        .long   0x40966ee8, 0x4096775c, 0x28000  | UI, prio 6 (0x400337ba)
        .long   0x43172de4, 0x43172e38, 0x4000   | worker, prio 2 (0x400f0df2)
        .long   0x43176e38, 0x43176e8c, 0x4000   | worker, prio 3 (0x400f0f0a)
        .long   0x44e4afd4, 0x44e5d448, 0x8000   | prio 5 (0x401353dc)
        .long   0x44e4af60, 0x44e54448, 0x8000   | prio 4 (0x40135530)
        .long   0x44e4aeec, 0x44e4b448, 0x8000   | prio 4 (0x40135656)
        .long   0x47db3ed0, 0x47fe0000, 0x20000  | the boot task, on the boot stack
