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
import { FORMAT2, Mod2 } from './link.ts'
import { JSONDecodeError, UnicodeDecodeError, decodeUtf8, loads } from './pyjson.ts'
import { repr, str, truthy } from './py.ts'
import { crc32 } from './zip.ts'

export const MAX_MODS = 256                // lines of /mods/active, and status records
export const ID_MAX = 28                   // a status record's id field (27 bytes and a NUL)
export const DETAIL = 32                   // a detail string (31 bytes and a NUL)
export const NAME_MAX = 128                // a path below /mods in /mods/active (127 bytes, folders included)
export const RECORD = 32
// the boot linker's own room (dl.h DL_POOL_*, dlink.c pools()): each pool per started MB of the +Drive area (at least
// 1), the symbols past the flashed build's own names, each capped at POOL_CAP; past one a mod is refused with LIMIT
export const POOL_PER_MB = { arena: 12288, lists: 1024, syms: 1024, contribs: 256 }
export const POOL_CAP = 0xfff0
export const INIT = 'drive_init'

export const NONE = 0, LOADED = 1, REFUSED = 2, DROPPED = 3
export const STATES: Record<number, string> = { 0: 'none', 1: 'loaded', 2: 'refused', 3: 'dropped' }
export const CODES: [string, string][] = [
  ['ok', 'loaded'],
  ['missing', 'the file is not in /mods'],
  ['changed', 'the file is not the one chosen (its CRC-32 differs)'],
  ['too-big', 'the file is larger than the staging buffer'],
  ['json', 'not a JSON object'],
  ['format', 'not a format-2 mod the loader can read'],
  ['target', 'made for another device or OS'],
  ['feature', 'uses {detail}, which a +Drive mod cannot'],
  ['duplicate', '{detail} is flashed already, or chosen twice'],
  ['core', 'needs core {detail} or newer'],
  ['requires', 'requires {detail}'],
  ['conflicts', 'conflicts with {detail}'],
  ['export', 'exports {detail}, which another mod exports'],
  ['claim', 'claims {detail}, which another mod claims'],
  ['import', 'imports {detail}, which nothing exports'],
  ['table', 'adds to the table {detail}, which takes no +Drive entries of that size'],
  ['full', 'the table {detail} is full ({arg} spare)'],
  ['room', 'the +Drive area is {arg} KB short'],
  ['range', 'a 16-bit PC-relative reference is out of range'],
  ['limit', 'past the loader\'s own limits'],
  ['count', 'more than 256 mods are chosen'],
  // TELOS on the Digitone mk1 (a build whose descriptor carries the stock shadow): sites and regions
  ['site', 'a site overlaps one of {detail}'],
  ['flashed', 'a site at {detail} overlaps what the flashed build writes'],
  ['', ''],                                 // 23: not a code
  ['stock', 'the bytes at {detail} are not stock\'s'],
  ['', ''],                                 // 25: not a code
  ['busy', 'no quiet moment to write the sites in'],
  ['region', 'a region overlaps one of {detail}'],
]
export const CODE: Record<string, number> = Object.fromEntries(CODES.flatMap(([n], i) => (n ? [[n, i]] : [])))
export const BOOT_ONLY = ['missing', 'changed', 'busy']   // the host has the files it is given; busy is the unit's moment
export const SKIPS = ['none', 'key', 'armed', 'drive', 'abi', 'unstable', 'count', 'memory']
export const NO_FIELDS = ['sites', 'collections', 'copied', 'storage', 'sharc']
const RSIZE: Record<string, number> = { abs32: 4, pc32: 4, pc16: 2 }

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
function pyIntOf(v: unknown): number | null {
  if (typeof v === 'boolean') return Number(v)
  if (typeof v === 'number') return Number.isFinite(v) ? Math.trunc(v) : null
  if (typeof v !== 'string' || !/^\s*[+-]?\d(_?\d)*\s*$/.test(v)) return null
  return parseInt(v.replace(/_/g, ''), 10)
}

/** A status's line, for a person. drive.message. */
export function message(code: number, detail = '', arg = 0): string {
  return CODES[code][1].replace('{detail}', detail).replace('{arg}', String(arg))
}

/** A file's CRC-32 in /mods/active: zlib's (the boot linker's). The container's footer hash is container.crc. */
export const fileCrc = (raw: Uint8Array): number => crc32(raw)

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
  shadow: [number, Uint8Array][]; telos: boolean; regions: [number, number, string][]
  ramEnd: number; lo: number; hi: number; staging: number; stagingSize: number
  globals: Map<string, Global>
  nsyms: number
  pools: Record<keyof typeof POOL_PER_MB, number>

  constructor(d: desc.Decoded) {
    this.d = d
    this.crc = d.crc; this.deviceKey = d.deviceKey; this.os = d.os
    this.modules = d.modules
    this.flashed = d.modules.map(m => m.name)
    this.tables = d.tables
    this.table = new Map(d.tables.map((t, i) => [t.name, i]))
    this.claims = d.claims.filter(([k]) => k === desc.CLAIM_NAME).map(([, , nm]) => nm)
    // TELOS (a descriptor with the stock shadow, ABI 1.2): sites and regions are taken, checked as the boot linker
    // checks them (DL_SITES, DL_REGIONS)
    this.shadow = d.shadow ?? []
    this.telos = this.shadow.length > 0
    this.regions = d.claims.filter(([k]) => k === desc.CLAIM_REGION).map(([, mi, nm]) => {
      const [lo, hi] = nm.split(' ')
      return [pyInt0(lo)!, pyInt0(hi)!, d.modules[mi].name]
    })
    ;[this.ramEnd, this.lo, this.hi, this.staging, this.stagingSize] = d.area
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
    this.pools = { arena: pool('arena'), lists: pool('lists'), syms: pool('syms'), contribs: pool('contribs') }
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
  sites: [number, number][] = []; regions: [number, number][] = []   // TELOS: [lo, hi) each
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

type State = { syms: number; contribs: number; arena: number; lists: number }

/** Pass 1, one file: what the boot linker's scan tells from the file and the build alone, in its order. drive.pass1. */
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
  for (const f of NO_FIELDS) if (!(f === 'sites' && build.telos) && !empty(g(doc, f))) return c.refuse('feature', f)
  const secs = g(doc, 'sections')
  if (!isDict(secs)) return c.refuse('format', '', 0, 'its sections')
  for (const s of Object.keys(secs)) if (s !== '.run' && s !== '.bss') return c.refuse('feature', s)
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
      c.regions = rs
    }
    if (k === 'names' && !keepList(v)) return c.refuse('limit', '', 0, 'resources.names')
    if (k === 'core' && v !== null && !(typeof v === 'string' && keep(v))) return c.refuse('format', '', 0, 'resources.core')
  }
  if (build.telos && !empty(g(doc, 'sites'))) {   // pass 1's checks of each site (dlink.c, scan_sites)
    for (const x of each(doc.sites)) {
      const ok = isDict(x) && ['addr', 'len', 'stock_sha256', 'new'].every(k => k in x)
      const a = ok ? pyInt0(pyStr(x.addr)) : null, n = ok ? pyIntOf(x.len) : null
      const want = ok ? fromhexLen(x.stock_sha256) : null, nw = ok ? fromhexLen(x.new) : null
      if (a === null || n === null || want === null || nw === null) return c.refuse('format', '', 0, 'a site')
      if (a <= 0 || !(n > 0 && n <= 256) || nw !== n || want !== 32) return c.refuse('format', '', 0, 'a site')
      // the stock hash needs the main OS, which a build here (its descriptor) has not: the unit checks it
      if (build.shadow.some(([sa, dt]) => a < sa + dt.length && sa < a + n)) return c.refuse('flashed', hex8(a))
      c.sites.push([a, a + n])
    }
  }
  for (const k of ['imports', 'weak', 'requires', 'conflicts']) if (!keepList(g(doc, k))) return c.refuse('limit', '', 0, k)
  const runLen = sectionLen(g(secs, '.run'))
  for (const r of each(truthy(g(doc, 'relocs')) ? doc.relocs : [])) {
    if (!(Array.isArray(r) && r.length === 5) || r[0] !== '.run' || !(r[2] in RSIZE) || !pyIsInt(r[1])
        || num(r[1]) < 0 || num(r[1]) + RSIZE[r[2]] > runLen)
      return c.refuse('format', '', 0, `a relocation ${repr(r)}`)
  }
  let contribs = 0
  for (const x of each(truthy(g(doc, 'contribute')) ? doc.contribute : [])) {
    if (!isDict(x)) return c.refuse('format', '', 0, 'a contribution')
    let to: string | null = null, n: number | null = null, order: unknown = 50
    for (const [k, v] of Object.entries(x)) {
      if (k === 'to') {
        const t = typeof v === 'string' ? build.table.get(v) : undefined
        if (t === undefined || !(build.tables[t].flags & desc.F_MORE) || build.tables[t].flags & desc.F_DSP) return c.refuse('table', v)
        to = v as string
      } else if (k === 'order') {
        if (!pyInt(v)) return c.refuse('format', '', 0, `an order of ${repr(v)}`)
        order = v
      } else if (k === 'data') {
        n = fromhexLen(v)
        if (n === null) return c.refuse('format', '', 0, 'an entry\'s data')
      } else if (k === 'claims' && !empty(v)) {
        return c.refuse('feature', 'claims')
      }
    }
    if (to === null || n === null) return c.refuse('format', '', 0, 'a contribution without "to" or "data"')
    const o = order as number
    if (!(o >= desc.ORDER_MIN && o < desc.LINKER_ORDER)) return c.refuse('format', '', 0, `an order of ${repr(order)}`)
    if (!n || n % build.tables[build.table.get(to)!].entry) return c.refuse('table', to)
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
  let m: Mod2
  try {                                     // the host's own reading of the file, a last check
    m = new Mod2(doc, c.name, c.raw)
  } catch (e) {
    if (e instanceof ModError) return c.refuse('format', '', 0, e.message)
    throw e
  }
  c.mod = m; c.exports = exports
  state.syms += exports.length
  state.contribs += contribs
  c.alive = true
  return true
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
      const x = c.exports.find(x => build.globals.has(x) || earlier.some(o => o.exports.includes(x)))
      if (x !== undefined) why = ['export', x]
    }
    if (!why) {
      const x = m.names.find(n => build.claims.includes(n) || earlier.some(o => o.mod!.names.includes(n)))
      if (x !== undefined) why = ['claim', x]
    }
    if (!why && c.regions.length) {         // regions: the flashed build's, then earlier mods'
      let w = c.regions.flatMap(([a, b]) => build.regions.filter(([x, y]) => a < y && x < b).map(r => r[2]))[0]
      if (w === undefined) w = earlier.find(o => c.regions.some(([a, b]) => o.regions.some(([x, y]) => a < y && x < b)))?.id ?? undefined
      if (w !== undefined) why = ['region', w]
    }
    if (!why) {
      const x = m.imports.find(x => !(build.globals.has(x) || alive.some(o => o.exports.includes(x))) && !m.weak.has(x))
      if (x !== undefined) why = ['import', x]
    }
    if (why) {
      c.refuse(why[0], why[1])
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

function layout(build: Build, order: Choice[]): [Map<string, number>, number] | null {
  const live = order.filter(c => c.alive).map(c => c.mod!)
  const base = new Map<string, number>()
  const at = layOut(live, '.run', build.lo, base)
  const end = align(layOut(live, '.bss', align(at, 4), base), 4)
  if (end > build.hi) {
    const alive = order.filter(c => c.alive)
    alive[alive.length - 1].refuse('room', '', Math.floor((end - build.hi + 1023) / 1024))
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
  let choices = files.map(([n, raw], i) => new Choice(i, n, raw))
  const r: Result = { build, skip: 'none', choices, live: [], base: new Map(), end: build.lo, inits: [] }
  if (choices.length > MAX_MODS) {          // the loader reads none of them
    r.skip = 'count'
    r.choices = choices = choices.slice(0, MAX_MODS)
    for (const c of choices) c.refuse('count', '', 0, '', NONE)
    return r
  }
  const state: State = { syms: build.nsyms, contribs: 0, arena: 1, lists: 0 }
  for (const c of choices) {
    if (c.raw === null) c.refuse('missing', '', 0, '', DROPPED)
    else if (build.stagingSize && c.raw.length > build.stagingSize) c.refuse('too-big')
    else pass1(build, c, state)
  }
  // by id, stable: of one id, the earlier line first
  const order = choices.filter(c => c.alive).sort((a, b) => cmp(a.id!, b.id!))
  let lay: [Map<string, number>, number] | null = null
  for (let k = 0; k < 4 * MAX_MODS; k++) {
    while (round(build, order));
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

/** `/mods/active` for [[file name, bytes]]: one line each, the name and its CRC-32. drive.active_text. */
export function activeText(entries: [string, Uint8Array][]): string {
  for (const [n] of entries) {
    if (!n || ulen(n) >= NAME_MAX || n !== n.trim() || n.startsWith('#') || /[\n\r]/.test(n))
      throw new ModError(`${repr(n)} cannot be a line of /mods/active`)
  }
  return entries.map(([n, raw]) => `${n} ${fileCrc(raw).toString(16).padStart(8, '0')}\n`).join('')
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
 * feature checks, which need no build). For a page that marks each mod "+Drive" or "flash". */
export function flashOnly(doc: unknown): string | null {
  if (!isDict(doc) || !pyEq(g(doc, 'elemod'), FORMAT2)) return 'format 1'
  for (const f of NO_FIELDS) if (!empty(g(doc, f))) return f
  if (isDict(doc.sections)) for (const s of Object.keys(doc.sections)) if (s !== '.run' && s !== '.bss') return s
  if (isDict(doc.resources) && !empty(g(doc.resources, 'regions'))) return 'regions'
  if (Array.isArray(doc.contribute) && doc.contribute.some(x => isDict(x) && !empty(g(x, 'claims')))) return 'claims'
  return null
}

