// SPDX-License-Identifier: GPL-3.0-or-later
// +Drive mods on the host: the build-checker (docs/FORMAT.md, "+Drive mods"). Ported from elekloader's drive.py
// (GPL-2.0-or-later): check() is pass 1, the check rounds and the layout, with the same order, codes, limits, details
// and notes, so a page refuses a set exactly as `elekloader check` and the Drive Loader's boot linker do. Pass 2 and
// the merge (`elekloader link --device-layout`) stay in the Python: a page only needs to know what would load.
//
// A build here is its descriptor alone (`/mods/.build`, or the same bytes in a data container): what the boot linker
// reads. Nothing here reads a clock or iterates in hash order.
import * as container from './container.ts'
import * as desc from './desc.ts'
import { ModError, cmpVersions, formatOf, isDict, pyEq } from './elemod.ts'
import { FORMAT2, KNOBS, Mod2, STD_SETS } from './link.ts'
import * as store from './store.ts'
import { devices } from './devices.ts'
import { JSONDecodeError, UnicodeDecodeError, decodeUtf8, loads } from './pyjson.ts'
import { pyInt as pyIntOf, repr, str, truthy } from './py.ts'
import { crc32 } from './zip.ts'

export const MAX_MODS = 256                // lines of /mods/active, and status records
export const ID_MAX = 28                   // a status record's id field (27 bytes and a NUL)
export const DETAIL = 32                   // a detail string (31 bytes and a NUL)
export const NAME_MAX = 128                // a path below /mods in /mods/active (127 bytes, folders included)
export const RECORD = 32
// the boot linker's own room (dl.h DL_POOL_*, dlink.c pools()): each pool per started MB of the +Drive area (at least
// 1), the symbols past the flashed build's own names, each capped at POOL_CAP; past one a mod is refused with LIMIT
export const POOL_PER_MB = { arena: 12288, lists: 1024, syms: 1024, contribs: 256, sites: 256, sdata: 4096, colls: 64 }
export const NAME_N_MAX = 60               // a table's name is shorter, so NAME_n is a global name too
export const POOL_CAP = 0xfff0
export const INIT = 'drive_init'
export const SITE_MAIN_LO = 0x40000400    // dl.h DL_MAIN_LO: the Rytm MKII's main_load, where a site may start

export const NONE = 0, LOADED = 1, REFUSED = 2, DROPPED = 3
export const STATES: Record<number, string> = { 0: 'none', 1: 'loaded', 2: 'refused', 3: 'dropped' }
export const CODES: Record<number, [string, string]> = {
  0: ['ok', 'loaded'],
  1: ['missing', 'the file is not in /mods'],
  2: ['changed', 'the file is not the one chosen (its CRC-32 differs)'],
  3: ['too-big', 'the file is larger than the room the boot reads it into'],
  4: ['json', 'not a JSON object'],
  5: ['format', 'not a format-2 mod the loader can read'],
  6: ['target', 'made for another device or OS'],
  7: ['feature', 'uses {detail}, which a +Drive mod cannot'],
  8: ['duplicate', '{detail} is flashed already, or chosen twice'],
  9: ['core', 'needs core {detail} or newer'],
  10: ['requires', 'requires {detail}'],
  11: ['conflicts', 'conflicts with {detail}'],
  12: ['export', 'exports {detail}, which another mod exports'],
  13: ['claim', 'claims {detail}, which another mod claims'],
  14: ['import', 'imports {detail}, which nothing exports'],
  15: ['table', 'adds to the table {detail}, which takes no +Drive entries of that size'],
  16: ['full', 'the table {detail} is full ({arg} spare)'],
  17: ['room', 'the +Drive area is {arg} KB short'],
  18: ['range', 'a 16-bit reference is out of range'],
  19: ['limit', 'past the loader\'s own limits'],
  20: ['count', 'more than 256 mods are chosen'],
  // TELOS on the Digitone mk1 (a build whose descriptor carries the stock shadow): sites and regions
  21: ['site', 'a site overlaps one of {detail}'],
  22: ['flashed', 'a site at {detail} overlaps what the flashed build writes'],
  24: ['stock', 'the bytes at {detail} are not stock\'s'],
  26: ['busy', 'no quiet moment to write the sites in'],
  27: ['region', 'a region overlaps one of {detail}'],
  28: ['outside', 'a region at {detail} is outside the device\'s free areas'],
  // the boot-linked sites (a build whose descriptor carries the site map, no shadow); 29, storage, is the Python's alone
  30: ['clash', 'site 0x{detail}: {why}'],
  31: ['std', 'subscribes to {detail}, which the core does not map'],
  // +Drive knob ids (F_KNOBS, the A4's dlink.c): a fixed id the rules refuse, or no run left
  32: ['ids', 'knob ids {detail}'],
}
export const CODE: Record<string, number> = Object.fromEntries(Object.entries(CODES).map(([i, [n]]) => [n, Number(i)]))
// DL_R_CLASH's kind (its argument's high byte; the low byte is the other mod's line for SITE_MOD)
export const SITE_CORE = 1, SITE_MOD = 2, SITE_PRELINK = 3, SITE_STOCK = 4
export const BOOT_ONLY = ['missing', 'changed', 'busy']   // the host has the files it is given; busy is the unit's moment
export const SKIPS = ['none', 'key', 'armed', 'drive', 'abi', 'unstable', 'count', 'memory']
export const NO_FIELDS = ['sites', 'collections', 'copied', 'storage', 'sharc']
export const SITE_FIELDS = ['sites', 'copied']   // ... except these on a build with a site map (ABI 1.2)
const RSIZE: Record<string, number> = { abs32: 4, pc32: 4, pc16: 2 }
const RSIZE16: Record<string, number> = { ...RSIZE, abs16: 2 }   // on F_ABS16 (the A4's dlink.c)

const utf8 = new TextEncoder()
const ulen = (s: string) => utf8.encode(s).length
const hex8 = (n: number) => '0x' + (n >>> 0).toString(16).padStart(8, '0')
const align = (v: number, a: number) => Math.ceil(v / a) * a
const pyInt = (v: unknown) => typeof v === 'number' && Number.isInteger(v)      // type(v) is int
const pyIsInt = (v: unknown) => pyInt(v) || typeof v === 'boolean'              // isinstance(v, int)
const num = (v: unknown) => (typeof v === 'boolean' ? Number(v) : (v as number))
const cmp = (a: string, b: string) => {                                            // Python's str order (code points)
  const x = [...a], y = [...b]
  for (let i = 0; i < Math.min(x.length, y.length); i++)
    if (x[i] !== y[i]) return x[i].codePointAt(0)! - y[i].codePointAt(0)!
  return x.length - y.length
}

/** Python's int(s, 0) on a string: decimal, 0x, 0o or 0b, underscores between digits. -> null (ValueError). */
function pyInt0(s: string | null): number | null {
  if (s === null) return null
  const t = s.trim().toLowerCase(), m = /^([+-]?)(0x[0-9a-f](_?[0-9a-f])*|0o[0-7](_?[0-7])*|0b[01](_?[01])*|[1-9](_?\d)*|0(_?0)*)$/.exec(t)
  if (!m) return null
  const body = m[2].replace(/_/g, ''), v = /^0[xob]/.test(body) ? Number(body) : parseInt(body, 10)
  return m[1] === '-' ? -v : v
}
/** Python's str(v) for int(str(v), 0): a string as it is, an int its digits; anything else no number (null). */
const pyStr = (v: unknown): string | null => (typeof v === 'string' ? v : pyInt(v) ? String(v) : null)
/** Python's int(v): an int or bool, a float truncated, a decimal string. -> null (ValueError/TypeError). */
function pyIntCall(v: unknown): number | null {
  if (typeof v === 'boolean') return Number(v)
  if (typeof v === 'number') return Number.isFinite(v) ? Math.trunc(v) : null
  if (typeof v !== 'string' || !/^\s*[+-]?\d(_?\d)*\s*$/.test(v)) return null
  return parseInt(v.replace(/_/g, ''), 10)
}

/** A status's line, for a person. drive.message. */
export function message(code: number, detail = '', arg = 0): string {
  if (code === CODE.clash) {
    const sp = detail.indexOf(' ')
    const addr = sp < 0 ? detail : detail.slice(0, sp), owner = sp < 0 ? '' : detail.slice(sp + 1)
    const why = ({ [SITE_CORE]: `taken by ${owner || 'the build'}`, [SITE_MOD]: `taken by another +Drive mod (line ${(arg & 0xff) + 1})`,
      [SITE_PRELINK]: 'in a pre-link range', [SITE_STOCK]: 'not stock bytes' } as Record<number, string>)[arg >> 8] ?? 'refused'
    return CODES[code][1].replace('{detail}', addr).replace('{why}', why)
  }
  return CODES[code][1].replace('{detail}', detail).replace('{arg}', String(arg))
}

/** A file's CRC-32 in /mods/active: zlib's (the boot linker's). The container's footer hash is container.crc. */
export const fileCrc = (raw: Uint8Array): number => crc32(raw)

// ---- payload skip (skip.c's twin) ---------------------------------------------------------------------------------

export type Payload = { off: number; len: number; sha256: string }
const isHex = (c: number) => (c >= 0x30 && c <= 0x39) || (c >= 0x61 && c <= 0x66) || (c >= 0x41 && c <= 0x46)
const asText = (b: number[]) => String.fromCharCode(...b)

/** A file as a loader with F_PAYLOAD_SKIP stages it (mods/drive-loader-a4mk2/skip.c): a skippable .payload's hex left
 * out, "" in its place. -> [staged bytes, the payload or null]. Followed a byte at a time as the loader does. drive.stage. */
export function stage(raw: Uint8Array): [Uint8Array, Payload | null] {
  const P_NONE = 0, P_SECT = 1, P_PAY = 2, P_PARTS = 3, P_PART = 4
  let depth = 0
  const types: number[] = []
  let instr = false, esc = false, iskey = false, keyexp = false
  let path = P_NONE, key: string | null = null, skey: string | null = null, elem = 0, nparts = 0
  let hexFirst = false, innum = false, haveLen = false, haveSha = false, done = false
  let cap: number[] = [], num = 0, plen = 0, sha: string | null = null
  const out: number[] = []
  let i = 0, pay: Payload | null = null
  const n = raw.length
  while (i < n) {
    const c = raw[i++]
    out.push(c)
    if (instr) {
      if (esc) esc = false
      else if (c === 0x5c) esc = true
      else if (c === 0x22) {
        instr = false
        const w = cap.length <= 64 ? asText(cap) : null
        if (iskey) {
          key = null
          if (depth === 1 && w === 'sections') key = 'sections'
          else if (path === P_SECT && depth === 2 && w === '.payload') key = 'payload'
          else if (path === P_PAY && depth === 3 && (w === 'len' || w === 'sha256' || w === 'parts')) key = w
        } else if (skey === 'sha256') {
          haveSha = w !== null && w.length === 64 && cap.every(isHex)
          sha = haveSha ? w!.toLowerCase() : null
        } else if (path === P_PART && depth === 5 && elem === 0) {
          hexFirst = w === 'hex'
        }
        continue
      }
      if (cap.length <= 64) cap.push(c)
      continue
    }
    if (innum) {
      if (c >= 0x30 && c <= 0x39) {
        num = num > Math.floor(0x7fffffff / 10) ? 0xffffffff : num * 10 + (c - 0x30)
        continue
      }
      innum = false
      haveLen = num <= 0x7fffffff
      plen = num
    }
    if (c === 0x20 || c === 0x09 || c === 0x0a || c === 0x0d) continue
    const top = depth > 0 && depth <= 8 ? types[depth - 1] : null
    if (c === 0x3a) { keyexp = false; continue }
    if (c === 0x2c) {
      keyexp = top === 0x7b
      if (path === P_PART && depth === 5) elem++
      continue
    }
    if (c === 0x7d || c === 0x5d) {
      if (path === P_PART && depth === 5) path = P_PARTS
      else if (path === P_PARTS && depth === 4) path = P_PAY
      else if (path === P_PAY && depth === 3) path = P_SECT
      else if (path === P_SECT && depth === 2) path = P_NONE
      if (depth) { depth--; types.length = Math.min(types.length, depth) }
      keyexp = false; key = null
      continue
    }
    if (c === 0x22 && keyexp && top === 0x7b) { instr = true; iskey = true; cap = []; continue }
    const k = key
    key = null
    if (c === 0x22) {
      instr = true; iskey = false; cap = []; skey = k
      if (path === P_PART && depth === 5 && elem === 1 && hexFirst && nparts === 1 && haveLen && haveSha && !done) {
        done = true; skey = null; cap = new Array(65).fill(0)
        pay = { off: i, len: plen, sha256: sha! }
        i = Math.min(n, i + 2 * plen)
      }
      continue
    }
    if (c === 0x7b || c === 0x5b) {
      if (c === 0x7b && k === 'sections' && depth === 1) path = P_SECT
      else if (c === 0x7b && k === 'payload') { path = P_PAY; haveLen = haveSha = false; nparts = 0 }
      else if (c === 0x5b && k === 'parts' && path === P_PAY) path = P_PARTS
      else if (c === 0x5b && path === P_PARTS && depth === 4) { path = P_PART; nparts++; elem = 0; hexFirst = false }
      depth = Math.min(depth + 1, 0xff)
      if (depth <= 8) types.push(c)
      keyexp = c === 0x7b
      continue
    }
    if (k === 'len' && c >= 0x30 && c <= 0x39) { innum = true; num = c - 0x30 }
  }
  return [Uint8Array.from(out), pay]
}

/** A file's bytes as the build's loader stages them: without a skippable payload's hex on F_PAYLOAD_SKIP. drive.staged. */
export function staged(build: Build | null, raw: Uint8Array | null): Uint8Array | null {
  return raw === null || !build?.payloadSkip ? raw : stage(raw)[0]
}

/** A .payload the stage skipped, as dlink.c reads it. drive._skipped_payload. */
function skippedPayload(d: unknown): boolean {
  if (!isDict(d)) return false
  const have = new Set<string>()
  for (const [k, v] of Object.entries(d)) {
    if (k === 'align') {
      if (!pyInt(v) || num(v) < 1 || num(v) > 4096 || num(v) & (num(v) - 1)) return false
    } else if (k === 'len') {
      if (!pyInt(v) || num(v) < 0 || num(v) > 0x7fffffff) return false
      have.add(k)
    } else if (k === 'sha256') {
      if (typeof v !== 'string' || ulen(v) !== 64) return false
      have.add(k)
    } else if (k === 'parts') {
      if (have.size !== 2 || !(Array.isArray(v) && v.length === 1 && Array.isArray(v[0]) && v[0].length === 2
          && v[0][0] === 'hex' && v[0][1] === '')) return false
      have.add(k)
    }
  }
  return have.has('parts')
}

/** Relocations that point into a skipped .payload ("sec:.payload"). drive._payload_relocs. */
const payloadRelocs = (v: unknown): boolean =>
  Array.isArray(v) && v.some(r => Array.isArray(r) && r.includes('sec:.payload'))

// ---- the build ---------------------------------------------------------------------------------------------------

export type Global = ['abs' | 'table' | 'count' | 'desc', number]

/** A flashed build, as the boot linker sees it: its descriptor. drive.Build (without an image). */
export class Build {
  d: desc.Decoded
  crc: number; deviceKey: string; os: string
  modules: desc.Decoded['modules']
  flashed: string[]
  tables: desc.DescTable[]
  table: Map<string, number>
  claims: string[]
  std: Map<string, string>
  stdMap: boolean
  shadow: [number, Uint8Array][]; telos: boolean; regions: [number, number, string][]
  areas: [number, number][]                 // a region must be inside one (TELOS)
  ramEnd: number; lo: number; hi: number; staging: number; stagingSize: number
  stageTop: boolean                         // the boot reads each file into the area itself (F_STAGE_TOP)
  payloadSkip: boolean                      // the boot stages each file without its .payload's hex (F_PAYLOAD_SKIP)
  rsize: Record<string, number>             // the relocation kinds the boot linker takes, with their sizes (F_ABS16)
  collections: boolean                      // +Drive mods may declare tables (F_COLLECTIONS, the A4's dlink.c)
  knobs: boolean                            // +Drive mods' "knobs" (F_KNOBS, on a device with driveKnobs)
  knobRange: [number, number] | null; paramIds: NonNullable<ReturnType<typeof devices>[number]['paramIds']> | null
  knobIds: [number, number, string][]       // the ids the flashed modules take (claims of kind 4)
  globals: Map<string, Global>
  nsyms: number
  pools: Record<keyof typeof POOL_PER_MB, number>
  siteMap: [number, number, number, number][]
  prelink: [number, number, number][]
  mainLo: number; mainHi: number
  takeClaims: boolean                       // contributions' claims (F_CLAIMS, the A4's dlink.c), where the site map checks them

  constructor(d: desc.Decoded) {
    this.d = d
    this.crc = d.crc; this.deviceKey = d.deviceKey; this.os = d.os
    this.modules = d.modules
    this.flashed = d.modules.map(m => m.name)
    this.tables = d.tables
    this.table = new Map(d.tables.map((t, i) => [t.name, i]))
    this.claims = d.claims.filter(([k]) => k === desc.CLAIM_NAME).map(([, , nm]) => nm)
    // the core's std map (F_STD_MAP, CLAIM_STD "<event> <table>"): a std mod's subscriptions' tables
    this.stdMap = !!((d.flags ?? 0) & desc.F_STD_MAP)
    this.std = new Map(d.claims.filter(([k]) => k === desc.CLAIM_STD).map(([, , nm]) => {
      const sp = nm.indexOf(' ')
      return [nm.slice(0, sp), nm.slice(sp + 1)] as [string, string]
    }))
    // TELOS (a descriptor with the stock shadow, ABI 1.2): sites and regions are taken, checked as the boot linker
    // checks them (DL_SITES, DL_REGIONS). With F_STOCK_SHADOW (the A4's loader) the shadow serves the stock parts
    // alone (stockBytes), and the build is no TELOS
    this.shadow = d.shadow ?? []
    this.telos = this.shadow.length > 0 && !((d.flags ?? 0) & desc.F_STOCK_SHADOW)
    this.areas = Object.values(devices().find(x => x.key === d.deviceKey)?.areas ?? {}) as [number, number][]
    this.regions = d.claims.filter(([k]) => k === desc.CLAIM_REGION).map(([, mi, nm]) => {
      const [lo, hi] = nm.split(' ')
      return [pyInt0(lo)!, pyInt0(hi)!, d.modules[mi].name]
    })
    ;[this.ramEnd, this.lo, this.hi, this.staging, this.stagingSize] = d.area
    this.stageTop = !!((d.flags ?? 0) & desc.F_STAGE_TOP)
    this.payloadSkip = !!((d.flags ?? 0) & desc.F_PAYLOAD_SKIP)
    this.rsize = (d.flags ?? 0) & desc.F_ABS16 ? RSIZE16 : RSIZE
    this.collections = !!((d.flags ?? 0) & desc.F_COLLECTIONS)
    // +Drive knob ids (F_KNOBS, docs/DRIVE.md "Knob ids on the +Drive"): the device's +Drive range and rules (the
    // profile's driveKnobs and paramIds, which the A4's dlink.c has as constants), and the flashed modules' ids
    const dev = devices().find(x => x.key === d.deviceKey)
    this.knobs = !!((d.flags ?? 0) & desc.F_KNOBS) && !!(dev?.driveKnobs && dev.paramIds)
    this.knobRange = this.knobs ? dev!.driveKnobs : null
    this.paramIds = this.knobs ? dev!.paramIds : null
    this.knobIds = d.claims.filter(([k]) => k === desc.CLAIM_KNOBS).map(([, mi, nm]) => {
      const [a, b] = nm.split(' ')
      return [parseInt(a, 16), parseInt(b, 16), d.modules[mi].name] as [number, number, string]
    })
    this.globals = new Map()
    const put = (k: string, v: Global) => { if (!this.globals.has(k)) this.globals.set(k, v) }
    for (const m of d.modules) for (const [nm, a] of m.exports) put(nm, ['abs', a])
    d.tables.forEach((t, i) => {
      put(t.name, ['table', i])
      if ([...t.name].length < 60) put(t.name + '_n', ['count', i])
    })
    put(desc.SYM, ['desc', 0])
    this.nsyms = d.modules.reduce((n, m) => n + m.exports.length, 0)
      + d.tables.reduce((n, t) => n + ([...t.name].length < 60 ? 2 : 1), 0) + 1
    const mb = this.hi > this.lo ? Math.max(1, Math.floor((this.hi - this.lo + 0xfffff) / 0x100000)) : 1
    const pool = (k: keyof typeof POOL_PER_MB) => Math.min(POOL_CAP, POOL_PER_MB[k] * mb + (k === 'syms' ? this.nsyms : 0))
    this.pools = { arena: pool('arena'), lists: pool('lists'), syms: pool('syms'), contribs: pool('contribs'),
      sites: pool('sites'), sdata: pool('sdata'), colls: pool('colls') }
    // the main OS a +Drive site may change: from the device's main_load to the boot copier (the site map's kind 4);
    // 0 on a descriptor without a site map (ABI 1.0 and 1.1)
    this.siteMap = d.abi[1] >= 2 ? (d.sites ?? []) : []
    this.prelink = d.prelink ?? []
    const boots = this.siteMap.filter(x => x[2] === desc.SITE_BOOT).map(x => x[0])
    this.mainLo = SITE_MAIN_LO
    this.mainHi = !this.telos && boots.length && Math.min(...boots) > SITE_MAIN_LO ? Math.min(...boots) : 0
    this.takeClaims = !!((d.flags ?? 0) & desc.F_CLAIMS) && !!this.mainHi
  }

  label(): string {
    const core = this.modules[0]
    return `${this.deviceKey} ${this.os}, ${core.name} ${core.version}, build ${hex8(this.crc)}`
  }
}

/** A build from `/mods/.build`'s bytes: the descriptor alone, or in a data container. drive.load_build (a built image
 * is the Python's). */
export function loadBuild(raw: Uint8Array): Build {
  if (raw.length >= 4 && new DataView(raw.buffer, raw.byteOffset).getUint32(0) === container.MAGIC) raw = container.parse(raw)[1]
  if (raw.length >= 4 && raw[0] === 0x45 && raw[1] === 0x4c && raw[2] === 0x4b && raw[3] === 0x44) return new Build(desc.decode(raw))
  throw new ModError('the build has no build descriptor: it was linked without the Drive Loader')
}

// ---- the chosen files --------------------------------------------------------------------------------------------

/** One line of /mods/active: a file, its state and, once read, its mod. drive.Choice. */
export class Choice {
  line: number; name: string; raw: Uint8Array | null; crc: number | null
  state = NONE; code = 0; arg = 0; detail = ''; note = ''
  label: string
  mod: Mod2 | null = null
  id: string | null = null
  exports: string[] = []
  tables: [string, number][] = []           // the tables it declares (+Drive collections): [name, entry]
  sites: [number, number][] = []; regions: [number, number][] = []   // TELOS: [lo, hi) each
  driveSites: [number, number, boolean][] = []   // [lo, hi, is a site]: its sites, then its copied blocks
  driveClaims: [number, number][] = []            // [lo, hi]: its contributions' claims (F_CLAIMS)
  knobFixed: [string, number, number][] = []   // the knob ids its entries name (+Drive knob ids): [table, a, b]
  alive = false

  constructor(line: number, name: string, raw: Uint8Array | null) {
    this.line = line; this.name = name; this.raw = raw
    this.crc = raw === null ? null : fileCrc(raw)
    this.label = name
  }

  refuse(code: string, detail: unknown = '', arg = 0, note = '', state = REFUSED): false {
    this.alive = false
    this.state = state; this.code = CODE[code]; this.arg = Math.min(arg, 0xffff)
    if (truthy(detail)) this.detail = str(detail)
    this.note = note
    return false
  }

  why(): string { return message(this.code, this.detail, this.arg) + (this.note ? ` (${this.note})` : '') }
}

/** A field the boot linker counts as absent: missing, null, false, 0, "", [] or {}. */
function empty(v: unknown): boolean {
  return v === undefined || v === null || v === false || v === 0 || v === ''
    || (Array.isArray(v) && !v.length) || (isDict(v) && !Object.keys(v).length)
}

const g = (d: Record<string, any>, k: string) => (k in d ? d[k] : null)          // dict.get

/** Python's `for x in v` over a JSON value (a dict gives its keys, a string its characters). */
function each(v: unknown): unknown[] {
  if (Array.isArray(v)) return v
  if (typeof v === 'string') return [...v]
  if (isDict(v)) return Object.keys(v)
  throw new TypeError(`'${typeof v}' object is not iterable`)
}

const symbol = (v: unknown) => Array.isArray(v) && v.length === 2 && ['.run', '.bss', 'abs'].includes(v[0]) && pyInt(v[1])

function sectionLen(d: unknown): number {
  if (!isDict(d)) return 0
  let n = 0
  for (const p of each(g(d, 'parts') ?? [])) {
    if (!Array.isArray(p) || !p.length) continue
    if (p[0] === 'hex') n += Math.floor((typeof p[1] === 'string' ? [...p[1]].length : (p[1] as unknown[]).length) / 2)
    else if (p[0] === 'stock' && p.length === 3 && pyIsInt(p[2])) n += num(p[2])
  }
  return n
}

/** bytes.fromhex: pairs of hex digits, ASCII whitespace between pairs. -> its length, or null (ValueError/TypeError). */
function fromhexLen(v: unknown): number | null {
  if (typeof v !== 'string') return null
  let n = 0
  for (let i = 0; i < v.length;) {
    if (' \t\n\r\x0b\x0c'.includes(v[i])) { i++; continue }
    if (!/^[0-9a-fA-F]{2}$/.test(v.slice(i, i + 2))) return null
    n++
    i += 2
  }
  return n
}

type State = { syms: number; contribs: number; arena: number; lists: number; sites: number; sdata: number; colls: number }

/** A number as the boot linker's js_int reads it: a JSON integer or a string int(v, 0) reads, within 32 bits signed.
 * drive._num. */
function num32(v: unknown): number | null {
  const n = typeof v === 'boolean' ? null : typeof v === 'number' ? (Number.isInteger(v) ? v : null)
    : typeof v === 'string' ? pyIntOf(v) : null
  return n !== null && n >= -0x80000000 && n < 0x80000000 ? n : null
}

const overlap = (a: number, b: number, lo: number, hi: number) => a < hi && lo < b

/** A refused site's detail: 8 hex digits, then the flashed owner (at most 22 bytes of its name). drive._site_detail. */
function siteDetail(addr: number, owner = ''): string {
  let d = (addr >>> 0).toString(16).padStart(8, '0')
  if (owner) d += ' ' + new TextDecoder().decode(utf8.encode(owner).slice(0, 22)).replace(/\uFFFD$/, '')
  return d
}

/** Pass 1's part for "sites" and "copied", after the exports: drive._scan_sites without the stock bytes (a
 * descriptor alone has none). -> [lo, hi, is a site][], or null after refusing c. */
function scanSites(build: Build, c: Choice, doc: Record<string, any>, state: State): [number, number, boolean][] | null {
  const out: [number, number, boolean][] = []
  let sdata = 0
  if (!build.mainHi) return out
  for (const [copied, key] of [[false, 'sites'], [true, 'copied']] as [boolean, string][]) {
    const v = g(doc, key)
    if (empty(v)) continue
    if (!Array.isArray(v)) { c.refuse('format', '', 0, key); return null }
    for (const x of v) {
      if (!isDict(x)) { c.refuse('format', '', 0, key); return null }
      let lo: number, hi: number
      if (!copied) {
        const addr = num32(g(x, 'addr')), n = num32(g(x, 'len'))
        const nw = typeof g(x, 'new') === 'string' ? fromhexLen(x.new) : null
        const sha = g(x, 'stock_sha256')
        let ok = addr !== null && n !== null && nw !== null && (g(x, 'kind') === 'code' || g(x, 'kind') === 'data')
          && typeof sha === 'string' && /^[0-9a-fA-F]{64}$/.test(sha) && n > 0 && nw === n
          && build.mainLo <= addr && addr < build.mainHi && n <= build.mainHi - addr
        if (ok && 'relocs' in x) {
          const rel = x.relocs
          if (!Array.isArray(rel)) ok = false
          else for (const r of rel) {
            const size = Array.isArray(r) && r.length === 4 && typeof r[1] === 'string' && Object.hasOwn(build.rsize, r[1]) ? build.rsize[r[1]] : undefined
            const off = size ? num32(r[0]) : null
            if (!size || off === null || off < 0 || off + size > n! || typeof r[2] !== 'string' || num32(r[3]) === null) { ok = false; break }
          }
        }
        if (!ok) { c.refuse('format', '', 0, `a site ${repr(g(x, 'addr'))}`); return null }
        lo = addr!; hi = addr! + n!
      } else {
        const l = num32(g(x, 'lo')), h = num32(g(x, 'hi')), to = num32(g(x, 'to'))
        if (l === null || h === null || to === null || !(l < h) || l < build.mainLo || h > build.mainHi) {
          c.refuse('format', '', 0, 'a copied block'); return null
        }
        lo = l; hi = h
      }
      if (state.sites + out.length >= build.pools.sites || (!copied && state.sdata + sdata + (hi - lo) > build.pools.sdata)) {
        c.refuse('limit', '', 0, 'sites'); return null
      }
      out.push([lo, hi, !copied])
      if (!copied) sdata += hi - lo
    }
  }
  const names = build.modules.map(m => m.name)
  for (const [lo, hi, isSite] of out) {
    if (!isSite) continue
    for (const [a, b, kind, mi] of build.siteMap) {
      if (overlap(lo, hi, a, b)) {
        const owner = (kind === desc.SITE_SITE || kind === desc.SITE_COPIED || kind === desc.SITE_CLAIM) && mi < names.length ? names[mi] : ''
        c.refuse('clash', siteDetail(lo, owner), SITE_CORE << 8); return null
      }
    }
    for (const [a, b] of build.prelink) if (overlap(lo, hi, a, b)) { c.refuse('clash', siteDetail(lo), SITE_PRELINK << 8); return null }
  }
  for (const [lo, hi, isSite] of out) {
    if (isSite) continue
    for (const [a, b, kind, mi] of build.siteMap) {
      if (kind === desc.SITE_SITE && overlap(lo, hi, a, b)) {
        c.refuse('clash', siteDetail(lo, mi < names.length ? names[mi] : ''), SITE_CORE << 8); return null
      }
    }
  }
  return out
}

/** Pass 1's part for the contributions' "claims" (F_CLAIMS), after the sites: drive._scan_claims. Nothing is written
 * for a claim. -> [lo, hi][], or null after refusing c. */
function scanClaims(build: Build, c: Choice, doc: Record<string, any>, state: State, nsites: number): [number, number][] | null {
  const out: [number, number][] = []
  if (!build.takeClaims) return out
  for (const x of (g(doc, 'contribute') ?? []) as Record<string, any>[]) {
    const v = g(x, 'claims')
    if (empty(v)) continue
    if (!Array.isArray(v)) { c.refuse('format', '', 0, "a contribution's claims"); return null }
    for (const r of v) {
      const [lo, hi] = Array.isArray(r) && r.length === 2 ? [num32(r[0]), num32(r[1])] : [null, null]
      if (lo === null || hi === null || lo < build.mainLo || hi > build.mainHi || !(lo < hi)) {
        c.refuse('format', '', 0, `a claim ${repr(r)}`); return null
      }
      if (state.sites + nsites + out.length >= build.pools.sites) { c.refuse('limit', '', 0, 'sites'); return null }
      out.push([lo, hi])
    }
  }
  const names = build.modules.map(m => m.name)
  for (const [lo, hi] of out) {
    for (const [a, b, kind, mi] of build.siteMap) {
      if (overlap(lo, hi, a, b)) {
        const owner = (kind === desc.SITE_SITE || kind === desc.SITE_COPIED || kind === desc.SITE_CLAIM) && mi < names.length ? names[mi] : ''
        c.refuse('clash', siteDetail(lo, owner), SITE_CORE << 8); return null
      }
    }
    for (const [a, b] of build.prelink) if (overlap(lo, hi, a, b)) { c.refuse('clash', siteDetail(lo), SITE_PRELINK << 8); return null }
  }
  return out
}

/** Do two overlapping ranges of different mods clash: a site (1) with anything, a claim (2) with a claim; a copied
 * block (0) with a copied block or a claim does not. drive._clashes. */
const clashes = (mine: number, theirs: number) => mine === 1 || theirs === 1 || (mine === 2 && theirs === 2)

/** A mod's ranges in dlink.c's order: its sites, its copied blocks, then its claims, with their kind. drive._ranges. */
const ranges = (c: Choice): [number, number, number][] =>
  [...c.driveSites.map(([lo, hi, s]) => [lo, hi, s ? 1 : 0] as [number, number, number]), ...c.driveClaims.map(([lo, hi]) => [lo, hi, 2] as [number, number, number])]

/** Pass 1, one file: what the boot linker's scan tells from the file and the build alone, in its order. drive.pass1. */
/** A mod's "collections", as the boot linker reads them (dlink.c collections()): each table's name and entry size
 * into tables; NAME and NAME_n are global names. -> null, or the refusal's [code, detail, arg, note]. drive._collections. */
function collections(v: unknown, tables: [string, number][], exports: string[], state: State, build: Build,
  keep: (s: string) => boolean): [string, string, number, string] | null {
  if (empty(v)) return null
  if (!isDict(v)) return ['format', '', 0, 'collections']
  for (const [name, d] of Object.entries(v)) {   // in the file's order, as the boot linker reads it
    if (!isDict(d) || !(ulen(name) > 0 && ulen(name) < NAME_N_MAX)) return ['format', '', 0, 'collections']
    let entry: number | null = null
    for (const [k, x] of Object.entries(d)) {
      if (k === 'entry') {
        if (!pyInt(x) || !((x as number) > 0 && (x as number) <= 0xffff) || (x as number) % 4) return ['format', '', 0, 'collections']
        entry = x as number
      } else if (k === 'space') return ['feature', 'collections', 0, '']   // a DSP table: never on the +Drive
    }
    if (entry === null) return ['format', '', 0, 'collections']
    if (state.colls + tables.length >= build.pools.colls || state.syms + exports.length + 2 * tables.length + 2 > build.pools.syms
        || !keep(name) || !keep(name + '_n')) return ['limit', '', 0, 'collections']
    tables.push([name, entry])
  }
  return null
}

/** The living mod that declares the +Drive table name: the first in id order. drive._declarer. */
const declarer = (order: Choice[], name: string) => order.find(o => o.alive && o.tables.some(([t]) => t === name))

/** The global names a mod defines: its exports, then each table's NAME and NAME_n. drive._names. */
const names = (c: Choice) => [...c.exports, ...c.tables.flatMap(([t]) => [t, t + '_n'])]

export function pass1(build: Build, c: Choice, state: State): boolean {
  const keep = (text: string) => {
    const n = ulen(text) + 1
    const arena = build.pools.arena
    if (state.arena >= arena || state.arena + n > arena) return false
    state.arena += n
    return true
  }
  const keepList = (v: unknown) => {
    if (v === null || v === undefined) return true
    if (!Array.isArray(v)) return false
    for (const x of v) {
      if (typeof x !== 'string' || !keep(x) || state.lists >= build.pools.lists) return false
      state.lists++
    }
    return true
  }
  let doc: unknown
  try {
    doc = loads(decodeUtf8(c.raw!))
  } catch (e) {
    if (e instanceof JSONDecodeError || e instanceof UnicodeDecodeError) return c.refuse('json', '', 0, e.message)
    throw e
  }
  if (!isDict(doc)) return c.refuse('json')
  if (!pyEq(g(doc, 'elemod'), FORMAT2) || !pyEq(formatOf(doc), FORMAT2)) return c.refuse('format', '', 0, 'not "elemod": 2')
  const ident = g(doc, 'id')
  if (typeof ident !== 'string' || !keep(ident) || !ident || ulen(ident) >= ID_MAX) return c.refuse('format', '', 0, 'its id')
  c.id = c.label = ident
  if (typeof g(doc, 'version') !== 'string' || !keep(doc.version)) return c.refuse('format', '', 0, 'its version')
  const tgt = g(doc, 'target')
  if (!isDict(tgt) || g(tgt, 'device') !== build.deviceKey || g(tgt, 'os') !== build.os)
    return c.refuse('target', '', 0, isDict(tgt) ? `${str(g(tgt, 'device'))} ${str(g(tgt, 'os'))}` : 'no target')
  for (const f of NO_FIELDS)
    if (!(f === 'sites' && build.telos) && !(build.mainHi && SITE_FIELDS.includes(f)) && !(f === 'collections' && build.collections)
        && !empty(g(doc, f))) return c.refuse('feature', f)
  const secs = g(doc, 'sections')
  if (!isDict(secs)) return c.refuse('format', '', 0, 'its sections')
  for (const s of Object.keys(secs)) {
    if (s === '.payload' && build.payloadSkip) {
      if (!skippedPayload(secs[s])) return c.refuse('feature', s)     // bytes the stage didn't skip
    } else if (s !== '.run' && s !== '.bss') return c.refuse('feature', s)
  }
  const res = g(doc, 'resources')
  if (res !== null && !isDict(res)) return c.refuse('format', '', 0, 'its resources')
  for (const [k, v] of Object.entries(res ?? {})) {
    if (k === 'regions' && !empty(v)) {
      if (!build.telos) return c.refuse('feature', 'regions')
      const rs: [number, number][] = []
      for (const r of each(v)) {
        const lo = isDict(r) && 'lo' in r ? pyInt0(pyStr(r.lo)) : null, hi = isDict(r) && 'hi' in r ? pyInt0(pyStr(r.hi)) : null
        if (lo === null || hi === null) return c.refuse('format', '', 0, 'resources.regions')
        rs.push([lo, hi])
      }
      if (rs.some(([lo, hi]) => hi <= lo)) return c.refuse('format', '', 0, 'resources.regions')
      for (const [lo, hi] of rs)                // inside a free area, as a flashed mod's (dl_region_ok)
        if (!build.areas.some(([a, z]) => a <= lo && hi <= z)) return c.refuse('outside', hex8(lo))
      c.regions = rs
    }
    if (k === 'names' && !keepList(v)) return c.refuse('limit', '', 0, 'resources.names')
    if (k === 'core' && v !== null && !(typeof v === 'string' && keep(v))) return c.refuse('format', '', 0, 'resources.core')
  }
  if (build.telos && !empty(g(doc, 'sites'))) {   // pass 1's checks of each site (dlink.c, scan_sites)
    for (const x of each(doc.sites)) {
      const ok = isDict(x) && ['addr', 'len', 'stock_sha256', 'new'].every(k => k in x)
      const a = ok ? pyInt0(pyStr(x.addr)) : null, n = ok ? pyIntCall(x.len) : null
      const want = ok ? fromhexLen(x.stock_sha256) : null, nw = ok ? fromhexLen(x.new) : null
      if (a === null || n === null || want === null || nw === null) return c.refuse('format', '', 0, 'a site')
      if (a <= 0 || !(n > 0 && n <= 256) || nw !== n || want !== 32) return c.refuse('format', '', 0, 'a site')
      // the stock hash needs the main OS, which a build here (its descriptor) has not: the unit checks it
      if (build.shadow.some(([sa, dt]) => a < sa + dt.length && sa < a + n)) return c.refuse('flashed', hex8(a))
      c.sites.push([a, a + n])
    }
  }
  for (const k of ['imports', 'weak', 'requires', 'conflicts']) if (!keepList(g(doc, k))) return c.refuse('limit', '', 0, k)
  if (build.payloadSkip && payloadRelocs(g(doc, 'relocs'))) return c.refuse('feature', 'relocs into .payload')
  const runLen = sectionLen(g(secs, '.run'))
  for (const r of each(truthy(g(doc, 'relocs')) ? doc.relocs : [])) {
    if (!(Array.isArray(r) && r.length === 5) || r[0] !== '.run' || typeof r[2] !== 'string' || !Object.hasOwn(build.rsize, r[2]) || !pyIsInt(r[1])
        || num(r[1]) < 0 || num(r[1]) + build.rsize[r[2]] > runLen)
      return c.refuse('format', '', 0, `a relocation ${repr(r)}`)
  }
  const events = g(doc, 'events')                // "std1": its "std" contributions are standard events
  if (build.stdMap && events !== undefined && events !== null && !(typeof events === 'string' && STD_SETS.includes(events)))
    return c.refuse('format', '', 0, `"events" ${repr(events)}`)
  let contribs = 0
  const late = build.stdMap || build.collections   // the A4's dlink.c finds the table after the keys
  for (const x of each(truthy(g(doc, 'contribute')) ? doc.contribute : [])) {
    if (!isDict(x)) return c.refuse('format', '', 0, 'a contribution')
    let to: unknown = null, n: number | null = null, order: unknown = 50, std = false
    for (const [k, v] of Object.entries(x)) {
      if (k === 'to') {
        to = v
        if (!late) {                            // without the std map (drive-loader-ar2): checked here
          const t = typeof v === 'string' ? build.table.get(v) : undefined
          if (t === undefined || !(build.tables[t].flags & desc.F_MORE) || build.tables[t].flags & desc.F_DSP) return c.refuse('table', v)
        }
      } else if (k === 'std' && build.stdMap) {
        if (v !== null && v !== true) return c.refuse('format', '', 0, `"std": ${repr(v)}`)
        std = v === true
      } else if (k === 'order') {
        if (!pyInt(v)) return c.refuse('format', '', 0, `an order of ${repr(v)}`)
        order = v
      } else if (k === 'data') {
        n = fromhexLen(v)
        if (n === null) return c.refuse('format', '', 0, 'an entry\'s data')
      } else if (k === 'claims' && !empty(v) && !build.takeClaims) {   // else read after the sites
        return c.refuse('feature', 'claims')
      } else if (k === 'relocs' && build.payloadSkip && payloadRelocs(v)) {
        return c.refuse('feature', 'relocs into .payload')
      }
    }
    if (std && (events === undefined || events === null))
      return c.refuse('format', '', 0, 'a standard event\'s subscription without "events"')
    if (std && typeof to === 'string') {        // the core's std map (the descriptor's CLAIM_STD)
      if (!build.std.has(to)) return c.refuse('std', to)
      to = build.std.get(to)!
    }
    if (to !== null && late) {                 // with it, once the subscription is known
      const t = typeof to === 'string' ? build.table.get(to) : undefined
      if (t === undefined && typeof to === 'string' && build.collections) {
        if (!keep(to)) return c.refuse('limit', '', 0, 'names')   // a +Drive table's: found in the check rounds
      } else if (t === undefined || !(build.tables[t].flags & desc.F_MORE) || build.tables[t].flags & desc.F_DSP) return c.refuse('table', to)
    }
    if (to === null || n === null) return c.refuse('format', '', 0, 'a contribution without "to" or "data"')
    const o = order as number
    if (!(o >= desc.ORDER_MIN && o < desc.LINKER_ORDER)) return c.refuse('format', '', 0, `an order of ${repr(order)}`)
    if (!n || (build.table.has(to as string) && n % build.tables[build.table.get(to as string)!].entry)) return c.refuse('table', to)
    if (state.contribs + contribs >= build.pools.contribs) return c.refuse('limit', '', 0, 'contributions')
    contribs++
  }
  const syms = truthy(g(doc, 'symbols')) ? doc.symbols : {}
  const exports: string[] = []
  for (const x of each(truthy(g(doc, 'exports')) ? doc.exports : [])) {
    if (typeof x !== 'string' || !isDict(syms) || !(x in syms) || !symbol(syms[x])) return c.refuse('format', '', 0, `exports ${str(x)}`)
    if (x === INIT) continue
    if (state.syms + exports.length >= build.pools.syms || !keep(x)) return c.refuse('limit', '', 0, 'symbols')
    exports.push(x)
  }
  const tables: [string, number][] = []
  if (build.collections) {
    const why = collections(g(doc, 'collections'), tables, exports, state, build, keep)
    if (why) return c.refuse(...why)
  }
  const sites = scanSites(build, c, doc, state)
  if (sites === null) return false
  const claims = scanClaims(build, c, doc, state, sites.length)
  if (claims === null) return false
  let own: Record<string, unknown> = build.collections ? { ...doc, collections: {} } : doc   // read here; Mod2 doesn't size them
  if ('.payload' in secs && build.payloadSkip) {   // its bytes stay on the +Drive: nothing places it
    own = { ...doc, sections: Object.fromEntries(Object.entries(secs).filter(([k]) => k !== '.payload')) }
    if (isDict(syms)) own.symbols = Object.fromEntries(Object.entries(syms).filter(([, v]) => !(Array.isArray(v) && v[0] === '.payload')))
  }
  let m: Mod2
  try {                                     // the host's own reading of the file, a last check
    m = new Mod2(own, c.name, c.raw)
  } catch (e) {
    if (e instanceof ModError) return c.refuse('format', '', 0, e.message)
    throw e
  }
  if (build.knobs) {                        // with +Drive knob ids: link.check's rule for __knobs
    for (const [, t, a] of m.knobRefs())
      if (!(t === 'abs32' || t === 'abs16') || !(a >= 0 && a < m.knobs)) return c.refuse('format', '', 0, 'a reference to __knobs')
  }
  c.knobFixed = build.knobs ? m.idRanges(new Uint8Array(0)) : []   // its .run's hex parts only, as dlink.c reads them
  for (const x of m.contribute) if (x.event !== undefined && build.stdMap) x.to = build.std.get(x.event)!   // the table a standard event maps to
  c.mod = m; c.exports = exports; c.driveSites = sites; c.driveClaims = claims; c.tables = tables
  state.syms += exports.length + 2 * tables.length
  state.colls += tables.length
  state.contribs += contribs
  state.sites += sites.length + claims.length
  state.sdata += sites.reduce((n, [lo, hi, isSite]) => n + (isSite ? hi - lo : 0), 0)
  c.alive = true
  return true
}

const hx = (v: number) => '0x' + v.toString(16)

/** link.idProblems for one +Drive mod against the flashed ids and the earlier living mods'. -> a refusal's detail,
 * or null. drive._knob_problem. */
function knobProblem(build: Build, c: Choice, earlier: Choice[]): string | null {
  const [own, [lo, hi], kinds] = build.paramIds!
  const ownOnly = new Set(kinds.filter(([, o]) => o).map(([t]) => t))
  for (const [table, a0, b] of c.knobFixed) {
    let a = a0
    if (a <= hi && lo <= b) return `${hx(a)}-${hx(b)} are core's`
    if (ownOnly.has(table) && a < own) return `${hx(a)}-${hx(b)} are below ${hx(own)}`
    if (b >= own) {
      a = Math.max(a, own)
      const taken: [number, number, string][] = [...build.knobIds, ...earlier.flatMap(o => o.knobFixed.map(([, x, y]) => [x, y, o.id!] as [number, number, string]))]
      for (const [x, y, nm] of taken) if (x <= b && a <= y) return `${hx(Math.max(a, x))}-${hx(Math.min(b, y))} are ${nm}'s`
    }
  }
  return null
}

/** Each living "knobs" mod's run (store.knobs in the device's +Drive range, past the flashed ids and the living
 * mods' own); a mod with no run left is refused. -> 1 when it refused one. drive._knob_runs (with no .store). */
function knobRuns(build: Build, order: Choice[]): number {
  if (!build.knobs) return 0
  const alive = order.filter(c => c.alive)
  const want = new Map(alive.filter(c => c.mod!.knobs).map(c => [c.id!, c.mod!.knobs] as [string, number]))
  const fixed: [number, number][] = [...build.knobIds.map(([a, b]) => [a, b] as [number, number]),
    ...alive.flatMap(c => c.knobFixed.map(([, a, b]) => [a, b] as [number, number]))]
  const [lo, hi] = build.knobRange!
  try {
    store.knobs({}, want, lo, fixed, hi)
  } catch (e) {
    const mod = (e as ModError & { mod?: string }).mod
    if (!(e instanceof ModError) || mod === undefined) throw e
    const c = alive.find(c => c.id === mod)!
    c.refuse('ids', `run of ${c.mod!.knobs}: none free`)
    return 1
  }
  return 0
}

function round(build: Build, order: Choice[]): number {
  let refused = 0
  const core = build.modules[0].version
  order.forEach((c, i) => {
    if (!c.alive) return
    const m = c.mod!
    const earlier = order.slice(0, i).filter(o => o.alive), alive = order.filter(o => o.alive)
    let why: [string, string] | null = null
    if (build.flashed.includes(c.id!) || earlier.some(o => o.id === c.id)) why = ['duplicate', c.id!]
    else if (m.needsCore && cmpVersions(core, m.needsCore) < 0) why = ['core', m.needsCore]
    if (!why) {
      const miss = m.requires.find(r => !build.flashed.includes(r) && !alive.some(o => o.id === r && o !== c))
      if (miss !== undefined) why = ['requires', miss]
    }
    if (!why) {
      let w = m.conflicts.find(x => build.flashed.includes(x))
      if (w === undefined) w = earlier.find(o => m.conflicts.includes(o.id!) || o.mod!.conflicts.includes(c.id!))?.id ?? undefined
      if (w !== undefined) why = ['conflicts', w]
    }
    if (!why && c.sites.length) {           // sites: the later of two that overlap goes
      const w = earlier.find(o => c.sites.some(([a, b]) => o.sites.some(([x, y]) => a < y && x < b)))
      if (w !== undefined) why = ['site', w.id!]
    }
    if (!why) {
      const x = names(c).find(x => build.globals.has(x) || earlier.some(o => names(o).includes(x)))
      if (x !== undefined) why = ['export', x]
    }
    if (!why) {
      const x = m.names.find(n => build.claims.includes(n) || earlier.some(o => o.mod!.names.includes(n)))
      if (x !== undefined) why = ['claim', x]
    }
    if (!why && build.knobs) {                // knob ids: link.idProblems' rules
      const x = knobProblem(build, c, earlier)
      if (x !== null) why = ['ids', x]
    }
    if (!why && c.regions.length) {         // regions: the flashed build's, then earlier mods'
      let w = c.regions.flatMap(([a, b]) => build.regions.filter(([x, y]) => a < y && x < b).map(r => r[2]))[0]
      if (w === undefined) w = earlier.find(o => c.regions.some(([a, b]) => o.regions.some(([x, y]) => a < y && x < b)))?.id ?? undefined
      if (w !== undefined) why = ['region', w]
    }
    if (!why) {
      const x = m.imports.find(x => !(build.globals.has(x) || alive.some(o => names(o).includes(x))) && !m.weak.has(x)
        && !(x === KNOBS && build.knobs && m.knobs))
      if (x !== undefined) why = ['import', x]
    }
    if (!why && build.collections) {          // +Drive tables: a living mod declares each, of that entry
      for (const x of m.contribute) {
        if (build.table.has(x.to)) continue
        const o = declarer(order, x.to)
        if (o === undefined || x.data.length % o.tables.find(([t]) => t === x.to)![1]) { why = ['table', x.to]; break }
      }
    }
    let arg = 0
    if (!why) {                               // sites against earlier living mods' sites and copied blocks
      for (const [lo, hi, mine] of ranges(c)) {   // sites and claims against earlier living mods' ranges (clashes)
        const o = earlier.find(o => ranges(o).some(([a, b, theirs]) => clashes(mine, theirs) && overlap(lo, hi, a, b)))
        if (o !== undefined) { why = ['clash', siteDetail(lo)]; arg = SITE_MOD << 8 | o.line; break }
      }
    }
    if (why) {
      c.refuse(why[0], why[1], arg)
      refused++
    }
  })
  for (const t of build.tables) {           // room, table by table: the last adder goes
    for (;;) {
      let total = t.count, last: Choice | null = null
      for (const c of order) {
        if (!c.alive) continue
        for (const x of c.mod!.contribute) if (x.to === t.name) { total += Math.floor(x.data.length / t.entry); last = c }
      }
      if (total <= t.capacity) break
      last!.refuse('full', t.name, t.capacity - t.count)
      refused++
    }
  }
  return refused
}

function layOut(mods: Mod2[], sec: string, at: number, base: Map<string, number>): number {
  for (const m of mods) {
    if (m.size(sec)) {
      at = align(at, Math.max(4, m.sections.get(sec)!.align))
      base.set(`${m.id}\0${sec}`, at)
      at += m.size(sec)
    }
  }
  return at
}

/** With the stage at the top: where pass 2 reads c's file. drive._stage_at. */
function stageAt(build: Build, c: Choice): number {
  const n = c.raw!.length
  return n > build.hi - build.lo ? build.lo : (build.hi - n) & ~3
}

/** The living mods in placement order: id order, or with the stage at the top, the longest file first, ties in id
 * order. drive.placement. */
export function placement(build: Build, order: Choice[]): Choice[] {
  const live = order.filter(c => c.alive)
  return build.stageTop ? live.map((c, i) => [c, i] as const).sort((a, b) => b[0].raw!.length - a[0].raw!.length || a[1] - b[1])
    .map(([c]) => c) : live
}

/** In placement order from the area's start: every .run, then every .bss, then the +Drive tables; with the stage at the top, each file above
 * its own .run's end. -> [base, end], or null after refusing the last living mod in id order for room. drive._layout. */
function layout(build: Build, order: Choice[]): [Map<string, number>, number] | null {
  const placed = placement(build, order)
  const base = new Map<string, number>()
  let at = build.lo, over = 0
  for (const c of placed) {
    at = layOut([c.mod!], '.run', at, base)
    if (build.stageTop) over = Math.max(over, at - stageAt(build, c))
  }
  let end = align(layOut(placed.map(c => c.mod!), '.bss', align(at, 4), base), 4)
  for (const c of order) {                  // the +Drive tables, in id order: living entries and a zero entry
    if (!c.alive) continue
    for (const [name, e] of c.tables) {
      let n = 0
      for (const o of order) if (o.alive) for (const x of o.mod!.contribute) if (x.to === name && !build.table.has(x.to)) n += Math.floor(x.data.length / e)
      base.set(`${c.id}\0#${name}`, end)
      end += (n + 1) * e
    }
  }
  over = Math.max(over, end - build.hi)
  if (over > 0) {
    const alive = order.filter(c => c.alive)
    alive[alive.length - 1].refuse('room', '', Math.floor((over + 1023) / 1024))
    return null
  }
  return [base, end]
}

export type Result = {
  build: Build; skip: string; choices: Choice[]; live: Choice[]
  base: Map<string, number>; end: number; inits: [string, number][]
}

/** Pass 1, the check rounds and the layout. `files`: [[file name, bytes, or null for a missing file]] in
 * /mods/active's order. drive.check. */
export function check(build: Build, files: [string, Uint8Array | null][]): Result {
  let choices = files.map(([n, raw], i) => new Choice(i, n, staged(build, raw)))
  const r: Result = { build, skip: 'none', choices, live: [], base: new Map(), end: build.lo, inits: [] }
  if (choices.length > MAX_MODS) {          // the loader reads none of them
    r.skip = 'count'
    r.choices = choices = choices.slice(0, MAX_MODS)
    for (const c of choices) c.refuse('count', '', 0, '', NONE)
    return r
  }
  const state: State = { syms: build.nsyms, contribs: 0, arena: 1, lists: 0, sites: 0, sdata: 0, colls: 0 }
  for (const c of choices) {
    if (c.raw === null) c.refuse('missing', '', 0, '', DROPPED)
    else if (build.stageTop && c.raw.length > build.hi - build.lo) c.refuse('too-big')
    else if (!build.stageTop && build.stagingSize && c.raw.length > build.stagingSize) c.refuse('too-big')
    else pass1(build, c, state)
  }
  // by id, stable: of one id, the earlier line first
  const order = choices.filter(c => c.alive).sort((a, b) => cmp(a.id!, b.id!))
  let lay: [Map<string, number>, number] | null = null
  for (let k = 0; k < 4 * MAX_MODS; k++) {
    for (;;) {
      while (round(build, order));
      if (!knobRuns(build, order)) break
    }
    lay = layout(build, order)
    if (lay !== null) break
  }
  const live = order.filter(c => c.alive)
  if (lay === null) {
    r.skip = 'unstable'
    for (const c of live) c.refuse('limit')
    return r
  }
  const [base, end] = lay
  r.live = live; r.base = base; r.end = live.length ? end : build.lo
  for (const c of live) {
    c.state = LOADED; c.code = 0
    const s = c.mod!.symbols.get(INIT)
    if (s && (s[0] === '.run' || s[0] === '.bss')) r.inits.push([c.id!, base.get(`${c.id}\0${s[0]}`)! + s[1]])
  }
  return r
}

/** `/mods/.status`'s text -> the most bytes one upload or read takes (the Rytm loader's `stage:` line, or the
 * A4 MKII loader's `room:` line, its sector store's largest free run), or null for a loader without the line.
 * drive.stage_of. */
export function stageOf(status: string): number | null {
  for (const ln of status.split('\n')) {
    const w = ln.split(/\s+/).filter(x => x)
    if (w.length >= 3 && (w[0] === 'stage:' || w[0] === 'room:') && /^[0-9]+$/.test(w[1]) && w[2] === 'bytes') return parseInt(w[1], 10)
  }
  return null
}

/** The most bytes one upload or read takes on the unit now: the .status `stage:` line, else the build's staging
 * buffer (only the floor with the stage at the top). drive.upload_room. */
export function uploadRoom(build: Build, status: string | null): number | null {
  const n = status !== null ? stageOf(status) : null
  return n !== null ? n : build.stagingSize || null
}

/** One line per chosen file: name, state, code, message. drive.status_text. */
export function statusText(r: Result): string {
  return r.choices.map(c => `${c.name} ${STATES[c.state]} ${c.code} ${c.state === LOADED ? 'loaded' : message(c.code, c.detail, c.arg)}\n`).join('')
    + (r.skip !== 'none' ? `skipped: ${r.skip}\n` : '')
}

/** The JSON summary check gives: drive.summary(r). */
export function summary(r: Result) {
  const b = r.build
  return {
    build: { crc: hex8(b.crc), device: b.deviceKey, os: b.os, core: `${b.modules[0].name} ${b.modules[0].version}`,
      area: [hex8(b.lo), hex8(b.hi)], staging_size: b.stagingSize },
    skip: r.skip,
    choices: r.choices.map(c => ({ line: c.line, file: c.name, crc: c.crc === null ? null : hex8(c.crc), id: c.id,
      state: STATES[c.state], code: c.code, reason: CODES[c.code][0], detail: c.detail, arg: c.arg,
      message: c.state === LOADED ? 'loaded' : c.why() })),
    loads: r.live.map(c => c.id),
    end: hex8(r.end), used: r.end - b.lo,
  }
}

/** elekloader check's lines: drive.print_result. */
export function resultLines(r: Result): string[] {
  const out = [`build: ${r.build.label()}`]
  if (r.skip !== 'none') out.push(`  nothing loads: ${r.skip}`)
  for (const c of r.choices) out.push(`  ${STATES[c.state].toUpperCase().padEnd(8)} ${c.name}${c.state === LOADED ? '' : ': ' + c.why()}`)
  out.push(`${r.live.length} of ${r.choices.length} load; the +Drive area is used to ${hex8(r.end)} (${r.end - r.build.lo} of ${r.build.hi - r.build.lo} bytes)`)
  return out
}

// ---- /mods/active ------------------------------------------------------------------------------------------------

/** `/mods/active` for [[file name, bytes]]: one line each, the name and the CRC-32 of the file as the build's loader
 * stages it (staged()). drive.active_text. */
export function activeText(entries: [string, Uint8Array][], build: Build | null = null): string {
  for (const [n] of entries) {
    if (!n || ulen(n) >= NAME_MAX || n !== n.trim() || n.startsWith('#') || /[\n\r]/.test(n))
      throw new ModError(`${repr(n)} cannot be a line of /mods/active`)
  }
  return entries.map(([n, raw]) => `${n} ${fileCrc(staged(build, raw)!).toString(16).padStart(8, '0')}\n`).join('')
}

/** `/mods/active` -> [[name, crc]], as the boot linker reads it; null past MAX_MODS lines. drive.parse_active. */
export function parseActive(text: string): [string, number][] | null {
  const out: [string, number][] = []
  for (let ln of text.split('\n')) {
    if (ln.endsWith('\r')) ln = ln.slice(0, -1)
    ln = ln.replace(/^[ \t]+|[ \t]+$/g, '')
    if (!ln || ln.startsWith('#')) continue
    const cut = Math.max(ln.lastIndexOf(' '), ln.lastIndexOf('\t'))
    if (cut < 0) continue
    const name = ln.slice(0, cut).replace(/[ \t]+$/, ''), h0 = ln.slice(cut + 1)
    if (ulen(name) >= NAME_MAX || [...h0].length > 10) continue
    const h = h0.slice(0, 2).toLowerCase() === '0x' ? h0.slice(2) : h0
    const m = /^[ \t\n\r\f\v]*([+-]?)(?:0[xX]_?)?([0-9a-fA-F]+(?:_[0-9a-fA-F]+)*)[ \t\n\r\f\v]*$/.exec(h)   // int(h, 16)
    if (!m) continue
    const v = parseInt(m[2].replace(/_/g, ''), 16) * (m[1] === '-' ? -1 : 1)
    if (v > 0xffffffff) continue
    if (out.length >= MAX_MODS) return null
    out.push([name, v])
  }
  return out
}

// ---- the catalogue -----------------------------------------------------------------------------------------------

/** Can this mod ever load from +Drive? -> null, or the first feature only a flashed build can carry (pass 1's
 * feature checks, which need no build; sites and copied blocks load on a build with a site map, tables a mod declares
 * on one with F_COLLECTIONS, but never a DSP table, contributions' claims on `build` when it takes them, F_CLAIMS).
 * For a page that marks each mod "+Drive" or "flash". */
export function flashOnly(doc: unknown, build?: Build): string | null {
  if (!isDict(doc) || !pyEq(g(doc, 'elemod'), FORMAT2)) return 'format 1'
  for (const f of NO_FIELDS) {
    if (SITE_FIELDS.includes(f) || empty(g(doc, f))) continue
    if (f === 'collections' && isDict(doc.collections) && Object.values(doc.collections).every(d => !isDict(d) || !('space' in d))) continue
    return f
  }
  if (isDict(doc.sections)) for (const s of Object.keys(doc.sections)) if (s !== '.run' && s !== '.bss') return s
  if (isDict(doc.resources) && !empty(g(doc.resources, 'regions'))) return 'regions'
  if (!build?.takeClaims && Array.isArray(doc.contribute) && doc.contribute.some(x => isDict(x) && !empty(g(x, 'claims')))) return 'claims'
  return null
}

