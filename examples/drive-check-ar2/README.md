# drive-check (Analog Rytm MKII, core-ar2 0.14)

A test for core-ar2's mod folders in the +Drive browser (`drive.c`,
docs/ADAPTING.md "Mod folders in the +Drive browser (core-ar2 0.14, Analog
Rytm MKII)"), on screen and in RAM. `check.c`'s header says what it does.

| where | what you see |
|---|---|
| SETTINGS > SAMPLES, the root | DRIVE CHECK after the +Drive's own entries |
| DRIVE CHECK, first visit | `..` only, then after about a second SUB, A.WAV 1000 B, B.WAV 2 MB |
| SUB | `..`, then C.WAV, D.WAV, E.WAV, 12 KB each; `..` goes back to DRIVE CHECK |
| LOAD TO PROJ on A.WAV, C.WAV, E.WAV | counted as loaded, nothing in the sample memory |
| LOAD TO PROJ on B.WAV, D.WAV | refused |
| CREATE DIR, RENAME, DELETE, MOVE HERE inside it | refused, as for any write-protected folder |

It is a test, not for a project you keep.

```bash
python -m elekloader.sdk.build examples/drive-check-ar2 --stock Analog-Rytm_MKII_OS1.74.syx
```
