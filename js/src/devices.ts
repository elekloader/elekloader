// SPDX-License-Identifier: GPL-3.0-or-later
// Device profiles: everything elekloader knows about one Elektron product. Ported from elekloader's devices/
// (GPL-2.0-or-later), whose files keep the evidence for each fact; the values here are theirs, unchanged.
import { str } from './py.ts'

export class UnknownFirmware extends Error {}

export type SharcProfile = {
  section: number
  processor: string
  isa: string
  siliconRevision: string | null
  storedSha256: string
  storedLen: number
  decodedSha256: string
  decodedLen: number
  storedMax: number
  decodedMax: number
  recordCount: number
  l1Aliases: [number, number, number][]          // core-byte start/end, loader-byte start; not ownership
  l2Bytes: Range
  l2Pc16: [number, number, number]               // PC16 start/end, loader-byte start
  ddrBytes: Range
  protected: [number, number, string][]         // loader-byte exclusions
  ownedRanges: Range[]                         // qualified loader-byte intervals only
  patchRanges?: Range[]                        // exact stock instruction intervals
  raw?: boolean                                 // takes raw payloads (`sharc_raw`: sharc.rawPayload), the Digitakt II
  activeKeyedCore?: { segments: [number, string, string][], zero: [number, number][], entries: Record<string, number>, stackBytes: number }
  hook?: [number, string, string]               // loader address, stock bytes, replacement bytes
  abi?: string
  pitchRange?: Range
  l2Abi?: string
  l2AppRange?: Range
  kernelAbi?: string                            // machine kernels (sharc.placeKernels): the dispatcher and every mod's kernels
  pool?: Range
  kernelStateBytes?: number
  // The tables whose entries name a machine by id: [table, kernel kind, needs its kernel].
  kernelRegistrations?: [string, 'sdk' | 'mk2-voice' | 'mk2-filter', boolean][]
  filterGAt?: number                            // mk2_filter's g (mk2/machines.h)
  voiceStack?: Range                            // the kernels' own stack, in L1
  voiceHook?: [number, string]
  filterStack?: Range
  stackGuard?: number                           // at each machine stack's base; the dispatcher checks it
  peekRange?: Range                             // core-dn2's DSP peek (mods/core-dn2/peek.s), with any SHARC mod
  peekCode?: string
  peekHook?: [number, string, string]
  dataSites?: Record<number, [string, string]>   // optional companion sites: exact stock/replacement bytes
  filterHook?: [number, string]
  filterCapture?: [number, string]              // the filter setup table's internal type 0 entry, sent to the dispatcher
  coreRange?: Range
  stackMax?: number
  codeGuardBytes?: number
  keyedPitchTable?: [number, string]
}

export type Release = {
  version: string        // the OS version, as Elektron names it
  syxSha256: string      // the stock .syx file
  mainSha256: string     // its main OS section, depacked
  mainLen: number        // ... and that section's length
  binSha256: string      // the stock card file (.bin), where the device has one
  sharc?: SharcProfile   // absent unless this release has measured section-7 facts
  dataRelease?: number   // the OS release word of the data container's header (absent: not known)
  prelink?: [number, number, number, string][]   // main OS code (kind 1) and data (2) the OS is done with before the
                         // +Drive boot link runs; a build descriptor lists them (desc.ts, ABI 1.2)
}

export type Range = [number, number]

export type Device = {
  key: string
  name: string
  sysexId: number
  releases: Release[]                           // in the profile's order
  mainSection: number
  mainLoad: number
  stage: number | null                          // null: unknown, so the in-place unpack is not simulated
  flashAt: number
  flashLimit: number
  updateMax: number | null                      // decoded update bytes: 8-byte preamble + container
  noLink: [number, number, string][]            // firmware routines no mod may link to (ordinary calls only)
  trailer: null | 'hmac'
  isa: 'coldfire'
  hmacKeyFrom: [number, string] | null          // for 'hmac': the section and seed the key is derived from
  container: 'ele3' | 'ele2' | 'elek'
  versionLen: number
  versionAt: number                             // 'ele3', 'ele2': where the version field is in the container header
  versionOptional: boolean                      // true: a build given no version keeps stock's field
  protected: [number, number, string][]        // main OS bytes no mod may change
  relocatable: [number, number, number[], string][]
  keep: [number, number, string][]             // main OS bytes that look free but the OS reads: no site may write them
  blobMax: number | null
  areas: Record<string, Range>                  // in the profile's order
  ddr: Range
  coreDdr: [string, Range][]                   // [core version, area]: from that core on, the linker's area in place of ddr
  slotClaims: [string, number, number, string][]   // [table, at, count, prefix]: a contribution to table points at a descriptor whose long at +at is a sound slot; the mod claims prefix:<slot>
  paramIds: [number, [number, number, string], [string, boolean][]] | null   // [own, [lo, hi, what], [[table, ownOnly]]]: knob id ranges (link.Mod2.idRanges)
  machineSlots: [string, [string, Record<number, number>][]] | null   // [table, [[core version, {slot: knobs}]]]: the slots a mod's "machine_knobs" are given from that core on (link.machineSlotsFor)
  pageIds: [number, [string, number][]] | null  // [first, [[table, at]]]: a mod's "pages" are ids from first up, past those a contribution to table names at +at
  driveKnobs: [number, number] | null   // [lo, hi]: with paramIds, the +Drive mods' knob ids; flashed runs stay below lo
  tableLimits: Record<string, number>           // table -> the most entries all mods together may add to it: the OS uses no more (link.check)
  driveAlign: number                            // a build descriptor's +Drive mods' area: from the flashed build's RAM end rounded up to this, to the top of the core's linker area (ddrFor); 0: none
  driveEarly: [number, number, string][]        // what the OS uses before a +Drive loader links and not again before it matters: a site there is refused (23, early)
  driveShadow: boolean                          // the build descriptor carries the stock bytes the flashed build writes (desc.ts, shadow): its +Drive loader copies stock parts from the running OS
  dataFamily: number                            // the family id of the data container's header (0: not known)
  dataDevice: number                            // ... and its device id (0: not known)
  sramCode: Range
  fastTable: string
  imageFree: Range[]
  reserve: [number, Record<string, [number, number]>] | null   // [unit, {name: [a, b]}]: a RAM reserve sized to the mods
  dspPayloads: Record<string, Range>            // tag -> [address, length]: DSP code the main OS uploads at boot
  dspAreas: [string, number, number, string, string][]   // [tag, lo, hi, name, what]: P words a mod frees by claiming name
  recovery: string
}

const rel = (version: string, syxSha256: string, mainSha256: string, mainLen: number, binSha256 = ''): Release =>
  ({ version, syxSha256, mainSha256, mainLen, binSha256 })

const base = {
  mainSection: 3, mainLoad: 0x40000400, isa: 'coldfire' as const, hmacKeyFrom: null, container: 'ele3' as const,
  versionLen: 4, versionAt: 0x14, versionOptional: false, protected: [], relocatable: [], keep: [], blobMax: null, driveAlign: 0, driveShadow: false, driveEarly: [] as [number, number, string][], dataFamily: 0, dataDevice: 0, sramCode: [0, 0] as Range, coreDdr: [] as [string, Range][], slotClaims: [] as [string, number, number, string][], paramIds: null, machineSlots: null, pageIds: null, driveKnobs: null, tableLimits: {}, fastTable: '',
  imageFree: [], reserve: null, dspPayloads: {}, dspAreas: [], trailer: null, updateMax: null, noLink: [] as [number, number, string][],
}

export const DIGITAKT_MK1: Device = {
  ...base,
  key: 'digitakt-mk1', name: 'Digitakt mk1', sysexId: 0x0a,
  releases: [
    rel('1.53', '9bdd44bb6102fb25c143cfab97bc92b7a89c463f795d3112dce89771e29bcc92',
      '4b47a9507758ca5669ca02ab2c0374d2c04c98aece445408295cc1dcb265c5df', 2475584),
    rel('1.54', 'f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6',
      '5c58bf9e3949ef09977c5fc007a61e8d026931f67f1621238379dfb8ee4d31a2', 2479680),
  ],
  stage: 0x40200000, flashAt: 0x80000, flashLimit: 0x380000,
  areas: { ddr: [0x47be0000, 0x47c00000], 'sram-tail': [0x8000f700, 0x80010000], 'sram-block': [0x80003360, 0x80008000] },
  ddr: [0x47be0000, 0x47c00000], sramCode: [0x8000f700, 0x80010000], fastTable: 'fa_copies',
  coreDdr: [['4.0', [0x47bc0000, 0x47c00000]]],   // core 4.0: 256 KB
  slotClaims: [['core_machines', 0, 128, 'machine']],   // a machine's descriptor names its id at +0
  tableLimits: { sdk_alpha_machines: 32 },   // machine-pages' SDK machines
  recovery: 'hold FUNC while powering on for the startup menu, then send the stock .syx',
}

export const DIGITAKT_MK2: Device = {
  ...base,
  key: 'digitakt-mk2', name: 'Digitakt II', sysexId: 0x14,
  releases: [
    { ...rel('1.17', '26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5',
      'a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964', 3275616),
      // Section 7 for raw SHARC mods (`sharc_raw`, docs/FORMAT.md): new loads in the L2 the stock stream leaves
      // unused, and patches of whole stock instructions in the L1 code load. Measured on 1.17.
      sharc: {
        section: 7, processor: 'ADSP-21569', isa: 'sharc+-visa', siliconRevision: null,
        storedSha256: '39113a8ae7f7ac7336bda4a56eeb45108f0618ea451bcf9f466803017184375d', storedLen: 184856,
        decodedSha256: '0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2', decodedLen: 321016,
        storedMax: 0x100000, decodedMax: 0xfffff, recordCount: 104,
        l1Aliases: [[0x240000, 0x270000, 0x28240000], [0x2c0000, 0x2f0000, 0x282c0000],
          [0x300000, 0x320000, 0x28300000], [0x380000, 0x3a0000, 0x28380000]],
        l2Bytes: [0x20000000, 0x20100000], l2Pc16: [0xb80000, 0xc00000, 0x20000000],
        ddrBytes: [0x80000000, 0xa0000000],
        protected: [[0x200fa000, 0x200fe000, 'DT2 L2 window two functions load as literals'],
          [0x200fe000, 0x20100000, 'Boot ROM workspace'], [0x282ec000, 0x282f0000, 'L1 block 1 cache'],
          [0x2831c000, 0x28320000, 'L1 block 2 cache'], [0x2839c000, 0x283a0000, 'L1 block 3 cache']],
        raw: true,
        ownedRanges: [[0x2001ab7c, 0x200fa000]],
        patchRanges: [[0x283827cc, 0x283827cc + 104500]],
      } },
  ],
  stage: 0x40400000, flashAt: 0x80000, flashLimit: 0x380000, trailer: 'hmac', hmacKeyFrom: [2, 'Master Overdrive'],
  // ddr-low: below the linker's area, above the DSP buffers; what mods that place code at fixed addresses claim
  areas: { ddr: [0x47f00000, 0x47f40000], 'ddr-low': [0x47e7b000, 0x47f00000], 'sram-tail': [0x8000f100, 0x80010000], 'sram-block': [0x80006e80, 0x80008000] },
  ddr: [0x47f00000, 0x47f40000], sramCode: [0x8000f100, 0x80010000], fastTable: 'core_fast',
  driveAlign: 0x1000,                              // +Drive mods' area: what the flashed build leaves of ddr's, from a 4 KB boundary
  driveShadow: true,                               // TELOS copies stock parts from RAM (desc.ts, shadow)
  recovery: 'hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over MIDI (not USB)',
}

export const DIGITONE_MK1: Device = {
  ...base,
  key: 'digitone-mk1', name: 'Digitone mk1', sysexId: 0x0d,
  releases: [
    rel('1.43', 'c5a54cc05b921f2e4bd814834c5365c2a5aa01d7772a9a2961fac1c3095bf9aa',
      '3831a477a2a22befb23c42e47e782853da49566ef5d0a1767fcf6c30e6767414', 2732208),
    rel('1.44', 'd4f200d04484333d82822db7744e6484d0def8f2db8ddf55ee2b780cc13c9659',
      'fce648a97c6c5d93b961732e8f8db6b02e0820c6d344c7e2131fa05a4b3168e4', 2736304),
  ],
  stage: 0x40200000, flashAt: 0x80000, flashLimit: 0x380000,
  areas: { ddr: [0x47be0000, 0x47c00000], bulk: [0x44000000, 0x47be0000] },
  ddr: [0x47be0000, 0x47c00000],
  coreDdr: [['4.0', [0x43ec0000, 0x44000000]]],   // core-dn1 4.0: 1.25 MB
  driveAlign: 0x1000,                              // +Drive mods' area: what the flashed build leaves of coreDdr's, from a 4 KB boundary
  driveShadow: true,                               // TELOS copies stock parts from RAM (desc.ts, shadow)
  slotClaims: [['core_params', 8, 79, 'soundslot'],   // a parameter slot's descriptor names its sound slot at +8
    ['sdk_alpha_machines', 0, 128, 'dnmachine']],   // an SDK machine names its id at +0
  tableLimits: { sdk_alpha_machines: 15 },   // MACHINES lists 15 machines
  recovery: 'hold FUNC while powering on for the startup menu, press TRIG 4 (OS UPGRADE), then send the stock .syx',
}

export const OCTATRACK: Device = {
  ...base,
  key: 'octatrack', name: 'Octatrack', sysexId: 0x05,
  releases: [
    rel('1.40C', '0a8d2d2b35c2cc78fa338576adc1af0cbc094cef973255103165c73edd87f2b6',
      '164f31224bf61181e3f50e7dec40df9afcae5b16dbf6e4c0d0cc5e986af0a84e', 1112560,
      '34695b606eb00e1b4dded5fd0c4b66f3a460522a632e47d7416dbd220599e1ad'),
  ],
  stage: null, flashAt: 0x4000, flashLimit: 0x200000, container: 'elek', versionLen: 10,
  protected: [[0x400de1e0, 0x400e21e0, 'the copy of the bootloader the OS can re-flash it from']],
  relocatable: [[0x400e2000, 18, [0x4001d82e], 'the USB device descriptor']],
  keep: [[0x400d24d0, 0x400d24e0, "the stock DELAY reads them past the end of its coefficient table, so a write "
    + "there changes the DELAY's output (octabam #771)"]],
  driveEarly: [[0x4001dfbc, 0x4001e042, 'the USB init, run once before the card mounts'],
               [0x400e2000, 0x400e21e0, 'the USB descriptor tables, read at the USB init']],
  areas: { ddr: [0x40a955e0, 0x41495de0] },
  ddr: [0x40a955e0, 0x41495de0],
  imageFree: [[0x400c45b0, 0x400c4702], [0x400d24e0, 0x400d2ce0], [0x400d64e0, 0x400d7c3c]],
  reserve: [6144, { arena_base: [0x40a955e0, 6144], __arena_pages: [14602, -1], __arena_fill: [14603, -1],
    __arena_clear: [0x05590800, -6144] }],
  dspPayloads: { A: [0x400e2324, 0x136cb], B: [0x400f59ef, 0x12d05] },
  dspAreas: [['A', 0xaa8, 0xbad, 'dsp:harvest:SPATIALIZER', "SPATIALIZER's code on payload A (core 0)"],
    ['B', 0x868, 0x96d, 'dsp:harvest:SPATIALIZER-B', "SPATIALIZER's code on payload B (core 1): dspload-ot's copier"]],
  recovery: 'hold FUNC while powering on for the startup menu, press TRIG 3 (MIDI UPGRADE) and send the stock '
    + '.syx over 5-pin MIDI (not USB)',
}

export const MODEL_CYCLES: Device = {
  ...base,
  key: 'model-cycles', name: 'Model:Cycles', sysexId: 0x11,
  releases: [
    rel('1.13', '44fe586269631a0ca7da25a3383fc6733c314809505fc3cc52f1e0ed9800640c',
      'cc99d4f0175d34d1e91d046e6ec85a5e8ab58ab9edbb3c24406acd48cb99ee98', 1744192),
  ],
  stage: 0x40200000, flashAt: 0x20000, flashLimit: 0x1e0000, trailer: 'hmac', hmacKeyFrom: [2, 'REVERB SEND'],
  versionAt: 0x0c, versionOptional: true,
  areas: { ddr: [0x47e00000, 0x47e40000], 'sram-tail': [0x8000f700, 0x80010000],
    'sram-chord-a': [0x80001c5c, 0x800074b4], 'sram-chord-b': [0x8000cac0, 0x8000eae0],
    bulk: [0x42800000, 0x47e00000] },
  ddr: [0x47e00000, 0x47e40000],
  recovery: 'hold FUNC while powering on for the STARTUP MENU, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over 5-pin MIDI (not USB)',
}

export const MODEL_SAMPLES: Device = {
  ...MODEL_CYCLES,
  key: 'model-samples', name: 'Model:Samples', sysexId: 0x0f,
  releases: [
    rel('1.13', 'e11859b68deb7e5e3fe86ab32581212093849c4be5d3950add011eac398a2ce8',
      'a351392c62ec1c6c3324a807baf46934690d54edfc76029a4b4882541cad1ab2', 1733184),
  ],
  hmacKeyFrom: [2, 'DELAY TIME'],
  areas: { ddr: [0x47e00000, 0x47e40000], 'sram-tail': [0x8000f700, 0x80010000] },
}

// the 138 static constructors Ghidra has no function for (the table at 0x402cd650: a count, then 146
// entries, only ever called from main() at 0x40100712): each body walked from its entry to its rts, not
// into what it calls (Ghidra, 2026-11-10). The 6-byte jmp thunks between some of them are not included.
const AR2_174_CTORS: [number, number][] = [
  [0x4017e2fa, 0x4017e318], [0x4017eec2, 0x4017f00e], [0x4017f99e, 0x4017f9bc], [0x4017fb70, 0x4017fb8e],
  [0x401808aa, 0x401808c8], [0x40180ea4, 0x40180f9c], [0x40181132, 0x40181150], [0x40181344, 0x40181376],
  [0x401815be, 0x401815dc], [0x4018182c, 0x40181872], [0x40181de4, 0x40181e3e], [0x40182fa2, 0x40183010],
  [0x401834d0, 0x4018353e], [0x40183696, 0x401836f0], [0x4018387e, 0x4018389c], [0x40183a68, 0x40183a86],
  [0x40183f70, 0x40183fa2], [0x40184166, 0x40184184], [0x401842fe, 0x40184330], [0x40184560, 0x4018457e],
  [0x40184680, 0x4018469e], [0x401847a0, 0x401847be], [0x401848ce, 0x40184900], [0x40184bc2, 0x40184bf4],
  [0x40184d18, 0x40184d72], [0x40184fb2, 0x40184fe4], [0x40185172, 0x40185190], [0x40185398, 0x401853de],
  [0x40185b0c, 0x40185b8e], [0x4018612a, 0x40186170], [0x401862d8, 0x4018631e], [0x401863fc, 0x40186442],
  [0x4018652c, 0x4018654a], [0x40186656, 0x4018669c], [0x401868f8, 0x40186916], [0x40186a6a, 0x40186ab0],
  [0x40187724, 0x401877a6], [0x401879bc, 0x401879da], [0x401879e0, 0x40187a3a], [0x40187afa, 0x40187b18],
  [0x40187d64, 0x40187d82], [0x40188170, 0x4018818e], [0x40188256, 0x40188288], [0x40188366, 0x40188398],
  [0x4018849a, 0x401884b8], [0x40188578, 0x40188596], [0x40188698, 0x401886b6], [0x40188c3a, 0x40188c58],
  [0x40188cee, 0x40188d0c], [0x40188d12, 0x401890d0], [0x40189646, 0x401897e0], [0x4018a438, 0x4018a456],
  [0x4018ad80, 0x4018ad9e], [0x4018ae80, 0x4018ae8e], [0x4018b1b8, 0x4018b1d6], [0x4018e86e, 0x4018e9d4],
  [0x4019092c, 0x4019095e], [0x40191bd2, 0x40191bf0], [0x40192006, 0x40192024], [0x401923f4, 0x4019244e],
  [0x401924f6, 0x401925ae], [0x401927ba, 0x4019288a], [0x40192bf0, 0x40192cfc], [0x401931a2, 0x401931c0],
  [0x401935a6, 0x401935c4], [0x40193e90, 0x40193ea4], [0x401944b0, 0x401944ce], [0x40194e3c, 0x40194f22],
  [0x4019571a, 0x40195902], [0x4019638e, 0x401963c0], [0x40196b06, 0x40196b38], [0x401975bc, 0x40197602],
  [0x40197d50, 0x40197dd2], [0x40198d88, 0x40198de2], [0x401991be, 0x401991f0], [0x40199acc, 0x40199afe],
  [0x4019bb4c, 0x4019bc46], [0x4019ca20, 0x4019ca7a], [0x4019ca80, 0x4019ca9e], [0x4019d2bc, 0x4019d2ee],
  [0x4019dc14, 0x4019dc82], [0x4019eb46, 0x4019effa], [0x4019f3d4, 0x4019f406], [0x401a0856, 0x401a089c],
  [0x401a0c34, 0x401a0ca2], [0x401a0d62, 0x401a0da8], [0x401a0e90, 0x401a0ed6], [0x401a1102, 0x401a115c],
  [0x401a332a, 0x401a33fc], [0x401a370c, 0x401a378e], [0x401a39b6, 0x401a3a38], [0x401a3c38, 0x401a3ca6],
  [0x401a3da2, 0x401a3e10], [0x401a3f2c, 0x401a3f9a], [0x401a406e, 0x401a40b4], [0x401a4314, 0x401a435a],
  [0x401a4360, 0x401a43a6], [0x401a4516, 0x401a455c], [0x401a4630, 0x401a468a], [0x401a496c, 0x401a498a],
  [0x401a4aee, 0x401a4b0c], [0x401a4eda, 0x401a4f20], [0x401a50a0, 0x401a50d2], [0x401a5310, 0x401a5356],
  [0x401a5458, 0x401a549e], [0x401a55c4, 0x401a560a], [0x401a570c, 0x401a572a], [0x401a57f2, 0x401a5824],
  [0x401a5972, 0x401a5990], [0x401a5b62, 0x401a5ba8], [0x401a5c86, 0x401a5cb8], [0x401a5cbe, 0x401a5cdc],
  [0x401a605c, 0x401a608e], [0x401a6672, 0x401a66cc], [0x401a67ce, 0x401a67ec], [0x401a6940, 0x401a69ae],
  [0x401a6bf2, 0x401a6c24], [0x401a6d78, 0x401a6dbe], [0x401a6ff0, 0x401a7036], [0x401a7236, 0x401a7268],
  [0x401b1844, 0x401b1876], [0x401b1944, 0x401b1a02], [0x401b1e36, 0x401b1e90], [0x401b1f64, 0x401b1fbe],
  [0x401b2762, 0x401b27a8], [0x401b6142, 0x401b6150], [0x401b654c, 0x401b657e], [0x401b68aa, 0x401b68dc],
  [0x401b69de, 0x401b6a10], [0x401b6bae, 0x401b6be0], [0x401b6ee8, 0x401b6f2e], [0x401b7754, 0x401b7786],
  [0x401b785a, 0x401b78a0], [0x401b7942, 0x401b7960], [0x401b7afa, 0x401b7b40], [0x401b7c3a, 0x401b7c80],
  [0x401b7f50, 0x401b7f6e], [0x401b808e, 0x401b809c],
]

export const ANALOG_RYTM_MK2: Device = {
  ...base,
  key: 'analog-rytm-mk2', name: 'Analog Rytm MKII', sysexId: 0x0c,
  releases: [
    rel('1.73', '8ad671087ec30433d5407c7380d2396e0915ab931c5d3dd18a78bf284d3d1e52',
      '28d9ef40c895e03f17dfce096543109adaf708947392db72818fc2a412d685d6', 3005432),
    { ...rel('1.74', 'cb0f99d620edde994fb860c0e79cc2464e427d06882828cfdfb00fc53700e123',
      '34cd0c4da8e4b810d1b25284475a99ea53821e2ab7d30a978bf15dc578251f3b', 3005432),
      dataRelease: 0x30313736,                     // a stock 1.74 project download's header
      prelink: [                                   // boot-order trace (Ghidra, 2026-11-10): run only before 0x401009e0
        [0x40000690, 0x4000074a, 1, 'reset helpers'],
        [0x4000074a, 0x40000762, 1, 'FUN_4000074a, FUN_40000756'],
        [0x400007e4, 0x40000870, 1, 'data copy, OS .bss clear'],
        [0x40000870, 0x400008fc, 1, 'reset entry'],
        [0x40000946, 0x400009bc, 1, 'FUN_40000946'],
        [0x40000c34, 0x40000c7e, 1, 'FUN_40000c34'],
        [0x40000dac, 0x40000de4, 1, 'scheduler first start'],
        [0x400014a2, 0x400014d8, 1, 'FUN_400014a2, VBR_FN, reset helper'],
        [0x40001c16, 0x40001dea, 1, 'panel UART/DMA setup'],
        [0x4000201c, 0x400021f0, 1, 'FUN_4000201c'],
        [0x400023c8, 0x40002454, 1, 'FUN_400023c8'],
        [0x400025f6, 0x4000261a, 1, 'FUN_400025f6'],
        [0x40003bb2, 0x40003bcc, 1, 'FUN_40003bb2'],
        [0x40003be6, 0x40003bfa, 1, 'FUN_40003be6'],
        [0x40006cd8, 0x40006e8a, 1, 'parameter lookup table build'],
        [0x4007a4d6, 0x4007a4e4, 1, 'FUN_4007a4d6'],
        [0x4007f356, 0x4007f36c, 1, 'ekFS pre-mount'],
        [0x4008119c, 0x40081238, 1, 'FUN_4008119c'],
        [0x40081c10, 0x40081c28, 1, 'FUN_40081c10'],
        [0x40081da0, 0x40081dac, 1, 'FUN_40081da0'],
        [0x4008253a, 0x400825da, 1, 'key-code map'],
        [0x40091998, 0x40091f88, 1, 'eMMC bring-up'],
        [0x40091ff4, 0x40092048, 1, 'FUN_40091ff4'],
        [0x40095e9e, 0x40095eb0, 1, 'FUN_40095e9e'],
        [0x400963ec, 0x40096448, 1, 'FUN_400963ec'],
        [0x400a3448, 0x400a3454, 1, 'FUN_400a3448'],
        [0x400fa456, 0x400fa46e, 1, 'state header'],
        [0x400fa5c6, 0x400fa640, 1, 'state header'],
        [0x400fb268, 0x400fb394, 1, 'boot project load'],
        [0x401006ee, 0x401009e0, 1, 'main() up to the hook'],
        [0x40100a94, 0x40100ac2, 1, 'start task prologue'],
        [0x40100b12, 0x40100b40, 1, 'creates the start task'],
        [0x40101efa, 0x40101fa2, 1, 'FUN_40101efa'],
        [0x40122960, 0x40122bf8, 1, 'SPI migration, working state read'],
        [0x40139b80, 0x40139cde, 1, 'FUN_40139b80, FUN_40139bc0, FUN_40139c00'],
        [0x4013fd52, 0x4013fdae, 1, 'FUN_4013fd52'],
        [0x4013fefe, 0x40140260, 1, 'FUN_4013fefe, FUN_40140068'],
        [0x4017014a, 0x401701c8, 1, 'RTOS init'],
        [0x4017c2cc, 0x4017c384, 1, 'constructor'],
        [0x4017d998, 0x4017d9b6, 1, 'constructor'],
        [0x40181622, 0x4018165e, 1, 'constructor'],
        [0x401893c2, 0x4018954c, 1, 'constructor'],
        [0x4018a290, 0x4018a2c8, 1, 'constructor'],
        [0x4018dfd2, 0x4018e074, 1, 'constructor'],
        [0x401a7622, 0x401b139a, 1, 'constructor FUN_401a7622, param_info copy'],
        [0x401bd2c6, 0x401bd2e6, 1, 'constructor'],
        [0x401bed50, 0x401bed5c, 2, 'old SPI-flash state addresses'],
        [0x402cd650, 0x402cd89c, 2, 'constructor table'],
        ...AR2_174_CTORS.map(([lo, hi]): [number, number, number, string] => [lo, hi, 1, 'constructor']),
      ].sort((a, b) => a[0] - b[0]) },
  ],
  stage: 0x40200000, flashAt: 0x80000, flashLimit: 0x300000,
  areas: { ddr: [0x47be0000, 0x47c00000], big: [0x47938000, 0x47fff000] },
  ddr: [0x47be0000, 0x47c00000],
  coreDdr: [['0.11', [0x47b10000, 0x47fff000]]],   // core-ar2 0.11: 5 MB, above its tags and mirrors
  driveAlign: 0x1000,                              // the +Drive mods' area: what the flashed build leaves of coreDdr's, from a 4 KB boundary
  dataFamily: 2,                                   // the data container's family id (stock checks it)
  dataDevice: 5,                                   // ... and its device id (a stock project download's)
  recovery: 'hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over MIDI DIN (not USB)',
  slotClaims: [['core_machines', 0, 53, 'machine']],   // a machine's descriptor names its slot at +0
  // core-ar2's knob ids: stock's records are 0..468, core's lockable parameters take 469..554
  // (469 + code - 42, lock.c) and a mod's own ids start at 555 (pages.h CORE_PARAM_LOCK_FIRST)
  paramIds: [555, [469, 554, "core's lockable parameters' ids"], [['core_params', false], ['core_param_locks', true], ['core_knob_looks', true], ['core_knob_faces', false]]],
  driveKnobs: [0x4000, 0x7fff],
  // the hidden slots a mod's "machine_knobs" are given (core-ar2 0.5: 34..43, 0.13: 34..52), with how many knobs from LEV
  // (sound index 9) on have a record there (stock 1.74's records)
  machineSlots: ['core_machines', [
    ['0.5', { 34: 3, 35: 8, 36: 8, 37: 8, 38: 8, 39: 8, 40: 8, 41: 8, 42: 8, 43: 5 }],
    ['0.13', { 34: 3, 35: 8, 36: 8, 37: 8, 38: 8, 39: 8, 40: 8, 41: 8, 42: 8, 43: 5,
      44: 8, 45: 4, 46: 8, 47: 5, 48: 4, 49: 4, 50: 5, 51: 8, 52: 0 }]]],
  // a mod's "pages": ids from 12 up (stock's are 0..11), past the ids entries of these tables name
  pageIds: [12, [['core_pages', 0], ['core_page_fns', 0], ['core_view_pages', 4]]],
}

export const ANALOG_RYTM_MK1: Device = {
  ...base,
  key: 'analog-rytm-mk1', name: 'Analog Rytm MKI', sysexId: 0x07, container: 'ele2', versionAt: 0x10,
  releases: [
    rel('1.74', '4f0d59051b4b3ac01f336639918ceb8186e0ae1a1abf2ed9bc028b8718514cff',
      '6d021e22e8912cfc1ee84e619c35db0174455cbc42bf64dbec1f1d06a5ab811d', 2824228),
  ],
  stage: 0x40200000, flashAt: 0x20000, flashLimit: 0x780000,   // the calibration records start at 0x780000
  protected: [
    [0x40000592, 0x400005d8, "the OS entry's bootstrap version compare and the call to BOOTSTRAP UPGRADE"],
    [0x4028c768, 0x402a1b84, 'the copy of the bootstrap the OS writes into flash, with its version words'],
    // the routines that can write the boot flash, kept stock as well as unlinkable
    [0x4011a7a4, 0x4011aa7a, 'BOOTSTRAP UPGRADE, which rewrites the boot flash'],
    [0x40096a6c, 0x40096b3e, 'the flash erases and the program routine every flash writer uses'],
  ],
  noLink: [
    [0x4011a7a4, 0x4011aa7a, 'BOOTSTRAP UPGRADE, which rewrites the boot flash'],
    [0x40096a6c, 0x40096ac2, 'the flash erase the OS store and calibration use'],
    [0x40096ac2, 0x40096af6, 'the flash erase used only for the bootstrap'],
    [0x40096af6, 0x40096b3e, 'the flash program routine every flash writer uses'],
  ],
  updateMax: 0x1f0000,                           // under the OS upgrade's 2 MiB decode buffer (devices/analog_rytm_mk1.py)
  areas: { ddr: [0x47be0000, 0x47c00000] },   // checked on a 1.74 unit (devices/analog_rytm_mk1.py)
  ddr: [0x47be0000, 0x47c00000],
  recovery: 'hold FUNC while powering on for the startup menu, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over MIDI DIN (not USB)',
}

export const ANALOG_FOUR_MK2: Device = {
  ...base,
  key: 'analog-four-mk2', name: 'Analog Four MKII', sysexId: 0x0b,
  releases: [
    rel('1.56', '5aea489db3e747547f71d5e08a2d20253149fe6258defad60306402d2fc23cb5',
      '748fec15ddc61bc3ff66047facba74328934c5fb59f427ddaf34a4e2629a474c', 2500408),
  ],
  stage: 0x40200000, flashAt: 0x80000, flashLimit: 0x300000,
  areas: { ddr: [0x47be0000, 0x47c00000], gap: [0x456472c0, 0x47be0000] },
  ddr: [0x47be0000, 0x47c00000],
  coreDdr: [['0.6', [0x45650000, 0x47c00000]]],   // core-a4mk2 0.6: 37.7 MB, the canary-checked gap and the area
  driveAlign: 0x1000,                              // the +Drive mods' area: what the flashed build leaves of coreDdr's, from a 4 KB boundary
  // knob ids (decided 2026-11-16): stock 1..0x126, core-a4mk2's 0x127..0x1ff, a mod's own from 0x200; flashed runs
  // below 0x4000, +Drive runs 0x4000..0x7fff
  paramIds: [0x200, [0x127, 0x1ff, "core-a4mk2's lock pseudo ids and their room"], [['core_params', false], ['core_knob_looks', true]]],
  driveKnobs: [0x4000, 0x7fff],
  recovery: 'hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over MIDI DIN (not USB)',
}

export const DIGITONE_MK2: Device = {
  ...base,
  updateMax: 0x200000,                           // conservative USB cap, measured on a unit; not found in code
  key: 'digitone-mk2', name: 'Digitone II', sysexId: 0x15,
  releases: [
    { ...rel('1.12', '7e5b8a733d4ab1fdf7e5d22757f7d8eddaa3cbf71d0858874a791844c02ceb35',
      'a098f620019b2ad38fd7e0397df39501fb1cf6bc26d0283915f34c49446fdf1a', 3196288),
      sharc: {
        section: 7, processor: 'ADSP-21569', isa: 'sharc+-visa', siliconRevision: null,
        storedSha256: '5ff93df8747bdf9eb28acf50391fb484d6aa6782bfe969b2b3807a8412645660', storedLen: 602076,
        decodedSha256: '336e340aa0cdcd34e314cfa44849f709a3134f6bd4cd57dfc7e15702c83115e2', decodedLen: 836956,
        storedMax: 0x100000, decodedMax: 0xfffff, recordCount: 95,
        l1Aliases: [[0x240000, 0x270000, 0x28240000], [0x2c0000, 0x2f0000, 0x282c0000],
          [0x300000, 0x320000, 0x28300000], [0x380000, 0x3a0000, 0x28380000]],
        l2Bytes: [0x20000000, 0x20100000], l2Pc16: [0xb80000, 0xc00000, 0x20000000],
        ddrBytes: [0x80000000, 0xa0000000],
        protected: [[0x200fa000, 0x200fe000, 'DN2 special window'], [0x200fe000, 0x20100000, 'Boot ROM workspace'],
          [0x282ec000, 0x282f0000, 'L1 block 1 cache'], [0x2831c000, 0x28320000, 'L1 block 2 cache'],
          [0x2839c000, 0x283a0000, 'L1 block 3 cache']],
        abi: 'dn2-master-mono32-v1', pitchRange: [0x282dda00, 0x282dde00],
        l2Abi: 'dn2-master-mono32-l2-v1', l2AppRange: [0x20090000, 0x20098000],
        kernelAbi: 'dn2-kernel-v1', pool: [0x20090000, 0x200b0000], kernelStateBytes: 256,
        kernelRegistrations: [['mk2_voice_synths', 'mk2-voice', true], ['mk2_voice_filters', 'mk2-filter', true],
          ['sdk_alpha_machines', 'sdk', false]],
        filterGAt: 16,
        voiceStack: [0x282de000, 0x282df000],
        voiceHook: [0x20004a36, '8715ffffeeff'],
        filterStack: [0x282df000, 0x282e0000], stackGuard: 0x57ac6a2d,
        peekRange: [0x282e0000, 0x282e0800], peekHook: [0x28393cbe, '01112600388a', '3e0617000000'],
        dataSites: { 0x20011576: ['3e07ff00f0ff', '3e0617004501'] },
        peekCode: '01112600388a0d102e001006ddc20006170050000d102e001806ddc50d112e0018068e0fff03decc200617005000be76bf860e102e0020060d112e0020060f102e0010060b102e001c060f112e001c06eec2000617005000dec1fbc10e102e003006eec50e112e0030060f112e0034060d112e0038060b102e003c06bbc20006170047008001fbb0220617004d000f112e003c060d112e0040060e112e004406089c7d058b0f800aabc0bf703f09089480060b0f0000ffff8401db0b0e0f0000eed5bec3200617003e0108940107089482078401de2aafce0b0f5a5a5a5aabce08948305abc3200617003e018b0ff0ffa001db0a0b102e000006abc30006170089000a112e0000068b0f00000b112e000406bfc68d0fff038001bdb0240617002c018b0f03008401eb0b200617002c013e020000bf02bec08001beb0040617002c010d0f240000008001edb0020617002c010d0f270000008001bdb004061700f1000d0f2c0000008001edb0020617002c010d0f2e0000c08001bdb004061700f1000d0f300000008001edb0020617002c010d0f310000c08001bdb004061700f1000d0f380000008001edb0020617002c010d0f390000c08001bdb004061700f1000d0f002000008001edb0020617002c010d0f0f2000a08001bdb0240617002c010d102e0004068b0fffffa001db0b3e020000bb02bec0bf70bf89089680053e0233000d00100617000a018e0ff0ffa001be0b0e0f0000ffffbecc8e0f0f008401ae0e3e020000ee0cedcd3e020000ee10ebcd08990207ddc53e020000bf01dbc30206170026018d0f00000d112e0004063e061c00629e8e0f0f008401ae0e3e020000ee0c8d0fff0fedcd3e020000ee10089902073e061c00629e8e0f0000089902073e061c00629ebe763f8001102e00140600112e00140601c1810fb80b800101b02206b800ab8a01102e00100610c001112e0010063e06b800ab8a0100',
        filterHook: [0x28392b0e, '140f5280c0db'],
        filterCapture: [0x8052dba4, '7e921c00'],
        coreRange: [0x282dd600, 0x282dda00], stackMax: 544, codeGuardBytes: 64,
        keyedPitchTable: [920, '314d5900969c5e00d23c6400b1326a004c837000073477009b4a7e0015cd8500e1c18d00c92f9600031e9f002e94a800'],
        ownedRanges: [[0x282dd600, 0x282e0800], [0x20090000, 0x200b0000]],
        patchRanges: [[0x2838489a, 0x283848a0], [0x20004a36, 0x20004a3c], [0x28392b0e, 0x28392b14],
          [0x8052dba4, 0x8052dba8], [0x28393cbe, 0x28393cc4]],
        activeKeyedCore: { segments: [[12, 'code', 'af2f375dc58a8ccd4b8abd89b1fb526441baa6fb5da9266c2b664e69d90fc829']],
          zero: [[1008, 16]], entries: { hook_offset_bytes: 12 }, stackBytes: 116 },
        hook: [0x2838489a, '8715ffff78ff', '3e06160006eb'],
      },
    },
  ],
  stage: 0x40400000, flashAt: 0x80000, flashLimit: 0x380000, trailer: 'hmac', hmacKeyFrom: [2, 'Multiplier'],
  areas: { ddr: [0x47f00000, 0x47f40000] },
  ddr: [0x47f00000, 0x47f40000],
  driveAlign: 0x1000,                              // +Drive mods' area: what the flashed build leaves of ddr's, from a 4 KB boundary
  driveShadow: true,                               // TELOS copies stock parts from RAM (desc.ts, shadow)
  slotClaims: [['mk2_voice_synths', 0, 32, 'mk2-synth'], ['sdk_alpha_machines', 0, 128, 'mk2-synth'],
    ['mk2_voice_filters', 0, 32, 'mk2-filter'],
    ['mk2_voice_pages', 0, 32, 'mk2-pages']],   // one pages entry per synth ID
  tableLimits: { sdk_alpha_machines: 16 },   // machines-dn2's SDK machines
  recovery: 'hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over MIDI (not USB)',
}

const DEVICES: Device[] = [DIGITAKT_MK1, DIGITAKT_MK2, DIGITONE_MK1, OCTATRACK, MODEL_CYCLES, MODEL_SAMPLES,
  ANALOG_RYTM_MK2, ANALOG_FOUR_MK2, DIGITONE_MK2, ANALOG_RYTM_MK1]

export const devices = (): Device[] => DEVICES

/** The file families syx.ts reads and writes. */
export const SYX_FAMILIES: readonly string[] = ['ele3', 'ele2']

/** -> where the version field of a syx.ts file of this family with this device byte is. */
export function versionAt(sysexId: number, container: 'ele3' | 'ele2' = 'ele3'): number {
  for (const d of DEVICES) if (d.container === container && d.sysexId === sysexId) return d.versionAt
  return container === 'ele2' ? 0x10 : 0x14
}

export function releaseFor(d: Device, syxSha256?: string, mainSha256?: string): Release | null {
  for (const r of d.releases) {
    if (syxSha256 && (syxSha256 === r.syxSha256 || syxSha256 === r.binSha256)) return r
    if (mainSha256 && r.mainSha256 === mainSha256) return r
  }
  return null
}

/** Whether format-2 mods (a core, the linker's areas) exist for it. */
export const linkable = (d: Device): boolean => d.ddr[1] > d.ddr[0]

/** Where an appended blob loads: the stock main OS's end. */
export const imageEnd = (d: Device, r: Release): number => d.mainLoad + r.mainLen

export const supported = (): string => DEVICES.flatMap(d => d.releases.map(r => `${d.name} ${r.version}`)).join(', ')

/** -> [Device, Release] for a stock file, by its hash. */
export function identify(syxSha256: string): [Device, Release] {
  for (const d of DEVICES) {
    const r = releaseFor(d, syxSha256)
    if (r) return [d, r]
  }
  throw new UnknownFirmware(`not a stock firmware elekloader knows (sha256 ${syxSha256}). Supported: ${supported()}`)
}

type Target = Record<string, unknown>

/** -> [Device, Release] a .elemod's "target" names. */
export function forTarget(target: unknown): [Device, Release] {
  if (!target || typeof target !== 'object' || Array.isArray(target)) throw new UnknownFirmware('no target')
  const t = target as Target
  const key = t.device
  for (const d of DEVICES) {
    if (key && d.key !== key) continue
    const r = releaseFor(d, typeof t.syx_sha256 === 'string' ? t.syx_sha256 : undefined)
    if (r && ('section3_sha256' in t ? t.section3_sha256 : r.mainSha256) === r.mainSha256
        && ('section3_len' in t ? t.section3_len : r.mainLen) === r.mainLen) return [d, r]
  }
  const s = (x: unknown) => (x === undefined ? '?' : str(x))
  throw new UnknownFirmware(`made for a firmware elekloader does not know (${s(t.product)} ${s(t.os)})`)
}

/** -> the "target" object a .elemod for this firmware carries. */
export const targetOf = (d: Device, r: Release) => ({
  device: d.key, product: d.name, os: r.version, syx_sha256: r.syxSha256, section3_sha256: r.mainSha256,
  section3_len: r.mainLen,
})
