# Adding a device

Everything elekloader knows about a product is in one profile,
`elekloader/devices/<device>.py`, registered in `devices/__init__.py`
(`_all()`). The Digitakt mk1 profile is the example. The Digitone mk1's
(`digitone_mk1.py`) is a second device of the same file family, with its own
core. The Octatrack's (`octatrack.py`) is a second file family, with a core
and a hook bus of its own.

| field | what it is | how it was found for the Digitakt mk1 |
|---|---|---|
| `releases` | each supported stock OS: its `.syx` sha256, and its main OS section's sha256 and length | hash the file, and the section depacked |
| `sysex_id` | the device byte of its OS messages | the framing messages (`0x0A`) |
| `main_section`, `main_load` | which container section is the main OS, and where it runs | the section table (id 3, `0x40000400`) |
| `stage` | where the bootloader stages the main OS before unpacking it in place | the bootstrap's loader (`0x800005fe`: read to `0x40200000`, unpack to `0x40000400`) |
| `flash_at`, `flash_limit` | where the container sits in flash and must end | the bootstrap (`0x80000` .. `0x380000`) |
| `update_max` | maximum decoded ELE3 update bytes, including the 8-byte preamble and the container (seal included); `None` when unknown | unknown on the mk1; DN2's conservative USB cap is measured on a unit, not found in code |
| `trailer` | how the file is sealed: `None`, or `'hmac'` | mk1 has none |
| `hmac_key_from` | for `'hmac'`: the section and seed string the key is derived from (never the key itself) | none on the mk1 |
| `isa` | the main CPU's instruction decoder, for the boundary checks | ColdFire (`isa/coldfire.py`) |
| `areas` | memory that is free at run time: where a mod's regions may lie | the research on what the OS never touches |
| `ddr`, `sram_code`, `fast_table` | where the linker puts mods' code and data, fast code, and the table that copies it | the areas above |
| `core_ddr` | from a core's version on, the linker's area in place of `ddr` | core 4.0: `0x47BC0000-0x47C00000`, the gap between the sample pool's end and the audio input ring |
| `recovery` | how to get back to stock, shown to the user | FUNC at power-on |
| `container`, `version_len` | the file family (`'ele3'`, `'elek'`) and the version field's length | the container header |
| `protected` | main OS ranges no mod may change, with the reason | none on the mk1 |
| `relocatable` | data inside a protected range that the OS reaches only through the listed 4-byte operands and never writes, so a mod may serve its own copy through them (sdk.octabam does, for a poke there) | none on the mk1 |
| `keep` | main OS ranges that look free (they read zero, nothing points at them) but that the OS reads, with the reason: no site may write them, and every build must leave them stock | none on the mk1 |
| `blob_max` | a cap on the appended blob, if the DDR areas do not give one | none on the mk1 |
| `image_free` | zero runs inside the main OS that `fixed` code may take (sdk.build) | none on the mk1 |
| `reserve` | (unit, {name: (a, b)}): for a core that sizes its RAM reserve to the mods, the symbols the linker defines from n, the units the mods' RAM fills: a + b·n each | none on the mk1 (its `ddr` is free RAM) |
| `dsp_payloads`, `dsp_areas` | DSP code the main OS uploads at boot (tag: address, length), and the P words of a payload a mod frees by claiming a name, where the linker places DSP code (docs/FORMAT.md, "DSP code") | none on the mk1: its OS has no DSP code |
| `toolchain` | the compiler, assembler and flags the SDK uses | the CFW's build |

## The Digitakt II (1.17)

Experimental. Whole builds and the core (mods/core-dt2) boot in digikit's
emulator, and a build of core with examples/perform-direct works on a unit
(4 Oct 2026): sent with Elektron Transfer over USB (the in-OS upgrade), it
was accepted and booted, the unit reported the build's own version (PD10),
and the key swap and a PERSONALIZE row (in an earlier version of the mod)
worked. digikit (https://github.com/m-dwyer/digikit,
docs/findings/01-container-and-patching.md) mapped 1.15C and 1.16. Each fact
below was found again in 1.17.

- **Files.** The Digitakt mk1's family (ELE3, the same SysEx transport),
  device byte `0x14`. It has six sections: 5, 2 (the bootstrap, run at
  `0x80000400`; its `dest` is a version, `0x0201`), 3 (MAIN OS), 4 (the
  updater), 7 (the SHARC DSP's program) and 8.
- **Sealed.** After the last section's 16-byte padding comes a 32-byte
  HMAC-SHA256 of everything before it, inside the preamble's length.
  - The bootstrap checks it, and the content checksum, before it flashes;
    the OS checks it on an upgrade. A file that fails is refused with
    `Checksum failed` or `UPGRADE ABORTED`, and nothing is written.
  - The key: in section 2, the seed `"Master Overdrive"`, a NUL and a
    32-byte constant C; the key is C ^ sha256(seed) ^ sha256(reversed seed).
    `syx.seal_key` reads it from the user's stock file, so it is never
    stored here.
  - From its own main OS stream, the writer reproduces the stock file byte
    for byte, seal included.
- **Staging.** The bootstrap's loader (`0x80000596`) reads section 3 from
  flash, at its container offset + `0x80000`, to `0x40400000`, and unpacks
  it from there to `0x40000400` with its depacker (`0x80000432`). The mk1
  stages at `0x40200000`; this image, 3.27 MB depacked, runs past that.
  Stock's in-place gap is 2,065,240 bytes.
- **The unit's own depacker takes elekloader's stream.** Run in place in
  digikit's ColdFire core (23 Oct 2026), the bootstrap's depacker yields
  the stock main OS from elekloader's repack (1,179,664 stored bytes,
  against Elektron's 1,147,584). The earlier packer was also checked in
  Unicorn through digikit's harness (`emu/oracle.py`, with the 1.17 entry).
- **Flash.** The container starts at `0x80000`: the bootstrap's read above,
  and digikit's map of the OS's own reads. The OS keeps a store at
  `0x380000`-`0x400000`, with the mk1's code at its own addresses: it reads
  a 0x14-byte header at `0x380000`, and erases the sectors at `0x380000` and
  `0x3c0000` (26 and 12 call sites, from `0x400ca504`). So `flash_limit` is
  `0x380000`: 3 MB for the container, of which stock takes 1.48 MB. The
  bootstrap's receive path has no size check of its own, so this limit is
  elekloader's alone.
- **Checked in an emulator** (digikit's, `emu.checkpoint` from a cold
  boot, then `tools/emucheck.py` to 600M instructions; 4 Oct 2026):
  - Two elekloader builds, a repack of stock 1.17 (version `DT01`) and a
    whole build with one data site (`DT02`), pass every stage as stock does:
    the main screen, six tasks, the +Drive formatted, the same instruction
    count. The repack's screen at 650M is identical to stock's.
  - digikit's extractor reads both, and its checker passes their content
    checksum, seal, framing count and every packet checksum.
  - Not checked in the emulator: the bootstrap's own flash path (it serves
    the container to the OS's flash reads and boots the main OS directly).
    The unit above accepted a sealed build.
- **What writes the flash** (tests/test_digitakt2.py checks each against
  the stock file). The OS's SPI NOR driver writes through 0x40137cbe and
  erases through 0x40137d3a/0x40137da4 (256 KB) and 0x401373e6 (64 KB). Its
  only callers:
  - the settings store, 0x380000-0x400000 (above);
  - the in-OS upgrade, 0x4013ac54 (the route Transfer takes over USB): it
    runs the validator 0x400d9fb0 (content checksum, the build floor,
    the seal), then erases from 0x80000 block by block to cover the
    container and writes it there. A container that ends below 0x380000
    cannot make it erase past it: 0x380000 - 0x80000 is a whole number of
    256 KB units;
  - the factory console's upload (0x400caff0, "READY FOR OS" over the debug
    UART), which writes a container to 0x80000 or another image to
    0x40000-0x80000.
  None of them writes anything from the main OS image: unlike the Analog
  Rytm mk1's (#11), it holds no copy of the bootstrap to re-flash, and needs no
  `protected` range. The bootstrap itself is section 2, which every build
  keeps stock (version 0x0201: the bootstrap's own upgrade only runs for a
  greater one). The bootstrap's MIDI upgrade (the STARTUP menu) writes the
  container to 0x80000 too, and checks the content checksum and the seal
  first.
- **The version a unit reports** is its container's. The OS caches the
  container's first 0x20 bytes from flash 0x80000 (0x4013641c) and reads
  the build and version fields from there (0x40124978, the answer to
  Transfer's version request; an exception report); there is no version
  string in the main OS. So a build's 4-character version field is what
  the unit shows (a unit reported PD10 for a build so named), and the
  loader's default (`2.0a`) differs from stock's.
- **Recovery.** The startup menu (FUNC at power-on), TRIG 4 for OS UPGRADE,
  over MIDI only (Elektron's readme). It is in the bootstrap, which no build
  changes, and checks only the content checksum and the seal.
- **At reset** (`0x400004e8`), the OS:
  - copies `0x40312000-0x40318e80` to SRAM at `0x80000000` and
    `0x40318e80-0x4031ff60` to `0x80008000`, and zeroes the rest of each
    32 KB half;
  - clears DDR `0x40312000-0x47E28470`;
  - runs its stack down from `0x48000000`.
  The mk1's reset code, with the II's addresses.
- **Free memory, and the core** (mods/core-dt2):
  - There is no sample pool in its DDR to take from: samples go to the DSP
    over FlexBus (0x8C000000, digikit). The reset clears 0x40312000-
    0x47E28470 for the OS's static `.bss`, almost all of the 128 MB.
  - Above the clear, the OS reaches its DSP buffers only through the
    uncached alias (+0x08000000): 0x4FE30000-0x4FE7AE80, so 0x47E30000-
    0x47E7AE80. The boot stack runs down from 0x48000000.
  - Between them, 0x47E7B000-0x48000000: no decoded instruction (digikit's
    `tools/refscan.py`, 96.75% coverage) names it or its alias (the only
    hit, 0x47efffff, is the high word of the double FLT_MAX); it is zero in
    every rung of a cold boot; and a write watch over all of it, cached and
    uncached, saw no write from 400M to 808M instructions of a session
    with 37 key events. The core takes 0x47F00000-0x47F40000 (256 KB) from
    the middle, 520 KB clear of the buffers and 768 KB of the stack.
  - SRAM: the reset copies the image's tail into each 32 KB half and zeroes
    the rest. Past the copies, 0x80006E80-0x80008000 and 0x8000F100-
    0x80010000 are named by no decoded instruction. They are zeroed after
    the boot copier runs, so they are free at run time only, as on the mk1.
    A write watch over both saw no write from 400M to 781M instructions of
    a session with 24 key presses. `.fast` code goes to the tail: core-dt2
    declares `core_fast` and copies it on the first `ev_tick`.
  - The core does not start DTIM0, unlike the mk1's (`NO_DTIM0` in
    core.s). Nothing before the OS starts it on the II either, and ten OS
    sites read its counter: with it running, a cold boot with core alone
    jumped to 0 at 553M instructions in digikit's emucheck.
  - The core's events: `ev_tick`, `ev_draw`, `ev_key`, `ev_enc`,
    `ev_settings`, and the II's own `ev_personalize` (SETTINGS >
    PERSONALIZE, whose builder makes its rows with the same generic
    `MenuItem`). mods/core-dt2/README.md lists the sites and routines.
  - Checked in the emulator (cold boot, then emucheck to 600M): core 1.0
    alone passes every stage, with its screen at 650M identical to stock's;
    with a mod that inverts an 8x8 block on `ev_draw`, the screen differs
    from stock in exactly those 64 pixels. Each event was driven with a
    test mod: key and encoder counts, a SETTINGS row, and
    a PERSONALIZE checkbox row. examples/perform-direct (a key swap) works
    on a unit, with core's other events registered but not used there.
  - Not covered: anything a session in the emulator does not reach (no
    DSP, no samples, an empty +Drive) and the unit run did not use.

## The Digitone mk1 and Digitone Keys (1.43)

- **Files.** One `.syx` serves both. It is the Digitakt mk1's family (ELE3,
  the same SysEx transport and layout) with seven sections: 5, 2 (the
  bootstrap, with the startup menu), 3 (MAIN OS), 4 (the updater), 6, 7 (the
  second CPU's image, which renders the FM voices) and 8. The longer table
  moves the first section to `0xA0`; the reader and writer take that from
  the stock file. From its own main OS stream, the writer reproduces the
  stock file byte for byte.
- **Unpacking.** The bootstrap loads the updater, which reads section 3
  from flash (offset + `0x80000`) to `0x40200000` and depacks it in place to
  `0x40000400`: the Digitakt mk1's loader, instruction for instruction
  (`0x80003518` in the updater). Stock's in-place gap is 403,540 bytes.
- **Recovery.** The startup menu (FUNC at power-on) has 4 ... OS UPGRADE.
- **Checked in an emulator.** digikit's firmware check runs the file's own
  bootstrap and updater, then a cold boot and a scripted session. Stock 1.43,
  repacked by elekloader, passes it against stock: the updater accepts the
  stream, and the screens and the audio are identical.
- **Linkable mods.** Its core is `mods/core-dn1`: `core.s` with the
  Digitone's addresses. Its first eight sites and every routine they call are the
  Digitakt mk1's found again in 1.43. They are the same code instruction
  for instruction, bar the addresses in it. The render handler's frame is
  0xB8 bytes (0xA8 on the Digitakt), hence `RENDER_FRAME`. From 2.1
  (`voice.s`) it has a ninth site, the render's voice note-on at
  0x4009e928, for the Digitone-only `ev_voice_on`, and parameter slots
  (`params.s`): ids 182-184 for mods, through 63 more sites; mod pages
  (`pages.s`): pages 27-30, through two more; and project data
  (`projdata.s`): mods' blocks saved with the project, through five more.
  From 2.2 the Mod Menu (`menu.s`): holding a track key opens a menu of
  the mods' entries; the four UI sites go to its wrappers first. From 2.3
  the menu is a grid of the entries' icons. From 3.0 (`fw.s`) it exports
  the firmware locations mods use, with each OS's values, so a mod that
  names only those has an empty port.
  From 3.1 (`voices.s`, `params.s`, `menu.s`) it has what machines need: the
  render's voices, the parameter overrides and the Mod Menu's submenus. From
  3.2 (`audio.s`, `midi.s`) it has the exclusive audio and the MIDI CCs.
  From 3.3 (`leds.s`, `block.s`, `fw.s`) it has the key LEDs and each block's
  sound parameters, and exports more firmware locations: the C library, the
  text box, the transport, the active pattern and the project.
  From 4.0 it has insert audio (`audio.s`): an owner on the master stage's
  output, program changes (`midi.s`, `ev_midi_pc`), and the mods run from a
  bigger area (below).
  1.44 has all of these, at its own addresses (`ports`).
- **The DDR area** is the Digitakt's, `0x47BE0000-0x47C00000`:
  - The OS clears `0x4028E000-0x43229E60` at start, and its stack runs down
    from `0x48000000`.
  - No instruction operand in its code points between `0x43EB0000` and the
    stack.
  - In a settled emulator run, no DDR page from `0x43400000` to
    `0x47C00000` is ever mapped.
  - So the profile has a second area, `bulk` (`0x44000000-0x47BE0000`, from
  core-dn1 3.2), for
    mods' large buffers: a mod claims a piece as a region and uses it at
    run time. Nothing is placed there, and the OS never clears it.
  - From core-dn1 4.0 the mods run from `0x43EC0000-0x44000000`
    (1.25 MB, the profile's `core_ddr`): the OS's operands end at a
    384 KB ring, `0x43E472C0-0x43EA72C0`, whose end it compares at
    `0x43EA72C8` and `0x43EA72C9`.
- **Checked in the emulator:** with core alone, the build passes every
  stage, with every screen identical to stock.
  - The audio is identical to stock up to PLAY.
  - After PLAY it is the same sound, sample-shifted, because core's few
    cycles move when the scripted key presses land. Stock against itself
    with PLAY 0.3 ms later diverges in the same way.

## The Octatrack (1.40C)

- **Files.** One stock image serves the MKI and MKII. It comes as a `.syx`
  (the legacy SysEx transport) and as a `.bin` card file (ELUP: a
  word-feedback cipher with a checksum). Both carry one ELEK container,
  whose only section is the main OS, at `0x40000400`. `elek.py` rebuilds
  both stock files byte for byte from their own main OS stream.
- **Protected.** `0x400de1e0-0x400e21e0` holds the copy of the bootloader
  that the OS can re-flash. No site, blob or verified output may change it.
  - The OS's USB descriptor tables are its top 480 bytes, from
    `0x400e2000`. The device descriptor (18 bytes) is `relocatable`: the
    only pointer to it is the operand of `pea 0x400e2000` at `0x4001d82c`
    (GET_DESCRIPTOR), and nothing writes it. A mod may point that operand
    at its own copy. tests/test_octatrack.py checks, against the stock
    file, that no other four bytes outside the range point into it.
- **Flash.** The container sits at `0x4000` and must end below `0x200000`.
- **Not known yet.** Where the bootloader stages the image (`stage` is
  `None`), so the in-place unpack is not simulated.
- **Linkable mods.** There is no free RAM in stock: the OS gives the whole
  audio page arena (`0x40a955e0-0x46025de0`, 14,602 pages of 6,144 B) to
  samples and the recorders. The core, `mods/core-ot`, takes the arena's
  bottom pages, as sambanks/octabam's platform does: from 0.3 only those
  the mods' RAM fills (the profile's `reserve`), at most 1,707 (10 MB, all
  of them in 0.2), which is the device's `ddr`: `0x40a955e0-0x41495de0`.
  - It moves the arena's base past them with octabam's 27 arena writes
    (the base, base + one page, the page count, the fill limit and the
    clear length). tests/test_octatrack.py derives each from the page count.
    The base's literal at 0x400254f8 is left stock: it ends PURGE SAMPLES'
    walk over the banks, and moved, PURGE froze the unit (core-ot 0.1-0.3,
    octabam #753).
  - Its `.boot` is appended at the image's end (`0x4010fdf0`) and called
    from the boot site `0x4000050c`, in place of `0x40001e50`, which it
    calls first. It copies the run image to the reserve through the
    uncached alias (`+0x08000000`) and zeroes `.bss`.
  - Its hook bus (0.2) has six events of the Octatrack's own: the sys
    task's tick, each composed frame, the keys, the encoders, MIDI in and
    the frame interrupt (docs/ADAPTING.md, "The hook bus on the
    Octatrack"). Its sites keep clear of every byte the converted octabam
    modules patch, so those link with 0.2 as they did with 0.1.
  - The OS composes the screen twice before the boot site, so before the
    run image is in the reserve. The draw site goes through a gate placed
    (`fixed`) at the end of the zero run at `0x400c45b0`, which waits for
    `.boot` to have run.
  - `elekloader.sdk.octabam` converts octabam's ColdFire-only modules to
    such mods, and whole remixes, which is how USB AUDIO IN converts
    (docs/ADAPTING.md, 4b). With `--bus`, the hooks the bus serves (TUNER,
    CC FEEDBACK, CC MAP, USB AUDIO OUT's producer) subscribe to its events
    instead of patching their sites.
- **Free space inside the image** (`image_free`, octabam's measured zero
  runs): `0x400c45b0-0x400c4702`, `0x400d24e0-0x400d2ce0` and
  `0x400d64e0-0x400d7c3c`. octabam pins code in these runs, and `fixed`
  code may use them.
  - The second run starts at `0x400d24d0` in stock. Its first 16 bytes are
    kept stock (`keep`, below).
  - The third run starts at `0x400d64da` in stock. Its first six bytes are
    left out: they follow the table at `0x400d64a0`.
  - Left out entirely: `0x400d2ee6-0x400d3020` (a live descriptor), and
    everything above `0x400d8000` (the PROJECT subsystem's RAM).
- **Bytes kept stock** (`keep`): `0x400d24d0-0x400d24df`. The stock DELAY
  reads them, so a write there changes its output (octabam issue #771,
  measured under octabam's emulator port, not on a unit).
  - The DELAY reads `0x400d1ce0 + 4 * (x >> 6)` (`0x40003526`,
    `0x40003540`). That table has 508 words and ends at `0x400d24d0`.
  - At a slow tempo with a long TIME the index reaches 508, past the table,
    into these zeros. octabam puts its cap at 511, unmeasured; hence the 16
    bytes.
  - In octabam, SYNTH MACHINE's page cave, pinned at `0x400d24d0`, changed
    the stock DELAY's output. octabam's fix moved the page to DRAM (its
    `PLACE` setting) and keeps these bytes stock.
  - The linker refuses any site that writes them, sdk.build refuses
    `fixed` code over them, and the build's verify checks them against
    stock (tests/test_octatrack.py). sdk.octabam puts a cave octabam pins
    there in `.run` (docs/ADAPTING.md, 4b).
- **DSP code** (`dsp_payloads`, `dsp_areas`): the main OS uploads payload A
  (`0x400e2324`, 0x136cb bytes) to DSP core 0 and B (`0x400f59ef`, 0x12d05
  bytes) to core 1 at boot. Payload A's P memory is full, so the one area
  for mods' DSP code is SPATIALIZER's code, P:0xaa8-0xbac (261 words), freed
  by the mod that claims `dsp:harvest:SPATIALIZER`: the DSP bus,
  `mods/dspbus-ot`.
- **Checked in an emulator** (octabam's `ot_emu`): with core and a mod that
  puts a marker in `.run`, the boot reaches the RTOS handoff as stock does,
  `.boot` runs once, and the marker is in the reserve after boot.
- **On a unit** (an MKII, 3 Oct 2026): eight builds flashed from the card
  booted, each showing its own version.
  - The builds were core alone, then core with 19 of the converted octabam
    mods (irpina/octabam2elemod v1.0) in seven groups, the last with all 19.
  - In the last build, the 15 mods it carried were tested in use, and all
    worked: the 14 non-USB mods but SCENES P2, and USB IO TRACKS MAIN CUE AB.
    The shop's details sheet says, mod by mod, what has been checked
    (`on_unit` in web/catalog.json).
  - Not yet run on an MKI.

## The Model:Cycles and Model:Samples (1.13)

One platform (an MCF54415, ColdFire V4 with EMAC, as the Digitakt), two
profiles: `model_cycles.py` keeps the evidence for each fact, and
`model_samples.py` gives the Model:Samples' own addresses.

- **Files.** One ELE3 container: sections 5 (the build string), 2 (the
  bootstrap), 3 (the main OS, packed as the Digitakt's) and 4 (the updater).
  Sealed as the Digitakt II: an HMAC-SHA256 after the last section, its key
  from the bootstrap's seed string (`REVERB SEND`, `DELAY TIME`). `syx.py`
  rebuilds both stock files byte for byte from their own main OS stream
  (tests/test_releases.py).
- **The version field** is at `0x0C`, not `0x14`: the unit shows the
  header's `1.13` there, and nothing reads `0x14-0x18`. The profile places it
  (`version_at`), and a build that names no version keeps stock's
  (`version_optional`).
- **Staging and flash.** Section 3 is read to `0x40200000` and unpacked to
  `0x40000400`, as on the Digitakt mk1. The container is at flash
  `0x20000` and must end below `0x1E0000`, where the OS keeps two stores:
  1.75 MB, of which stock takes 0.70 MB.
- **Free memory.** DDR `0x47E00000-0x47E40000` (the linker's `ddr`), below
  the boot stack and above every buffer the OSes use; the SRAM tail
  `0x8000F700-0x80010000`, zeroed at reset and named by no instruction. On
  the Model:Cycles, CHORD's two SRAM tables (`sram-chord-a`, `-b`) are free
  only to a mod that serves CHORD its tables from elsewhere.
- **The core** ([mods/core-mc](../mods/core-mc), [mods/core-ms](../mods/core-ms))
  is the shared core.s with ev_tick, ev_draw, ev_key and ev_enc
  ([CORES.md](CORES.md)).
- **On the units.** Builds with the core and mods on it (a hook bus, menus,
  USB audio, sample playback changes) run on both units, sent with Transfer
  over USB as an OS update. Recovery: hold
  FUNC while powering on, TRIG 4 (OS UPGRADE), and the stock `.syx` over
  5-pin MIDI.

## The Analog Rytm MKII (1.73, 1.74)

The profile (`analog_rytm_mk2.py`, where each fact's evidence is) and a
core (mods/core-ar2) built from the Digitakt mk1's sources.
On a unit (11 Oct 2026), a build of core-ar2 with hello-marker-ar2 booted,
reported its own version (HM01) and drew the marker on every screen.
With examples/hooks-check-ar2 (HC01), every event core-ar2 provides ran as
expected: tick, draw, key, encoder, a SETTINGS row, and the render's entry
and exit.
The Rytm runs on the Digitakt mk1's platform, and its main OS reads like the
Digitakt's in the places elekloader depends on.

- **Files.** The Digitakt mk1's family (ELE3, the same SysEx transport),
  device byte `0x0C`, no trailer. Five sections: 5, 2 (the bootstrap, with the
  STARTUP menu), 1 (an FPGA bitstream), 3 (MAIN OS) and 8 (the ARM image the
  Digitakt mk1 and Digitone mk1 carry too, byte for byte). The writer
  reproduces the stock file byte for byte from its own main OS stream.
- **Staging.** The bootstrap's loader (`0x80000590`) is the Digitakt mk1's call
  for call: section 3 from flash at its container offset + `0x80000`, to
  `0x40200000`, unpacked in place to `0x40000400` by the same depacker. Stock's
  in-place gap on 1.74 is 401,995 bytes; elekloader's repack of 1.74,
  436,273 (packer checked 23 Oct 2026).
- **Flash.** `flash_limit` is `0x300000`. The OS keeps the Digitakt's store at
  `0x380000`-`0x400000`, and a second one of its own at `0x300000`-`0x380000`
  (a 0x1B072-byte block at `0x300000` or `0x340000`), read, written and erased
  through the same routines. elekloader's repack of 1.74 ends at `0x1F2BB0`
  (Elektron's at `0x1EA5D0`). The in-OS upgrade erases from `0x80000`, a
  sector at a time, only as far as the container it received reaches.
- **No protected range.** The main OS holds no copy of the bootstrap: no 512
  bytes of section 2's first 0x7D94 are in it (after them come zero-padded
  tables), what they share is pieces of tables and data, and the packed
  section shares nothing. Every call into the OS's SPI NOR routines (program
  0x40136fae; erase 0x4013702a and five others) is from one of: the stores
  at 0x300000 and 0x380000; the in-OS upgrade (0x4009f3a0: erase and write
  from 0x80000, from what it received); the factory console (0x4013a068:
  "READY FOR BOOTSTRAP" writes a received bootstrap at 0x40000, "READY FOR
  OS" a received container from 0x80000); and an erase of 0x400000-0xc00000
  behind a marker. None writes bytes of the main OS image.
  tests/test_rytm_mk2.py checks both.
- **Mods' area.** `0x47BE0000`-`0x47C00000`, the Digitakt mk1's. Checked on a
  unit: filled from the boot site at power-on, no word of it changed through a
  long session of playback, sampling, effects, MIDI and Overbridge, or after a
  power cycle.
- **Recovery.** The STARTUP menu (FUNC at power-on), TRIG 4 for OS UPGRADE, the
  stock file over MIDI DIN. Checked on a unit with stock 1.73.
- **The TypeScript engine** (js/) has the profile too (`ANALOG_RYTM_MK2`). Its
  ColdFire decoder agrees with the Python's at every even offset of the 1.73
  main OS (`tools/isa_parity`), and `tools/parity.ts` takes the Rytm's cases
  (`stocks.ar173`, `dirs.ar`). With core-ar2 0.1, hello-marker-ar2 and
  hooks-check-ar2 built for 1.73, its nine cases (four builds, a check, three
  refusals) give the same files, messages and checks as the Python (11 Oct
  2026). The web page's list of where to download each stock OS does not
  name the Rytm yet.
- **1.74.** Its file differs from 1.73's only in the main OS and section 5:
  the same bootstrap, FPGA bitstream and ARM image, so the same staging,
  flash layout and recovery; the writer reproduces it, and its main OS
  depacks in place. The main OS is 1.73's with code added in two places
  (Outbox 8's main-output routing, the release's one change): from
  0x40057xxx everything sits 0x6c further on, from 0x40123exx 0x1cc, and
  the data after the code further still. The OS entry, the SRAM operands,
  the RAM it clears (0x402CE000-0x42FBB250) and its stack are 1.73's, and
  no operand points into the mods' area. core-ar2 (to 0.2), hello-marker-ar2 and
  hooks-check-ar2 have 1.74 ports (every site and routine is 1.73's, moved;
  STRING_CTOR, in the data-shifted region, +0x2a4); built for 1.73 they
  are byte for byte what they were. From 0.3 core-ar2 is for 1.74 only
  (1.74 its `os`, no ports), and 0.2 is the last core with 1.73. tests/test_rytm_mk2.py passes with
  either file, and the TypeScript engine builds core-ar2 with each example
  for 1.74 byte for byte as the Python does. On a unit (17 Oct 2026), the
  1.74 builds of core-ar2 with hello-marker-ar2 (HM74) and with
  hooks-check-ar2 (HC74) work as the 1.73 ones do; in rytm-emu both boot
  as stock 1.74 does, differing only where the mod draws.

## The Analog Four MKII (1.56)

A proof of concept: the profile (`analog_four_mk2.py`, where each fact's
evidence is) and a core (mods/core-a4) built from core-ar2 0.3's sources.
The A4 runs on the Analog Rytm MKII's platform, but every address, size and
count here was found again in the A4's own image (Ghidra, and rytm-emu's A4
build, 20 Oct 2026), none copied from the Rytm. On a unit, a build of core-a4
with hooks-check-a4 (HC56, 20 Oct 2026) booted and every event ran as
expected, and one with store-check-a4 (SC56, 21 Oct 2026) kept every scope's
fields through saves, loads, power cycles and a Transfer round trip.
Recovery to stock worked on the unit (21 Oct 2026). core-a4 0.2's
parameter pages and views, with pages-check-a4 (PA56, 23 Oct 2026), drew
every screen checked as in the emulator, and a mod's view came back after
a project load and a power cycle.

- **Files.** The Rytm's family (ELE3, the same SysEx transport), device byte
  `0x0B`, version field at `0x14`, no trailer. Five sections: 5, 2 (the
  bootstrap, with the STARTUP menu), 1 (an FPGA bitstream), 3 (MAIN OS) and
  8 (the same ARM image as the Rytm's). The writer reproduces the stock file
  byte for byte from its own main OS stream.
- **Staging.** The bootstrap's loader (`0x80000590`) is the Rytm's call for
  call, with a byte-identical depacker: section 3 from flash at its container
  offset + `0x80000`, to `0x40200000`, unpacked in place to `0x40000400`.
  Stock's in-place gap is 540,192 bytes; elekloader's repack of stock,
  563,703 (packer checked 23 Oct 2026).
- **Flash.** `flash_limit` is `0x300000`, where the OS keeps its voice
  calibration record (`RBOW`, 0xE56 bytes). The settings store is the
  Rytm's, `0x380000`-`0x400000`; there is no store at `0x340000`.
  elekloader's repack of stock ends at `0x18DF70` (Elektron's at
  `0x188390`).
- **No protected range.** Every call into the SPI NOR routines comes from
  the calibration record, the settings store, the factory console, the in-OS
  upgrade or the COKI copy (which erases 0x400000-0xc00000 after copying it
  to the card); none writes bytes of the main OS. The main OS holds no copy
  of the bootstrap: the one thing they share is a 1,175-byte font-width
  table. tests/test_analog_four_mk2.py checks both.
- **The OS entry** is the Rytm's apart from addresses and one constant (it
  ORs 0x06 into 0xFC044000, the Digitakt mk1's value); the boot site is
  `0x40001180`. The OS clears `0x40253000`-`0x448B6D50` at start and runs
  its stack down from `0x48000000`.
- **Mods' area.** `0x47BE0000`-`0x47C00000`, the Rytm's. No code operand
  points into it or its uncached view, and in rytm-emu a watch over it saw
  no access through a cold boot and a session of keys, encoders and PLAY.
  The OS's one allocator (operator new) has a fixed 16 MB arena,
  0x41872230-0x42872230, inside the RAM it clears; every task stack is
  there too, and its fixed buffers outside it (effects, the delay ring,
  DMA) end at 0x456472C0 (cached view). So 0x456472C0-0x47BE0000 (37.6 MB)
  is unused as well. On a unit (21 Oct 2026), ddr-canary-a4 filled the
  mods' area after the mods' data, 0x448B6D50-0x44C00000 and that gap with a
  pattern and read them back with no word changed, through a session of
  normal use (Overbridge included) and after a power cycle.
- **The core** (mods/core-a4) has the Rytm's events at the A4's sites, and
  mod storage in bytes of the A4's own structures (mods/core-a4/README.md);
  from 0.2, core-ar2 0.4's parameter routines, and views added behind the
  page keys.
  In rytm-emu (with a fallback added so it accepts a patched A4 image), a
  build with core alone runs as stock does, with the same frames and a
  byte-identical screen; hello-marker-a4 differs from stock only in its
  square; hooks-check-a4 counted every event and listed its SETTINGS row.
  With store-check-a4, every scope's markers read back after a project
  save, loading another project, loading it back, and a power cycle, and
  CREATE NEW cleared them all.
- **Recovery.** The STARTUP menu (FUNC at power-on), TRIG 4 for OS UPGRADE,
  the stock file over MIDI DIN. Checked on a unit with stock 1.56 after the
  test builds (21 Oct 2026).
- **The TypeScript engine** (js/) has the profile too (`ANALOG_FOUR_MK2`) and
  core-a4's storage spec, and `tools/parity.ts` takes the A4's cases
  (`stocks.a456`, `dirs.a4`: core-a4 with each example, alone and together,
  the examples without core, a check, and each Rytm core on the other
  device). Its eleven cases (six builds, a check, four refusals) give the
  same files, messages and checks as the Python (21 Oct 2026). The web page's
  list of where to download each stock OS does not name the A4 yet.

## The Digitone II (1.12)

Experimental. The profile (`digitone_mk2.py`, where each fact's evidence
is), whole builds, and linkable mods with a core (core-dn2 0.1: tick, draw,
key, encoder, SETTINGS and PERSONALIZE; mods/core-dn2/README.md has its
sites and evidence). The Digitone II
runs on the Digitakt II's platform, but every address, size and count here
was found again in DN2 1.12's own sections (23 Oct 2026), none copied from
the Digitakt II. Stock 1.12 and MIDI recovery have worked on a unit.
The first core builds stalled during USB transfer before flashing; the
smaller core-only CO02 build then flashed and booted successfully (below).

- **Files.** The Digitakt II's family, device byte `0x15`, the same six
  sections (5, 2, 3, 4, 7, 8), main OS 3,196,288 bytes depacked. Sealed as
  the Digitakt II: the key comes from section 2's seed `"Multiplier"` (no
  other string there verifies the trailer). The OS's upgrade validator
  (`0x400dbdb0`) checks the content checksum, then the seal, with its own
  copy of the seed and constant (`0x40210a4a`, `0x4021b9ef`), which give
  the same key. The writer reproduces the stock file byte for byte, seal
  included; the profile also knows Elektron's zip of it.
- **Staging.** The bootstrap is the Digitakt II 1.17's code instruction for
  instruction, at the same addresses: the loader `0x80000596` reads section
  3 from flash at its container offset + `0x80000` to `0x40400000`, and the
  depacker `0x80000432` (byte-identical) unpacks it to `0x40000400`. Run
  in place (in digikit's ColdFire core), that depacker yields the stock
  image from Elektron's stream and from elekloader's repack, and from
  builds grown by 256 KB of code, by 1 MB of random bytes, and past the
  stage. These are isolated depacker probes; images above the USB cap below
  are not eligible for physical USB transfer. On a stream whose output overtakes its unread input it faults,
  the case `syx.verify` refuses. Stock's in-place gap is 2,128,334 bytes.
- **Flash.** `flash_limit` is `0x380000`: the settings store
  `0x380000`-`0x400000` (26 and 12 pushes of `0x380000` and `0x3c0000`). Stock
  takes 1.80 MB of the 3 MB; elekloader's repack of stock ends at
  `0x253ae0` (1,160,952 stored main-OS bytes; packer checked 23 Oct 2026).
  The in-OS upgrade (`0x40139104`) erases from `0x80000` only as
  far as the container it received. The bootstrap and the OS both read the
  container's header at `0x80000` and want product `0x34`, which the
  writer keeps.
- **No protected range.** Every write and erase through the SPI NOR driver
  comes from the settings store, the factory console, the in-OS upgrade or
  a migration (`0x400f2022`) that erases only the `0x400000`, `0x800000` and
  `0xc00000` sectors; none writes bytes of the main OS. The main OS holds no
  copy of the bootstrap: they share only a font-width table and zero runs.
- **At reset** (`0x400004e8`), the OS copies `0x402fd000-0x40305000` to SRAM
  at `0x80000000` and `0x40305000-0x4030c980` to `0x80008000` (zeroing the
  rest of the second half), clears DDR `0x402fd000-0x466b84d0` and runs its
  stack down from `0x48000000`. Its heap (a buddy allocator, 32 MB at
  `0x4464bbf0`) is inside the clear.
- **Mods' area.** `0x47F00000`-`0x47F40000` (256 KB), the Digitakt II's,
  the profile's one area and the linker's `ddr`: a whole build's regions
  must lie in it, and its blob may be at most its size.
  - Above the clear, the OS names only its DMA buffers, all through the
    uncached alias (+0x08000000): `0x466c0000`-`0x46709340` in the cached
    view. In its code, nothing names `0x47E00000`-`0x48000000` or its alias
    but the stack top, stores to `0x48000000` itself (a reset) and `FLT_MAX`.
  - In digikit's emulator (`regions_probe`, ColdFire only), a canary over
    `0x46710000`-`0x48000000`, in both views, filled as the reset clear
    returns: through a cold boot and two panel sessions (every page, mode,
    arrow and encoder; PLAY, the 16 trigs, RECORD and MIDI notes) to 2.25G
    instructions, the canary held everywhere but the stack's top 52 bytes,
    and the final state (the regions cleared) was the same as a run that
    filled them with zeros. The probe compares contents, not accesses: a
    read whose value is unused, or a write later undone, would not show.
    The area is 24 MB above the DMA buffers and 768 KB below the stack top.
  - SRAM is not qualified: the profile has no SRAM area and no `.fast`.
- **Checked in an emulator** (digikit's `boot_gate`, ColdFire only): a
  repack of stock, whole builds with a data site and a blob (up to the
  area's 256 KB) or a region, and one grown to 3.5 KB below the flash
  limit, all boot as stock does, with the same instruction count to the
  main screen and the same screen. These emulator probes do not establish
  that builds above the USB cap can be transferred to a unit.
- **The core** (mods/core-dn2 0.1) has the Digitakt II's six UI events at the
  Digitone II's sites: boot (0x40000538), `ev_tick` (0x4002e464), `ev_draw`
  (0x4002e4ca), `ev_key` (0x4002f20c), `ev_enc` (0x4002f256), `ev_settings`
  (0x4009ff88) and `ev_personalize` (0x400987a2), found by the Digitakt II's
  code and checked in the Digitone II's own (mods/core-dn2/README.md). In
  the emulator, core alone shows the same screens as stock through a panel
  session; examples/hooks-check-dn2 shows all six UI events firing, the row in
  SETTINGS and in PERSONALIZE (once, after reopening), two `ev_key`
  handlers in their order, and a taken key that the stock never sees.
  The core includes the shared physical/synthetic panel bridge, bounded
  encoder turns, a monotonic input clock and the `ev_sysex` observer and
  stock sender export used by the DigiHealth DN2 fork. DTIM0 stays stopped.
- **USB update size (measured, not found in code).** On a unit (23 Oct
  2026), stock 1.12's decoded stream of 1,886,024 bytes transferred and
  flashed with elektroid-cli. The first core build, 2,147,448 decoded
  bytes, stalled before YES with both elektroid-cli and Transfer. The
  last acknowledged SysEx offset (2,660,352 bytes) corresponds to
  2,099,083 decoded bytes, within one USB block of 2 MiB. The profile's
  `update_max=0x200000` conservatively caps the **8-byte preamble plus
  container**, including its seal, at 2 MiB. `syx.verify` refuses larger
  builds even when they fit flash. This is an operational cap inferred
  from the stall, not proof of the exact receive-buffer boundary. The
  bootstrap's MIDI receive limit remains unknown.
- **On the unit** (23 Oct 2026): the core-only CO02,
  sha256 `90cdea1d94cbc39907353bc1f3b70c710b0b0e2ef44423e3b5679fdd12cb1832`,
  transferred over USB with elektroid-cli in 94.5 seconds and flashed.
  The user confirmed reboot, and a fresh device-info readback reported
  `CO02`. Its decoded update is 1,915,960 bytes, below the conservative
  cap. The user then reported a cold boot after 10 seconds off.
  A seeded one-minute MIDI exercise sent 6,974 scenario messages and 145
  cleanup messages without transport or cleanup errors. Fresh device-info
  (`CO02`) and 128-slot project-directory replies were identical before
  and after; the user reported it "Playing and responsive" during the run.
  No incoming MIDI messages were observed, so individual parameter changes
  were not acknowledged or read back. This is a short smoke check, not an
  audio-quality measurement or full event/workload qualification. It
  establishes acceptance of this exact smaller build, not the exact
  receive-buffer boundary. The earlier hello-marker HM02 and hooks-check
  HC02 have not been flashed; the six UI events were subsequently checked
  with the distinct DS03 showcase below. The ten-minute project/audio
  workload remains pending.
- **DigiHealth and the core on the unit** (23 Oct 2026): DS03 (core +
  DigiHealth 1.3 + hooks-check) flashed and rebooted after physical YES.
  Fresh version/helper replies and all 7,204 immutable runtime bytes
  matched the exact linked build. The six UI events, encoder delivery,
  reopened SETTINGS/PERSONALIZE callbacks, subscriber ordering and
  consumed/pass-through TEMPO passed, with saved screens reviewed. Three
  one-second input-clock comparisons differed from host time by at most
  0.3%. The hooks-check bars deliberately erase parts of every screen;
  framebuffer/counter comparison confirmed that they explain the reported
  tearing appearance. Omit hooks-check for normal use.
  The subsequent DH03 (core + DigiHealth only) installed using remote YES
  after inspecting the update dialog. Its version and all 6,620 immutable
  runtime bytes matched. Clean screens, normal TEMPO, encoder input,
  bounded hold expiry and SYSTEM INFO on/off were verified. The
  [DigiHealth fork branch](https://github.com/simonjohansson/digihealth/tree/codex/dn2-remote-control)
  has the helper; no CPU/DSP-load or sample-memory measurement, FAST AUDIO,
  SRAM or SHARC changes are included. This is functional control evidence,
  not longer project/audio qualification.
- **Recovery.** The STARTUP menu (FUNC at power-on), TRIG 4 for OS UPGRADE,
  the stock file over MIDI DIN (the 1.12 manual, 14.4). Proven on the unit
  with stock 1.12 (23 Oct 2026); about 15 minutes, followed by a normal
  boot showing 1.12.
- **The TypeScript engine** (js/) has the profile too (`DIGITONE_MK2`). A
  DN2-only cases document for `tools/parity.ts` gives the same files,
  messages and checks as the Python (23 Oct 2026), in 27 cases:
  - whole builds from the `.syx` and the zip, and a check;
  - core alone, with hello-marker, with hooks-check, with both, and a check;
  - refusals: no version, a blob over the area, regions in SRAM and past
    the area, a Digitakt II mod, a format-2 mod, hello-marker or
    hooks-check without core, two cores, a site on core's draw site,
    `.fast` code, an event core-dn2 lacks, two mods with one row name,
    wrong stock bytes and the measured USB update cap;
  - core with DigiHealth, and with DigiHealth plus hooks-check.

## What a new device needs besides its profile

- **Sealed files.** `trailer='hmac'` with `hmac_key_from` (the Digitakt
  II and the Digitone II, above). Another sealed device needs its seed found
  in its bootstrap, and the writer checked to
  reproduce its stock file, seal included.
- **The packing format.** The loader repacks with the same codec
  (`codec/`). Check that the device's own depacker accepts it: every
  section must round-trip through `codec/elz.py`, and the in-place
  simulation must hold for the device's `stage`.
- **Another CPU.** A decoder in `isa/` with `reader()`, `decode()` and the
  flags `ILLEGAL`, `LINEF`, `FPU`, `PCREL` and the flow constants.
- **Tests.** Before anything is flashed:
  - the writer must reproduce a stock file from its own main OS stream
    (`test_writer_no_change_is_stock`);
  - a verified build must pass the device's cold boot in an emulator, if
    one exists.

Mods are made per release: a mod's target names one stock file by hash, so
supporting a new OS version means adding its release to the profile and
rebuilding the mods for it (below).

## A new OS version of a supported device

How Digitakt mk1 1.54 and Digitone mk1 1.44 were added (2026-10-01):

1. **Compare the file with the last release's.** Every section but the main
   OS (and section 5, the version string) must be the same bytes: the same
   bootstrap and updater, so the same staging and flash layout. For both
   releases that held. If the bootstrap changes, check `stage`,
   `flash_limit` and the recovery path again.
2. **Add the release** to the profile: the file's sha256, and the main OS's
   sha256 and length, depacked. The writer must reproduce the file from its
   own main OS stream, and its main OS must depack in place from `stage`
   (`tests/test_releases.py` checks both for every release given).
3. **Check the memory facts** the profile states: here, the same SRAM
   operands in the code, and no operand near the DDR area mods run from.
   Both releases moved their RAM past the image by 0x1000 and changed
   nothing else there.
4. **Port the cores**, under `ports` in their `mod.json` (ADAPTING.md 3.10):
   every `defsym` and site found again in the new image, and each site's
   stock bytes read from it. `tests/test_releases.py` builds and lints every
   core for every release it names.
5. **Run a build with the core in the emulator** against the new stock
   file. Digitakt mk1 1.54: core 2.1 passes, with every screen identical
   and the audio identical apart from the recording's silent end, 1 ms
   off, exactly as core 2.1 does on 1.53. Digitone mk1 1.44: not yet,
   since the emulator does not boot stock 1.44 to a settled screen.
6. **Tell mod authors** to build their mods for the new OS. A mod for the
   old one is refused with the new stock file.
