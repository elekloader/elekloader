# ddr-canary (Analog Four MKII)

A test for the DDR the Analog Four MKII profile calls unused: the mods' area
after the mods' own data, and the two gaps the OS never addresses
(docs/DEVICES.md, "Analog Four MKII"):

| region | what it is |
|---|---|
| 0x448B6D50-0x44C00000 | above the RAM the OS clears at start |
| 0x456472C0-0x47BE0000 | above the OS's last fixed buffer (37.6 MB) |
| `__bss_end`-0x47C00000 | the mods' area, after the mods' code and data |

On the first `ev_tick` after boot it starts filling them with
address ^ 0xC0DEF00D, the pattern the Rytm's canary used, then reads them
back for as long as it runs. Both go through the uncached view (address +
0x08000000), so what it reads is what DDR holds. It does 64 KB a tick, about
20 ticks a second: the fill and each later sweep take about 35 s. A changed
word is counted and never written back.

Over every screen (y = 0 is the bottom row):

- a bar along the very top: the sweeps done, a pixel each, wrapping at 120.
  It stays empty for the first 35 s or so, while the fill runs;
- below it, a bar: the changed words the last sweep found, up to 120;
- below that, up to three dots: which regions (left to right as in the
  table) have ever had a changed word;
- below that, 32 dots: the first changed word's address, most significant
  bit at the left, a lit dot for a 1;
- in the top-right corner a 3x3 square that blinks while the mod runs, and
  under it one that lights and stays lit once any word has changed.

A clean run shows the sweep bar growing and nothing else but the blinking
square. It names fill_rect itself, at its 1.56 address.

Checked in an emulator (rytm-emu's A4 build: clean sweeps, and one word
written into the gap on purpose was caught at its address) and on a unit
(CA56, 21 Oct 2026: clean through a session of normal use (Overbridge included) and after a power
cycle).

```bash
# needs m68k-linux-gnu binutils and gcc (or set ELEKLOADER_CROSS to another prefix)
python -m elekloader.sdk.build mods/core-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.sdk.build examples/ddr-canary-a4 --stock Analog-Four_MKII_OS1.56.syx
python -m elekloader.patch --stock Analog-Four_MKII_OS1.56.syx \
    --mod mods/core-a4/out/core-0.2.elemod --mod examples/ddr-canary-a4/out/ddr-canary-0.1.elemod \
    --out a4-canary.syx --version CA56
```
