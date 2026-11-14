# TELOS for the Octatrack: what was measured

Part of [README.md](README.md). Marks as there.

## 1. What was measured

### Boot order, stock 1.40C [M]

`ot_emu --watch-pc` (the timestamp is the instruction count), the card
`ot07/fx/card.img`, 4 s of live time:

| instructions | what | where |
|---|---|---|
| 4,270,944 | the boot site 0x4000050c calls 0x40001e50: **the DSP bootstrap upload**, before the OS (core's `.boot` runs here) | boot |
| 4,353,786 | 0x40000512, the SRAM init (`ev_boot_early`) | boot |
| 10,170,953 | the RTOS handoff, 10 tasks made | |
| 18,792,399 | 0x40025aa2/ac2: the defaults routine stores the bank pointer and clears settings bytes (first of two runs) | main |
| 42,051,355 | 0x4001fbd0: main's own store of the bank pointer (runs once) | main |
| 44,923,397 | 0x4001fc96: main's last init call (`ev_boot_done`) | main |
| 45,153,687 | the first boot screen draw | |
| 45,273,090 | 0x4001e030: the USB controller's init (runs once) | |
| **45,834,943** | **0x40061734: the card's first mount (`ev_card`, state 1, mode 1)** | sys task |
| 45,901,075 | the first UI tick a shop mod hooks (0x4005595c) | |
| 57,562,208 | the card event again (the remount) | |
| 57,598,736 | the engine task's first job: LOAD PROJECT | engine |
| 64,831,084 | 0x40025aa2/ac2 again, in the project load | |
| 101,091,381 | the first audio frame (with the sequencer's clock in the emulator) | frame |

### Reading files at the first mount [M]

The probe `probe/` (a mod on core-ot 0.42, `ev_card`) reads
`/ELEKLOADER/TELOS/ACTIVE` and a 256 KB file with core's file calls on the
first `ev_card` with a card mounted. Result on a card built with octabam's
`emu_card.py`: state 1, mode 1; both opens 1, both reads 1; the 16 bytes were
`octa303 1a2b3c4d`, the big file's word sum 0x7fff8000, the expected sum.
The engine task had not run a job yet, so nothing raced the file calls.

### The shop's mods [M]

The 34 `octabam2elemod` v1.1 files, and the companions in `mods/*-ot`:

- **Sites.** All 34 patch the firmware: 371 code sites (127 distinct
  addresses) and 364 data sites. None contributes to a core table, so none
  could be a TELOS mod under the Rytm's rules.
- **Early sites.** Of the 127 code addresses, 5 ran before the first mount:
  - **run once:**
    - 0x4001e030, USB init (USB CROSSBAR and the 12 USB IO mods);
    - 0x4001fbd0, main's bank-pointer store (MIDI SCENES);
  - **run again later, before anything depends on them:**
    - 0x40025aa2 and 0x40025ac2, also in every project load (MIDI SCENES,
      QUANTIZER);
    - 0x4009d1de, 80 more runs (MIDI SCENES).

  Of the data sites inside instructions (28), 2 ran before the mount and run
  again later (RLEN PLEN 0x40002c72, SYNTH 0x40031ece). The 186 data sites in
  tables are USB descriptors (read at USB init), DSP payload bytes (LO-FI AMF
  FIX, SYNTH), and menu and parameter tables whose first read is still to
  measure [O].
- **MIDI SCENES' once-only site is harmless.** Its hook `bank_inv`
  (octabam's `seam_bank.s`) stores the pointer, then resets its own state
  (`last_part = -1`, two arrays zeroed). The same hook runs again from the
  project load (0x40025aa2) before any of its other sites runs. So the
  early-site rule needs a "runs again before use" class, not just "ran".
- **Stock bytes.** The shop mods' `.run` holds 1,262 stock references
  (12,670 bytes), copies of firmware instructions their stubs replay. 14 mods
  copy bytes the flashed core or a companion has changed:
  - DIRECT JUMP: one, under a core-ot site;
  - QUANTIZER: 7, under intercepts' sites;
  - the 12 USB IO mods: 2 each, under usb-ot's sites.

  Copied from the running OS, they would be wrong; the stock shadow
  ([LOADING.md](LOADING.md)) gives the right bytes.

## 7. The shop's mods under TELOS [M]

**The base build used:** core 0.42, intercepts 0.2, machines-ot 0.2,
dspbus-ot 0.1, settings-ot 0.1, seq-events-ot 0.2 and trig-events-ot 0.2:
every companion except USB.

**The conversion.** The converter (`elekloader.sdk.octabam`) was rerun on
octabam `363861e` with `--bus --intercepts --machines` against core 0.42 and
those companions. This is the "core standard" form: hooks the core's bus,
intercepts or machines serve go there.
- **Converted: 22.** That is the 15 non-USB mods below and 7 USB ones.
- **Not converted:** the 12 USB IO remixes (their DSP part needs octabam's
  `dsp_asm`, not installed here; USB is flash-only anyway). The DSP and
  OCTAKIT modules, as always.
- **Changed by `--intercepts`:** QUANTIZER and SYNTH are now
  `-intercepts`. The rest come out as v1.1's files (`-bus` where v1.1 had
  it).

Each mod was linked with the base build on its own, every pair was
linked, and all of them together:

| mod | `.run`+`.bss` | v1.1 file (published) | converted file | early sites | stock shadow bytes |
|---|---|---|---|---|---|
| CC FEEDBACK | 272 | loads | loads | | |
| CC MAP | 784 | loads | loads | | |
| DIRECT JUMP | 360 | loads | loads | | 1 range (core) |
| FLEX SEEK BIND, ...-CTR | 24, 16 | loads | loads | | |
| LO-FI AMF FIX | 0 | loads | `feature` until DSP payloads are measured | | |
| MIDI SCENES | 9,328 | loads | loads | 0x4001fbd0 once; 0x40025aa2, 0x4009d1de again | |
| QUANTIZER | 3,740 | **`flashed`: overlaps intercepts at 0x40010212** | loads | | 7 ranges (intercepts) |
| RECORDER HOLD, SPACING | 332, 148 | loads | loads | | |
| REPITCH | 576 | loads | loads | | |
| RLEN PLEN | 300 | loads | loads | 0x40002c72 again | |
| SCENES P2 | 2,604 | loads | loads | | |
| SYNTH | 20,260 | **`flashed`: overlaps intercepts at 0x4000dfd4** | loads; its DELAY bytes are a data site to measure | 0x40031ece again | |
| TUNER | 22,384 | loads | loads | | |
| USB (18 files) | | flash only | flash only | 0x4001e030 once, descriptors | |

- **Together:** all 15 link with the base build except one pair: MIDI
  SCENES and SCENES P2 both claim the Parts' window bytes (as octabam
  refuses them). Without SCENES P2 all 14 link, about 61 KB. A 256 KB area
  holds the whole shop four times over, so the 1 MB default leaves room for
  new mods.
- **What needs converting for TELOS:**
  - QUANTIZER and SYNTH, to the `-intercepts` form, which a base build with
    intercepts needs whether or not TELOS is used. Done here (local files,
    `/root/elek/out/telos/conv`). Publishing them is an `octabam2elemod`
    release (v1.2), and yours to decide.
  - MIDI SCENES' one once-only early site needs an answer in the rules,
    not in its code: `bank_inv` runs again in the project load before
    anything reads its state ([MEASURED.md](MEASURED.md)). Two ways:
    1. The early table classes 0x4001fbd0 as "superseded" because the
       same store happens at 0x40025aa2 in every project load (a fact of
       the firmware, kept in the profile).
    2. The mod declares the site `late_ok` with that reason (a FORMAT.md
       field the converter writes).

    Recommend (1): it needs no change to the format or the files.
  - Nothing else. The `-bus` conversion of v1.1 already put TUNER, CC MAP
    and CC FEEDBACK on the bus.
