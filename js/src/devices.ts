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
  activeKeyedCore?: { segments: [number, string, string][], zero: [number, number][], entries: Record<string, number>, stackBytes: number }
  hook?: [number, string, string]               // loader address, stock bytes, replacement bytes
  abi?: string
  pitchRange?: Range
  l2Abi?: string
  l2AppRange?: Range
  voiceAbi?: string
  voiceRange?: Range
  voiceTable?: string
  voiceAdapterSha256?: string
  voiceAdapterBytes?: number
  voiceStateBytes?: number
  voiceBindingOffset?: number
  voiceStackMax?: number
  voiceHook?: [number, string, string]
  filterAbi?: string
  filterRange?: Range
  filterTable?: string
  filterGAt?: number
  filterAdapterSha256?: string
  filterAdapterBytes?: number
  filterStateBytes?: number
  filterBindingOffset?: number
  filterStackMax?: number
  filterHook?: [number, string, string]
  filterCapture?: [number, string, string]      // the filter setup table's internal type 0 entry, sent to the adapter
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
  updateMax: number | null                      // decoded ELE3 update bytes: 8-byte preamble + container
  trailer: null | 'hmac'
  isa: 'coldfire'
  hmacKeyFrom: [number, string] | null          // for 'hmac': the section and seed the key is derived from
  container: 'ele3' | 'elek'
  versionLen: number
  versionAt: number                             // 'ele3': where the version field is in the container header
  versionOptional: boolean                      // true: a build given no version keeps stock's field
  protected: [number, number, string][]        // main OS bytes no mod may change
  relocatable: [number, number, number[], string][]
  keep: [number, number, string][]             // main OS bytes that look free but the OS reads: no site may write them
  blobMax: number | null
  areas: Record<string, Range>                  // in the profile's order
  ddr: Range
  coreDdr: [string, Range][]                   // [core version, area]: from that core on, the linker's area in place of ddr
  slotClaims: [string, number, number, string][]   // [table, at, count, prefix]: a contribution to table points at a descriptor whose long at +at is a sound slot; the mod claims prefix:<slot>
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
  versionLen: 4, versionAt: 0x14, versionOptional: false, protected: [], relocatable: [], keep: [], blobMax: null, sramCode: [0, 0] as Range, coreDdr: [] as [string, Range][], slotClaims: [] as [string, number, number, string][], fastTable: '',
  imageFree: [], reserve: null, dspPayloads: {}, dspAreas: [], trailer: null, updateMax: null,
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
  recovery: 'hold FUNC while powering on for the startup menu, then send the stock .syx',
}

export const DIGITAKT_MK2: Device = {
  ...base,
  key: 'digitakt-mk2', name: 'Digitakt II', sysexId: 0x14,
  releases: [
    rel('1.17', '26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5',
      'a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964', 3275616),
  ],
  stage: 0x40400000, flashAt: 0x80000, flashLimit: 0x380000, trailer: 'hmac', hmacKeyFrom: [2, 'Master Overdrive'],
  areas: { ddr: [0x47f00000, 0x47f40000], 'sram-tail': [0x8000f100, 0x80010000], 'sram-block': [0x80006e80, 0x80008000] },
  ddr: [0x47f00000, 0x47f40000], sramCode: [0x8000f100, 0x80010000], fastTable: 'core_fast',
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
  slotClaims: [['core_params', 8, 79, 'soundslot']],   // a parameter slot's descriptor names its sound slot at +8
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
  areas: { ddr: [0x40a955e0, 0x41495de0] },
  ddr: [0x40a955e0, 0x41495de0],
  imageFree: [[0x400c45b0, 0x400c4702], [0x400d24e0, 0x400d2ce0], [0x400d64e0, 0x400d7c3c]],
  reserve: [6144, { arena_base: [0x40a955e0, 6144], __arena_pages: [14602, -1], __arena_fill: [14603, -1],
    __arena_clear: [0x05590800, -6144] }],
  dspPayloads: { A: [0x400e2324, 0x136cb], B: [0x400f59ef, 0x12d05] },
  dspAreas: [['A', 0xaa8, 0xbad, 'dsp:harvest:SPATIALIZER', "SPATIALIZER's code on payload A (core 0)"]],
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

export const ANALOG_RYTM_MK2: Device = {
  ...base,
  key: 'analog-rytm-mk2', name: 'Analog Rytm MKII', sysexId: 0x0c,
  releases: [
    rel('1.73', '8ad671087ec30433d5407c7380d2396e0915ab931c5d3dd18a78bf284d3d1e52',
      '28d9ef40c895e03f17dfce096543109adaf708947392db72818fc2a412d685d6', 3005432),
    rel('1.74', 'cb0f99d620edde994fb860c0e79cc2464e427d06882828cfdfb00fc53700e123',
      '34cd0c4da8e4b810d1b25284475a99ea53821e2ab7d30a978bf15dc578251f3b', 3005432),
  ],
  stage: 0x40200000, flashAt: 0x80000, flashLimit: 0x300000,
  areas: { ddr: [0x47be0000, 0x47c00000] },
  ddr: [0x47be0000, 0x47c00000],
  recovery: 'hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over MIDI DIN (not USB)',
  slotClaims: [['core_machines', 0, 53, 'machine']],   // a machine's descriptor names its slot at +0
}

export const ANALOG_FOUR_MK2: Device = {
  ...base,
  key: 'analog-four-mk2', name: 'Analog Four MKII', sysexId: 0x0b,
  releases: [
    rel('1.56', '5aea489db3e747547f71d5e08a2d20253149fe6258defad60306402d2fc23cb5',
      '748fec15ddc61bc3ff66047facba74328934c5fb59f427ddaf34a4e2629a474c', 2500408),
  ],
  stage: 0x40200000, flashAt: 0x80000, flashLimit: 0x300000,
  areas: { ddr: [0x47be0000, 0x47c00000] },
  ddr: [0x47be0000, 0x47c00000],
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
        voiceAbi: 'dn2-voice-mono32-l2-v1', voiceRange: [0x20090000, 0x20094000],
        voiceTable: 'mk2_voice_synths',
        voiceAdapterSha256: '11cb0abb8a60cd9e97253ff7a8c063b90bd70f110a1cee95c52c98a98dbbcef1',
        voiceAdapterBytes: 2048, voiceStateBytes: 1024, voiceBindingOffset: 15872, voiceStackMax: 128,
        voiceHook: [0x20004a36, '8715ffffeeff', '3e06bc000080'],
        filterAbi: 'dn2-filter-mono32-l2-v1', filterRange: [0x20094000, 0x20098000],
        filterTable: 'mk2_voice_filters', filterGAt: 16,
        filterAdapterSha256: '25ecf301d2dfc322b5311afa58cd00f89316f0afe7df66a45e62c0aca590538c',
        filterAdapterBytes: 2048, filterStateBytes: 1024, filterBindingOffset: 15872, filterStackMax: 20,
        filterHook: [0x28392b0e, '140f5280c0db', '3e06bc0000a0'],
        filterCapture: [0x8052dba4, '7e921c00', '2aa3bc00'],
        coreRange: [0x282dd600, 0x282dda00], stackMax: 544, codeGuardBytes: 64,
        keyedPitchTable: [920, '314d5900969c5e00d23c6400b1326a004c837000073477009b4a7e0015cd8500e1c18d00c92f9600031e9f002e94a800'],
        ownedRanges: [[0x282dd600, 0x282dde00], [0x20090000, 0x20098000]],
        patchRanges: [[0x2838489a, 0x283848a0], [0x20004a36, 0x20004a3c], [0x28392b0e, 0x28392b14],
          [0x8052dba4, 0x8052dba8]],
        activeKeyedCore: { segments: [[12, 'code', 'af2f375dc58a8ccd4b8abd89b1fb526441baa6fb5da9266c2b664e69d90fc829']],
          zero: [[1008, 16]], entries: { hook_offset_bytes: 12 }, stackBytes: 116 },
        hook: [0x2838489a, '8715ffff78ff', '3e06160006eb'],
      },
    },
  ],
  stage: 0x40400000, flashAt: 0x80000, flashLimit: 0x380000, trailer: 'hmac', hmacKeyFrom: [2, 'Multiplier'],
  areas: { ddr: [0x47f00000, 0x47f40000] },
  ddr: [0x47f00000, 0x47f40000],
  slotClaims: [['mk2_voice_synths', 0, 32, 'mk2-synth'], ['mk2_voice_filters', 0, 32, 'mk2-filter'],
    ['mk2_voice_pages', 0, 32, 'mk2-pages']],   // one pages entry per synth ID
  recovery: 'hold FUNC while powering on for the STARTUP menu, press TRIG 4 (OS UPGRADE), '
    + 'then send the stock .syx over MIDI (not USB)',
}

const DEVICES: Device[] = [DIGITAKT_MK1, DIGITAKT_MK2, DIGITONE_MK1, OCTATRACK, MODEL_CYCLES, MODEL_SAMPLES,
  ANALOG_RYTM_MK2, ANALOG_FOUR_MK2, DIGITONE_MK2]

export const devices = (): Device[] => DEVICES

/** -> where the version field of an 'ele3' file with this device byte is. */
export function versionAt(sysexId: number): number {
  for (const d of DEVICES) if (d.container === 'ele3' && d.sysexId === sysexId) return d.versionAt
  return 0x14
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
