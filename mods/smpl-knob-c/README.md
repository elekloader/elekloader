# smpl-knob-c

A companion of smpl-machines (Analog Rytm MKII): knob C's own value for the
SMPL machines that ask for one (`SMPL_OWN_C`), so turning it never changes
the sample stock plays. It holds the bits and the knob id; smpl-machines,
which imports its record weakly, does the rest (mods/smpl-machines/README.md,
"Knob C").

| | |
|---|---|
| needs | core-ar2 0.15 or later (knob faces on own ids), smpl-machines |
| sites | none |
| exports | `smpl_knob_c`: `{id, field}` (`analog-rytm-mk2/smpl.h`) |
| storage | `smpl_c`, 7 bits a sound, lockable (p-locks, LFO, velocity and aftertouch, a CC) |
| knobs | 1 |

Only a mod with such a machine lists it in `requires`. Every such machine
shares the one field, since a sound runs one machine at a time; a change of
machine writes the new one's default into it.
