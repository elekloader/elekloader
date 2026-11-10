// SPDX-License-Identifier: GPL-3.0-or-later
// +Drive storage: the boot allocator's twin and /mods/.store (docs/DRIVE.md, "+Drive storage"). Ported from
// elekloader's drive_store.py (GPL-2.0-or-later): the same placements, the same .store bytes, the same notes.
//
// The Drive Loader places a +Drive mod's storage fields in the bits each scope of the flashed build leaves free
// (from the room's top down, past the build's used bits), keeps them in /mods/.store, parks a removed mod's fields
// with their bits and values, repacks a scope under a new generation when its never-used bits run out or the flashed
// build changes under it, gives lock codes from the top of the range down past the build's own and CCs from the
// pool's end. drive_store.py's docstring has the rules and the file's layout.
import { sha256 } from './bytes.ts'
import { ModError } from './elemod.ts'
import * as store from './store.ts'
import type { Build } from './drive.ts'
import { crc32 } from './zip.ts'

export const MAGIC = 'DLST'
export const FORMAT = 1
const HEAD = 0x28
const SCOPE_REC = 8
const MAX_IDS = 15                              // the last 15 live IDs before this one
const MAX_ANCESTORS = 15
const MAX_INITS = 63
const MAX_BITS = 32
const ID_HASH = 'ELKDRIVE'                      // the layout ID's hash starts with these bytes
const NAME_MAX = 255                            // a mod id or a symbol, in bytes
const SCOPES = store.SCOPES as readonly string[]
const SCOPE_NUM: Record<string, number> = Object.fromEntries(SCOPES.map((s, i) => [s, i + 1]))
export const TABLES = [store.CODES_TABLE, store.CCS_TABLE, store.INITS_TABLE, store.HANDLES_TABLE]

const utf8 = new TextEncoder()
const utf8d = new TextDecoder('utf-8', { fatal: true })
const ub = (s: string) => utf8.encode(s)
function cmpBytes(a: Uint8Array, b: Uint8Array): number {
  const n = Math.min(a.length, b.length)
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return a[i] - b[i]
  return a.length - b.length
}
const cmpStr = (a: string, b: string) => cmpBytes(ub(a), ub(b))
const cmpKey = (a: [string, string], b: [string, string]) => cmpStr(a[0], b[0]) || cmpStr(a[1], b[1])
const K = (mod: string, sym: string) => mod + '\0' + sym
const unK = (k: string): [string, string] => k.split('\0') as [string, string]
const hex8 = (v: number) => '0x' + (v >>> 0).toString(16).padStart(8, '0')
const be = (v: number) => { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, v >>> 0); return b }
const toHex = (b: Uint8Array) => Array.from(b, x => x.toString(16).padStart(2, '0')).join('')
const bump = (g: number) => (g >= 255 ? 1 : g + 1)

export type Field = { mod: string; sym: string; scope: string; offset: number; bits: number; init?: number }
type NewField = { mod: string; sym: string; scope: string; offset: number | null; bits: number; init?: number }
export type Code = { mod: string; sym: string; code: number }
export type Cc = { mod: string; sym: string; cc: number }
export type ScopeState = { fgen: number; gen: number; low: number }
export type State = {
  flashed_id: number; id: number; ids: number[]; scopes: Record<string, ScopeState>; next_code: number
  fields: Field[]; parked: Field[]; codes: Code[]; freed_codes: Code[]; ccs: Cc[]; freed_ccs: Cc[]
}

// ---- what the flashed build gives ----------------------------------------------------------------------------

/** The flashed build, as the boot allocator sees it. drive_store.Base. */
export class Base {
  device: string
  scopes: Record<string, [number, number, number]>
  flashedId: number | null
  ancestors: number[]
  codes: [number, number][] | null
  ccs: [number, number][] | null
  inits: number
  ncodes: number
  room: Record<string, number | null>
  firstCode: number
  lastCode: number
  pool: number[]
  fxTaken: number[]

  constructor(device: string, scopes: Record<string, [number, number, number]>, flashedId: number | null,
    ancestors: number[] = [], codes: [number, number][] | null = [], ccs: [number, number][] | null = [], inits = 0,
    room: Record<string, number | null> | null = null, ncodes = 0) {
    this.device = device
    this.scopes = Object.fromEntries(SCOPES.map(s => [s, [...scopes[s]] as [number, number, number]]))
    this.flashedId = flashedId
    this.ancestors = [...ancestors]
    this.codes = codes === null ? null : codes.map(c => [c[0], c[1]] as [number, number])
    this.ccs = ccs === null ? null : ccs.map(c => [c[0], c[1]] as [number, number])
    this.inits = inits
    this.ncodes = this.codes !== null ? this.codes.length : ncodes
    this.room = room !== null ? { ...room } : Object.fromEntries(TABLES.map(t => [t, 256]))
    ;[this.firstCode, this.lastCode] = store.codeRange(device)
    ;[this.pool, this.fxTaken] = store.ccPool(device)
    if (store.kinds(device).length)
      throw new ModError(`+Drive storage keeps one range of lock codes; the ${device} numbers them per track kind`)
  }

  codeSet(): Set<number> { return new Set((this.codes ?? []).map(([, c]) => c & 0xff)) }
  ccSet(): Set<number> { return new Set((this.ccs ?? []).map(([, c]) => c)) }

  /** Does the build place the core's LFO destination field for this scope? */
  lfoDst(scope: string): boolean {
    const n = SCOPE_NUM[scope]
    return (this.codes ?? []).some(([d, c]) => (c & store.LFO_FLAG) !== 0 && d >>> 28 === n)
  }

  /** A drive.Build (the descriptor alone, on this side) -> Base: its codes, CCs and layout ID are known only
   *  where the tables are empty. drive_store.Base.from_build. */
  static fromBuild(build: Build): Base {
    const d = build.d
    const sc: Record<string, [number, number, number]> = {}
    for (const [n, r, u, g] of d.storage.scopes) sc[n] = [r, u, g]
    if (Object.keys(sc).length !== SCOPES.length || !SCOPES.every(s => s in sc))
      throw new ModError('the build keeps no mod storage: its core has no __store_gen')
    const tab = (name: string) => { const i = build.table.get(name); return i === undefined ? undefined : build.tables[i] }
    const entries = (name: string): null | [] => { const t = tab(name); return t === undefined ? null : t.count === 0 ? [] : null }
    const room: Record<string, number | null> = {}
    for (const name of TABLES) { const t = tab(name); room[name] = t === undefined ? null : t.capacity - t.count }
    const inits = tab(store.INITS_TABLE)?.count ?? 0
    const ct = tab(store.CODES_TABLE)
    const codes = entries(store.CODES_TABLE), ccs = entries(store.CCS_TABLE)
    return new Base(build.deviceKey, sc, null, [], codes === null && ct === undefined ? [] : codes,
      ccs === null && tab(store.CCS_TABLE) === undefined ? [] : ccs, inits, room, ct?.count ?? 0)
  }
}

// ---- the chosen mods -----------------------------------------------------------------------------------------

/** The storage the chosen +Drive mods declare, in id order (UTF-8 bytes). drive_store.Want. */
export class Want {
  mods: string[] = []
  decls: [string, string, string, number][] = []
  locks = new Set<string>(); ccs = new Set<string>(); lfos = new Set<string>(); rough = new Set<string>()
  routes = new Set<string>(); handles = new Set<string>()
  inits = new Map<string, number>()

  constructor(mods: Record<string, any>[] = []) {
    const sorted = [...mods].sort((a, b) => cmpStr(a.id, b.id))
    this.mods = sorted.map(m => m.id)
    for (const m of sorted) {
      const mid = m.id as string
      for (const [sym, scope, bits] of store.parseDecls(m)) this.decls.push([mid, sym, scope, bits])
      for (const s of store.parseLocks(m)) this.locks.add(K(mid, s))
      for (const s of store.parseCcs(m)) this.ccs.add(K(mid, s))
      for (const s of store.parseLfos(m)) this.lfos.add(K(mid, s))
      for (const s of store.parseRough(m)) this.rough.add(K(mid, s))
      for (const s of store.parseRoutes(m)) this.routes.add(K(mid, s))
      for (const s of store.parseHandles(m)) this.handles.add(K(mid, s))
      for (const [s, v] of store.parseInits(m)) this.inits.set(K(mid, s), v)
      for (const s of store.parseCvs(m)) throw new ModError(`${mid}: storage field '${s}': +Drive storage has no CV track`)
    }
  }

  /** The same declarations, of the given mods only. */
  of(mods: string[]): Want {
    const w = new Want(), keep = new Set(mods)
    const mine = (k: string) => keep.has(unK(k)[0])
    w.mods = this.mods.filter(m => keep.has(m))
    w.decls = this.decls.filter(d => keep.has(d[0]))
    for (const name of ['locks', 'ccs', 'lfos', 'rough', 'routes', 'handles'] as const)
      w[name] = new Set([...this[name]].filter(mine))
    w.inits = new Map([...this.inits].filter(([k]) => mine(k)))
    return w
  }
}

// ---- the room ------------------------------------------------------------------------------------------------

function needs(base: Base, w: Want): [string, number, number, (k: string) => boolean][] {
  const out: [string, number, number, (k: string) => boolean][] = []
  for (const s of SCOPES) {
    const [room, used] = base.scopes[s]
    const mine = new Set(w.decls.filter(d => d[2] === s).map(d => K(d[0], d[1])))
    out.push([s, w.decls.filter(d => d[2] === s).reduce((n, d) => n + d[3], 0), room - used, k => mine.has(k)])
  }
  out.push(['codes', w.locks.size, base.lastCode - base.firstCode + 1 - base.ncodes, k => w.locks.has(k)])
  for (const [t, n, uses] of [[store.CODES_TABLE, w.locks.size, (k: string) => w.locks.has(k)],
    [store.INITS_TABLE, w.inits.size, (k: string) => w.inits.has(k)],
    [store.HANDLES_TABLE, w.handles.size, (k: string) => w.handles.has(k)]] as const) {
    const r = base.room[t]
    const have = r === null || r === undefined ? 0 : t === store.INITS_TABLE ? Math.min(r, MAX_INITS - base.inits) : r
    out.push([t, n, have, uses])
  }
  return out
}

/** Which mods the room refuses: the last in id order that needs what is short, again until the set fits.
 *  -> [mod id, what, short by][]. drive_store.fit. */
export function fit(base: Base, want: Want): [string, string, number][] {
  const alive = [...want.mods], out: [string, string, number][] = []
  for (;;) {
    const w = want.of(alive)
    const row = needs(base, w).find(([, n, have]) => n > have)
    if (row === undefined) return out
    const [what, n, have, uses] = row
    const users = new Set(w.decls.filter(d => uses(K(d[0], d[1]))).map(d => d[0]))
    const last = alive.filter(m => users.has(m)).at(-1)!
    alive.splice(alive.indexOf(last), 1)
    out.push([last, what, n - have])
  }
}

// ---- the state: /mods/.store ---------------------------------------------------------------------------------

export function fresh(base: Base): State {
  return {
    flashed_id: base.flashedId ?? 0, id: base.flashedId ?? 0, ids: [],
    scopes: Object.fromEntries(SCOPES.map(s => [s, { fgen: base.scopes[s][2], gen: base.scopes[s][2], low: base.scopes[s][0] }])),
    next_code: base.lastCode, fields: [], parked: [], codes: [], freed_codes: [], ccs: [], freed_ccs: [],
  }
}

function names(mod: string, sym: string): [Uint8Array, Uint8Array] {
  const m = ub(mod), s = ub(sym)
  if (!m.length || !s.length || m.length > NAME_MAX || s.length > NAME_MAX) throw new ModError(`.store: a name of 1 to ${NAME_MAX} bytes`)
  return [m, s]
}

class Out {
  b: number[] = []
  u8(v: number) { this.b.push(v & 0xff) }
  u16(v: number) { this.b.push((v >>> 8) & 0xff, v & 0xff) }
  u32(v: number) { this.b.push((v >>> 24) & 0xff, (v >>> 16) & 0xff, (v >>> 8) & 0xff, v & 0xff) }
  bytes(d: Uint8Array) { for (const x of d) this.b.push(x) }
  pad() { while (this.b.length % 4) this.b.push(0) }
}

/** The state -> /mods/.store's bytes. drive_store.encode. */
export function encode(st: State): Uint8Array {
  const o = new Out()
  for (const c of [...MAGIC]) o.u8(c.charCodeAt(0))
  o.u16(FORMAT); o.u16(0); o.u32(0); o.u32(0)
  o.u32(st.flashed_id); o.u32(st.id); o.u8(st.next_code); o.u8(st.ids.length)
  for (const n of [st.fields.length, st.parked.length, st.codes.length, st.freed_codes.length, st.ccs.length, st.freed_ccs.length, 0]) o.u16(n)
  for (const s of SCOPES) { const sc = st.scopes[s]; o.u8(sc.fgen); o.u8(sc.gen); o.u16(0); o.u32(sc.low) }
  for (const i of st.ids) o.u32(i)
  for (const f of [...st.fields, ...st.parked]) {
    const [m, s] = names(f.mod, f.sym)
    o.u8(SCOPE_NUM[f.scope]); o.u8(f.bits); o.u16(f.offset); o.u32(f.init ?? 0); o.u8(m.length); o.u8(s.length); o.u16(0)
    o.bytes(m); o.bytes(s); o.pad()
  }
  for (const [list, v] of [[st.codes, 'code'], [st.freed_codes, 'code'], [st.ccs, 'cc'], [st.freed_ccs, 'cc']] as const)
    for (const c of list as any[]) {
      const [m, s] = names(c.mod, c.sym)
      o.u8(c[v]); o.u8(m.length); o.u8(s.length); o.u8(0); o.bytes(m); o.bytes(s); o.pad()
    }
  const raw = new Uint8Array(o.b)
  const v = new DataView(raw.buffer)
  v.setUint32(8, raw.length)
  v.setUint32(0x0c, crc32(raw))
  return raw
}

/** /mods/.store's bytes -> the state, or null for a file the allocator does not take. drive_store.decode. */
export function decode(raw: Uint8Array | null): State | null {
  if (raw === null) return null
  try {
    if (raw.length < HEAD + SCOPE_REC * 6) return null
    const v = new DataView(raw.buffer, raw.byteOffset, raw.length)
    if (String.fromCharCode(...raw.subarray(0, 4)) !== MAGIC || v.getUint16(4) !== FORMAT || v.getUint32(8) !== raw.length) return null
    const z = new Uint8Array(raw); z.fill(0, 0x0c, 0x10)
    if (crc32(z) !== v.getUint32(0x0c)) return null
    const nids = raw[0x19]
    const [nf, npk, ncd, nfc, ncc, nfcc] = [0, 1, 2, 3, 4, 5].map(i => v.getUint16(0x1a + 2 * i))
    const st: State = { flashed_id: v.getUint32(0x10), id: v.getUint32(0x14), next_code: raw[0x18], ids: [], scopes: {},
      fields: [], parked: [], codes: [], freed_codes: [], ccs: [], freed_ccs: [] }
    let at = HEAD
    for (const s of SCOPES) { st.scopes[s] = { fgen: raw[at], gen: raw[at + 1], low: v.getUint32(at + 4) }; at += SCOPE_REC }
    if (at + 4 * nids > raw.length) return null
    for (let i = 0; i < nids; i++) { st.ids.push(v.getUint32(at)); at += 4 }
    const nm = (a: number, lm: number, ls: number): [string, string, number] => {
      if (!lm || !ls || a + lm + ls > raw.length) throw new RangeError()
      return [utf8d.decode(raw.subarray(a, a + lm)), utf8d.decode(raw.subarray(a + lm, a + lm + ls)), a + Math.ceil((lm + ls) / 4) * 4]
    }
    for (const [list, n] of [[st.fields, nf], [st.parked, npk]] as const) {
      for (let i = 0; i < n; i++) {
        if (at + 12 > raw.length) return null
        const sn = raw[at], bits = raw[at + 1], off = v.getUint16(at + 2), init = v.getUint32(at + 4)
        const [mod, sym, next] = nm(at + 12, raw[at + 8], raw[at + 9])
        at = next
        if (!(sn >= 1 && sn <= SCOPES.length) || !(bits >= 1 && bits <= MAX_BITS) || init >= 2 ** bits) return null
        const f: Field = { mod, sym, scope: SCOPES[sn - 1], offset: off, bits }
        if (init) f.init = init
        list.push(f)
      }
    }
    for (const [list, key, n] of [[st.codes, 'code', ncd], [st.freed_codes, 'code', nfc], [st.ccs, 'cc', ncc], [st.freed_ccs, 'cc', nfcc]] as const) {
      for (let i = 0; i < n; i++) {
        if (at + 4 > raw.length) return null
        const c = raw[at]
        const [mod, sym, next] = nm(at + 4, raw[at + 1], raw[at + 2])
        at = next;
        (list as any[]).push({ mod, sym, [key]: c })
      }
    }
    if (at !== raw.length) return null
    return st
  } catch (e) {
    if (e instanceof RangeError || e instanceof TypeError) return null
    throw e
  }
}

const copy = (st: State): State => JSON.parse(JSON.stringify(st))

/** FORGET DATA: drop a parked mod's fields, codes and CCs. -> [the new state, whether it changed]. drive_store.forget. */
export function forget(st: State, mod: string): [State, boolean] {
  const out = copy(st)
  out.parked = st.parked.filter(f => f.mod !== mod).map(f => ({ ...f }))
  out.freed_codes = st.freed_codes.filter(c => c.mod !== mod).map(c => ({ ...c }))
  out.freed_ccs = st.freed_ccs.filter(c => c.mod !== mod).map(c => ({ ...c }))
  const changed = out.parked.length !== st.parked.length || out.freed_codes.length !== st.freed_codes.length
    || out.freed_ccs.length !== st.freed_ccs.length
  return [out, changed]
}

// ---- the layout ID -------------------------------------------------------------------------------------------

/** The live layout ID. drive_store.layout_id. */
export function layoutId(st: State, base: Base): number {
  const empty = !(st.fields.length || st.parked.length || st.codes.length || st.freed_codes.length)
  if (empty && SCOPES.every(s => st.scopes[s].gen === base.scopes[s][2])) return base.flashedId!
  const o = new Out()
  o.bytes(ub(ID_HASH)); o.u32(base.flashedId!)
  for (const s of SCOPES) o.u8(st.scopes[s].gen)
  const fs = [...st.fields, ...st.parked].sort((a, b) => SCOPE_NUM[a.scope] - SCOPE_NUM[b.scope] || a.offset - b.offset)
  for (const f of fs) {
    const [m, s] = names(f.mod, f.sym)
    o.u8(SCOPE_NUM[f.scope]); o.u8(f.bits); o.u16(f.offset); o.u32(f.init ?? 0)
    o.u8(m.length); o.bytes(m); o.u8(s.length); o.bytes(s)
  }
  for (const c of [...st.codes, ...st.freed_codes].sort((a, b) => a.code - b.code)) {
    const [m, s] = names(c.mod, c.sym)
    o.u8(c.code); o.u8(m.length); o.bytes(m); o.u8(s.length); o.bytes(s)
  }
  const h = sha256(new Uint8Array(o.b))
  return (new DataView(h.buffer, h.byteOffset).getUint32(0)) || 1
}

// ---- the allocator -------------------------------------------------------------------------------------------

/** allocate()'s result. drive_store.Placed. */
export class Placed {
  state: State; notes: string[]; id: number; ancestors: number[]; generations: Record<string, number>
  descriptors = new Map<string, number>()
  parked: [string, number][] = []
  initEntries: Uint8Array[]; handleEntries: Uint8Array[]; codeEntries: Uint8Array[] = []; ccEntries: Uint8Array[]
  bytes: Uint8Array

  constructor(base: Base, st: State, notes: string[], want: Want) {
    this.state = st; this.notes = notes; this.id = st.id
    const anc: number[] = [], seen = new Set([this.id])
    for (const a of [...st.ids, base.flashedId!, ...base.ancestors]) if (a && !seen.has(a)) { seen.add(a); anc.push(a) }
    this.ancestors = anc.slice(0, MAX_ANCESTORS)
    this.generations = Object.fromEntries(SCOPES.map(s => [s, st.scopes[s].gen]))
    const inited = st.fields.filter(f => f.init)
    const slot = new Map(inited.map((f, i) => [K(f.mod, f.sym), base.inits + i + 1]))
    const dsc = (f: Field, live = true) => ((SCOPE_NUM[f.scope] << 28) | (f.bits << 22)
      | ((live ? slot.get(K(f.mod, f.sym)) ?? 0 : 0) << 16) | f.offset) >>> 0
    for (const f of st.fields) this.descriptors.set(K(f.mod, f.sym), dsc(f))
    this.parked = st.parked.map(f => [K(f.mod, f.sym), dsc(f, false)])
    this.initEntries = inited.map(f => be(f.init!))
    this.handleEntries = st.fields.filter(f => want.handles.has(K(f.mod, f.sym))).map(f => be(this.descriptors.get(K(f.mod, f.sym))!))
    const scopeOf = new Map(st.fields.map(f => [K(f.mod, f.sym), f.scope]))
    for (const c of st.codes) {
      const k = K(c.mod, c.sym)
      const flags = (want.lfos.has(k) && base.lfoDst(scopeOf.get(k)!) ? store.LFO_FLAG : 0)
        | (want.rough.has(k) ? store.SMOOTH_OFF : 0) | (want.routes.has(k) ? store.ROUTE_FLAG : 0)
      this.codeEntries.push(new Uint8Array([...be(this.descriptors.get(k)!), ...be(c.code | flags)]))
    }
    this.ccEntries = st.ccs.map(c => new Uint8Array([...be(this.descriptors.get(K(c.mod, c.sym))!), ...be(c.cc)]))
    this.bytes = encode(st)
  }

  /** A JSON-ready summary, as drive_store.Placed.summary. */
  summary() {
    const fields: Record<string, string> = {}
    for (const k of [...this.descriptors.keys()].sort((a, b) => cmpKey(unK(a), unK(b))))
      fields[unK(k).join('/')] = hex8(this.descriptors.get(k)!)
    return {
      id: hex8(this.id), ancestors: this.ancestors.map(hex8), generations: this.generations, fields,
      parked: this.parked.map(([k, d]) => `${unK(k).join('/')} ${hex8(d)}`),
      codes: this.codeEntries.map(toHex), ccs: this.ccEntries.map(toHex), inits: this.initEntries.map(toHex),
      handles: this.handleEntries.map(toHex), notes: this.notes, store: toHex(this.bytes),
    }
  }
}

function heldOk(held: Field[], low: number, room: number, used: number): boolean {
  const spans = held.map(f => [f.offset, f.offset + f.bits]).sort((a, b) => a[0] - b[0] || a[1] - b[1])
  return spans.every(([a, b]) => used <= a && b <= room && low <= a) && spans.every((s, i) => i === 0 || s[0] >= spans[i - 1][1])
}

const sameDecl = (f: { scope: string; bits: number; init?: number }, w: [string, number, number] | undefined) =>
  w !== undefined && w[0] === f.scope && w[1] === f.bits && w[2] === (f.init ?? 0)

/** Place the +Drive storage of the mods in want against the flashed build, from the state prev (null: a fresh
 *  placement). -> Placed. drive_store.allocate. */
export function allocate(base: Base, prev: State | null, want: Want): Placed {
  if (base.codes === null || base.ccs === null || base.flashedId === null)
    throw new ModError('placing +Drive storage needs the build\'s tables: give a built .syx or main OS image, not the descriptor alone')
  const short = fit(base, want)
  if (short.length) throw new ModError(`${short[0][0]} does not fit: ${short[0][1]} is ${short[0][2]} short`)
  const notes: string[] = []
  const st = prev === null ? fresh(base) : decode(encode(prev))
  if (st === null) throw new ModError('.store: a bad state')
  const oldId = st.id
  const wantF = new Map<string, [string, number, number]>()
  for (const [mid, sym, scope, bits] of want.decls) {
    if ([...wantF.keys()].some(k => unK(k)[1] === sym)) throw new ModError(`the storage field ${sym} is declared by two mods`)
    wantF.set(K(mid, sym), [scope, bits, want.inits.get(K(mid, sym)) ?? 0])
  }
  const genTo = new Map<string, number>(), why = new Map<string, string>()
  for (const s of SCOPES) {
    const [room, used, gen] = base.scopes[s]
    const sc = st.scopes[s]
    const held = [...st.fields, ...st.parked].filter(f => f.scope === s)
    if (sc.fgen !== gen) {
      genTo.set(s, sc.gen === sc.fgen ? gen : bump(sc.gen))
      why.set(s, 'the flashed build repacked it')
    } else if (!heldOk(held, sc.low, room, used) || !(used <= sc.low && sc.low <= room)) {
      genTo.set(s, bump(sc.gen))
      why.set(s, 'the flashed build took its bits')
    }
    sc.fgen = gen
  }
  const kept: Field[] = [], gone: Field[] = []
  for (const f of st.fields) (sameDecl(f, wantF.get(K(f.mod, f.sym))) ? kept : gone).push(f)
  const have = new Set(kept.map(f => K(f.mod, f.sym)))
  const added: NewField[] = []
  for (const k of [...wantF.keys()].filter(k => !have.has(k)).sort((a, b) => cmpKey(unK(a), unK(b)))) {
    const [mid, sym] = unK(k), [scope, bits, init] = wantF.get(k)!
    const f: NewField = { mod: mid, sym, scope, offset: null, bits }
    if (init) f.init = init
    added.push(f)
  }
  let back = new Set<string>()
  for (const f of added) {
    const same = st.parked.filter(g => g.mod === f.mod && g.sym === f.sym && g.scope === f.scope && g.bits === f.bits
      && (g.init ?? 0) === (f.init ?? 0))
    if (same.length && !genTo.has(f.scope)) {
      const g = same.at(-1)!
      f.offset = g.offset
      st.parked.splice(st.parked.indexOf(g), 1)
      back.add(K(f.mod, f.sym))
    }
  }
  for (const f of gone) {
    st.parked = st.parked.filter(g => !(g.mod === f.mod && g.sym === f.sym))
    st.parked.push({ ...f })
  }
  const fields = [...kept, ...added] as Field[]
  const repacked = new Set<string>()
  const repack = (s: string, gen: number) => {
    const room = base.scopes[s][0], sc = st.scopes[s]
    sc.gen = gen
    st.parked = st.parked.filter(f => f.scope !== s)
    let at = room
    for (const f of fields.filter(f => f.scope === s).sort((a, b) => cmpKey([a.mod, a.sym], [b.mod, b.sym]))) {
      at -= f.bits
      f.offset = at
    }
    sc.low = at
    repacked.add(s)
    if (at < room || gen !== base.scopes[s][2])
      notes.push(`+Drive ${s} storage repacked (generation ${gen}), as ${why.get(s)}: projects and sounds saved before load it at defaults`)
  }
  for (const s of SCOPES) {
    const [room, used] = base.scopes[s]
    const sc = st.scopes[s]
    if (fields.filter(f => f.scope === s).reduce((n, f) => n + f.bits, 0) > room - used)
      throw new ModError(`the +Drive mods need more ${s} storage than the build leaves`)
    if (genTo.has(s)) { repack(s, genTo.get(s)!); continue }
    const pend = added.filter(f => f.scope === s && f.offset === null)
    if (!pend.length) continue
    if (sc.low - pend.reduce((n, f) => n + f.bits, 0) >= used) {
      for (const f of pend) { sc.low -= f.bits; f.offset = sc.low }
    } else {
      why.set(s, 'its never-used bits ran out')
      repack(s, bump(sc.gen))
    }
  }
  back = new Set(added.filter(f => back.has(K(f.mod, f.sym)) && !repacked.has(f.scope)).map(f => K(f.mod, f.sym)))
  const parked = new Set(st.parked.map(f => K(f.mod, f.sym)))
  for (const f of gone) {
    const k = K(f.mod, f.sym), w = wantF.get(k)
    if (parked.has(k)) notes.push(`${f.mod} ${f.sym} (${f.scope}): parked; its bits and values are kept for when it comes back`)
    else notes.push(`${f.mod} ${f.sym} (${f.scope}): ${w === undefined ? 'parked, and its scope repacked'
      : w[0] === f.scope && w[1] === f.bits ? 'changed its init' : 'changed size'}, its data is dropped`)
  }
  for (const f of added) {
    if (back.has(K(f.mod, f.sym))) notes.push(`${f.mod} ${f.sym}: back at bit ${f.offset} of ${f.scope} storage, with the values it had`)
    else notes.push(`${f.mod} ${f.sym}: ${f.bits} bits of ${f.scope} storage at bit ${f.offset}`)
  }
  st.fields = fields.sort((a, b) => SCOPE_NUM[a.scope] - SCOPE_NUM[b.scope] || a.offset - b.offset)
  codes(st, base, want.locks, new Set(kept.map(f => K(f.mod, f.sym))), back, notes)
  ccs(st, base, want.ccs, new Set(fields.filter(f => f.scope === 'kit').map(f => K(f.mod, f.sym))), notes)
  const scopeOf = new Map(fields.map(f => [K(f.mod, f.sym), f.scope]))
  for (const k of [...want.lfos].sort((a, b) => cmpKey(unK(a), unK(b)))) {
    const sc = scopeOf.get(k)!
    if (!base.lfoDst(sc)) {
      const [m, s] = unK(k)
      notes.push(`${m} ${s}: not an LFO destination on this build, which places no ${sc === 'sound' ? store.LOCK_DST : store.FX_LOCK_DST}`)
    }
  }
  st.id = layoutId(st, base)
  if (st.id !== oldId) {
    const ids: number[] = [], seen = new Set([st.id])
    for (const a of [oldId, ...st.ids]) if (!seen.has(a)) { seen.add(a); ids.push(a) }
    st.ids = ids.slice(0, MAX_IDS)
    notes.push(`layout ID ${hex8(st.id)} (from ${hex8(oldId)}, whose projects it still reads)`)
  }
  st.flashed_id = base.flashedId
  return new Placed(base, st, notes, want)
}

const keySort = (a: string, b: string) => cmpKey(unK(a), unK(b))

function codes(st: State, base: Base, want: Set<string>, kept: Set<string>, back: Set<string>, notes: string[]) {
  const flashed = base.codeSet()
  const top = flashed.size ? Math.max(...flashed) : base.firstCode - 1
  const out: Code[] = []
  let freed = st.freed_codes.filter(c => !flashed.has(c.code)).map(c => ({ ...c }))
  for (const c of st.freed_codes)
    if (flashed.has(c.code)) notes.push(`${c.mod} ${c.sym}: lock code ${c.code} is the flashed build's now`)
  for (const c of st.codes) {
    const k = K(c.mod, c.sym)
    if (flashed.has(c.code)) notes.push(`${c.mod} ${c.sym}: lock code ${c.code} is the flashed build's now`)
    else if (want.has(k) && kept.has(k)) out.push({ ...c })
    else {
      freed.push({ ...c })
      notes.push(`${c.mod} ${c.sym}: lock code ${c.code} kept for it while it is out`)
    }
  }
  const have = new Set(out.map(c => K(c.mod, c.sym)))
  let nxt = st.next_code
  for (const k of [...want].filter(k => !have.has(k)).sort(keySort)) {
    const [m, s] = unK(k)
    const same = freed.filter(c => K(c.mod, c.sym) === k)
    let c: Code
    if (same.length && back.has(k)) {
      c = same.at(-1)!
      freed.splice(freed.indexOf(c), 1)
      notes.push(`${m} ${s}: back with lock code ${c.code}`)
    } else if (nxt > top) {
      c = { mod: m, sym: s, code: nxt }
      nxt -= 1
      notes.push(`${m} ${s}: lock code ${c.code}`)
    } else if (freed.length) {
      const old = freed.shift()!
      c = { mod: m, sym: s, code: old.code }
      notes.push(`${m} ${s}: lock code ${c.code} (was ${old.mod} ${old.sym}'s)`)
    } else {
      const used = new Set([...flashed, ...out.map(c => c.code)])
      const free: number[] = []
      for (let x = base.lastCode; x >= base.firstCode; x--) if (!used.has(x)) free.push(x)
      if (!free.length) throw new ModError('the +Drive mods have more lockable parameters than the build leaves codes')
      c = { mod: m, sym: s, code: free[0] }
      notes.push(`${m} ${s}: lock code ${c.code}`)
    }
    freed = freed.filter(d => K(d.mod, d.sym) !== k)
    out.push(c)
  }
  st.codes = out.sort((a, b) => a.code - b.code)
  st.freed_codes = freed
  st.next_code = Math.max(nxt, base.firstCode - 1)
}

function ccs(st: State, base: Base, want: Set<string>, kits: Set<string>, notes: string[]) {
  const flashed = base.ccSet()
  const pool = [...base.pool].reverse().filter(cc => !flashed.has(cc))
  const room = base.room[store.CCS_TABLE] || 0
  const out: Cc[] = []
  const freed = st.freed_ccs.filter(c => !flashed.has(c.cc)).map(c => ({ ...c }))
  for (const c of st.ccs) {
    const k = K(c.mod, c.sym)
    if (flashed.has(c.cc)) notes.push(`${c.mod} ${c.sym}: CC ${c.cc} is the flashed build's now`)
    else if (want.has(k) && !(kits.has(k) && base.fxTaken.includes(c.cc))) out.push({ ...c })
    else {
      freed.push({ ...c })
      notes.push(`${c.mod} ${c.sym}: CC ${c.cc} kept for it until the unused CCs run out`)
    }
  }
  const used = new Set([...out.map(c => c.cc), ...freed.map(c => c.cc)])
  const unused = pool.filter(cc => !used.has(cc))
  const have = new Set(out.map(c => K(c.mod, c.sym)))
  for (const k of [...want].filter(k => !have.has(k)).sort(keySort)) {
    const [m, s] = unK(k)
    const ok = kits.has(k) ? (cc: number) => !base.fxTaken.includes(cc) : () => true
    const same = freed.filter(c => K(c.mod, c.sym) === k && ok(c.cc))
    const nw = unused.filter(ok)
    const old = freed.filter(c => ok(c.cc))
    if (out.length >= room) {
      notes.push(`${m} ${s}: no CC (the build's table ${store.CCS_TABLE} is full); it is on MIDI by its NRPN only`)
      continue
    }
    let c: Cc
    if (same.length) {
      c = same.at(-1)!
      freed.splice(freed.indexOf(c), 1)
      notes.push(`${m} ${s}: back with CC ${c.cc}`)
    } else if (nw.length) {
      unused.splice(unused.indexOf(nw[0]), 1)
      c = { mod: m, sym: s, cc: nw[0] }
      notes.push(`${m} ${s}: CC ${c.cc}`)
    } else if (old.length) {
      const o = old[0]
      freed.splice(freed.indexOf(o), 1)
      c = { mod: m, sym: s, cc: o.cc }
      notes.push(`${m} ${s}: CC ${c.cc} (was ${o.mod} ${o.sym}'s)`)
    } else {
      notes.push(`${m} ${s}: no CC left; it is on MIDI by its NRPN only`)
      continue
    }
    out.push(c)
  }
  st.ccs = out.sort((a, b) => a.cc - b.cc)
  st.freed_ccs = freed
}
