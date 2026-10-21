// SPDX-License-Identifier: GPL-3.0-or-later
// Mod storage: the lockfile and the allocator (docs/ADAPTING.md, "Mod storage"). Ported from elekloader's store.py
// (GPL-2.0-or-later): the same lockfile, the same places for the same fields, the same messages.
//
// A core that keeps mod storage (core-ar2 0.2) imports `__store_gen`. Mods declare fields in their mod.json
// ("storage": [{"sym", "scope", "bits"}]) and the linker gives each field's symbol a descriptor: its scope in bits
// 28..31, its width in bits 22..27 and its bit offset in bits 0..21. The lockfile keeps every field where it was from
// one build to the next; a freed field that comes back takes its old place, any other new or resized field takes bits
// no field has used, a scope that runs out is repacked under a new generation, and any change gives the lockfile its
// new layout's ID (a hash of the layout) with the old one kept as an ancestor. Nothing here is random.
import { sha } from './bytes.ts'
import { ModError, cmpVersions, int } from './elemod.ts'
import { dumps as pyDumps, repr, str } from './py.ts'

export const FORMAT = 1
export const SCOPES = ['sound', 'kit', 'global', 'track', 'pattern', 'project'] as const
export type Scope = typeof SCOPES[number]
const SCOPE_NUM: Record<string, number> = Object.fromEntries(SCOPES.map((s, i) => [s, i + 1]))
const MAX_ANCESTORS = 15
const MAX_BITS = 32
export const GEN_SYMS = ['__store_gen', '__store_gen2']
export const IDS_TABLE = 'core_store_ids'

export type Field = { mod: string; sym: string; scope: string; offset: number; bits: number }
export type Lock = {
  elekloader_store: number; device: string; id: string; ancestors: string[]
  split: { track: number; pattern: number }; generation: Record<string, number>; next: Record<string, number>
  fields: Field[]; freed: Field[]
}
export type Decl = [string, string, string, number]          // mod id, sym, scope, bits

type Spec = { fixed: Record<string, number>; coreFixed: [string, Record<string, number>][]; live: Record<string, number>; store: number; items: Record<string, number>; split: { track: number; pattern: number } }
const DEVICES: Record<string, Spec> = {
  'analog-rytm-mk2': {
    fixed: { sound: 24, kit: 64, global: 40 },
    coreFixed: [['0.3', { sound: 56 }]],      // from that core's version on (0.3: OS 1.74 only)
    live: { track: 512, pattern: 512 },        // the most the split can give an item
    store: 4352 - 10,                           // bytes after the header
    items: { track: 129 * 13, pattern: 129 },
    split: { track: 2, pattern: 4 },            // a new lockfile's
  },
}

export const supports = (devKey: string) => Object.hasOwn(DEVICES, devKey)

function spec(devKey: string): Spec {
  if (!Object.hasOwn(DEVICES, devKey)) throw new ModError(`the ${devKey} has no mod storage`)
  return DEVICES[devKey]
}

/** -> {scope: bits} for a split, with a core of that version (null: the newest core's room). store.capacity. */
export function capacity(devKey: string, split: { track: number; pattern: number }, core: string | null = null): Record<string, number> {
  const sp = spec(devKey)
  const cap: Record<string, number> = { ...sp.fixed }
  for (const [v, f] of sp.coreFixed) if (core === null || cmpVersions(core, v) >= 0) Object.assign(cap, f)
  cap.track = 8 * split.track
  cap.pattern = 8 * split.pattern
  cap.project = 8 * (sp.store - sp.items.track * split.track - sp.items.pattern * split.pattern)
  return cap
}

const isDict = (v: unknown): v is Record<string, any> => typeof v === 'object' && v !== null && !Array.isArray(v) && !(v instanceof Uint8Array)
const whole = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v)
const idStr = (v: number) => '0x' + (v >>> 0).toString(16).padStart(8, '0')
const sameKeys = (d: Record<string, any>, keys: readonly string[]) => {
  const k = Object.keys(d)
  return k.length === keys.length && keys.every(x => Object.hasOwn(d, x))
}

function idVal(s: unknown, what: string): number {
  const v = int(s, 'lockfile: ' + what)
  if (!(v > 0 && v <= 0xFFFFFFFF)) throw new ModError(`lockfile: ${what} ${repr(s)} is not a 32-bit ID other than 0`)
  return v
}

/** A mod document's "storage" -> [sym, scope, bits][]. */
export function parseDecls(m: Record<string, any>): [string, string, number][] {
  const out: [string, string, number][] = []
  const seen = new Set<string>()
  const st = Object.hasOwn(m, 'storage') ? m.storage : []
  if (!Array.isArray(st)) throw new ModError('"storage" is a list of fields')
  for (const f of st) {
    if (!isDict(f) || Object.keys(f).some(k => !['sym', 'scope', 'bits'].includes(k)))
      throw new ModError('a storage field is {"sym", "scope", "bits"}')
    const sym = Object.hasOwn(f, 'sym') ? f.sym : null
    const scope = Object.hasOwn(f, 'scope') ? f.scope : null
    const bits = Object.hasOwn(f, 'bits') ? f.bits : null
    if (typeof sym !== 'string' || !sym || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(sym))
      throw new ModError(`storage field ${repr(sym)}: "sym" is a C name`)
    if (!(SCOPES as readonly unknown[]).includes(scope))
      throw new ModError(`storage field ${sym}: scope ${repr(scope)} (${SCOPES.join(', ')})`)
    if (!whole(bits) || !(bits >= 1 && bits <= MAX_BITS))
      throw new ModError(`storage field ${sym}: bits ${repr(bits)} (1 to ${MAX_BITS})`)
    if (seen.has(sym)) throw new ModError(`storage field ${sym} is declared twice`)
    seen.add(sym)
    out.push([sym, scope as string, bits])
  }
  return out
}

const cmpStr = (a: string, b: string) => (a < b ? -1 : a > b ? 1 : 0)
const byField = (a: Field, b: Field) => SCOPE_NUM[a.scope] - SCOPE_NUM[b.scope] || a.offset - b.offset || a.bits - b.bits
  || cmpStr(a.mod, b.mod) || cmpStr(a.sym, b.sym)

/** A layout's ID: the first 4 bytes of a SHA-256 of everything that says where data sits (not the ID or the
 * ancestors), other than 0. Freed fields count: their bits still hold their values, which a field coming back reads.
 * store.layout_id. */
export function layoutId(lock: Lock): number {
  const what: unknown[] = [lock.elekloader_store, lock.device, [lock.split.track, lock.split.pattern],
    SCOPES.map(s => lock.generation[s]), SCOPES.map(s => lock.next[s])]
  for (const k of ['fields', 'freed'] as const)
    what.push([...lock[k]].sort(byField).map(f => [f.scope, f.offset, f.bits, f.mod, f.sym]))
  return parseInt(sha(new TextEncoder().encode(pyDumps(what))).slice(0, 8), 16) || 1
}

export function fresh(devKey: string): Lock {
  const sp = spec(devKey)
  const lock: Lock = {
    elekloader_store: FORMAT, device: devKey, id: '', ancestors: [], split: { ...sp.split },
    generation: Object.fromEntries(SCOPES.map(s => [s, 1])), next: Object.fromEntries(SCOPES.map(s => [s, 0])),
    fields: [], freed: [],
  }
  lock.id = idStr(layoutId(lock))
  return lock
}

/** Validate a lockfile's shape. Throws ModError. */
export function checkLock(lock: unknown, devKey: string): asserts lock is Lock {
  if (!isDict(lock)) throw new ModError('lockfile: not a JSON object')
  const v = Object.hasOwn(lock, 'elekloader_store') ? lock.elekloader_store : null
  if (!whole(v)) throw new ModError('lockfile: not an elekloader storage lockfile')
  if (v > FORMAT) throw new ModError(`lockfile: format ${v} is newer than this elekloader (${FORMAT}): update elekloader`)
  if (v < 1) throw new ModError(`lockfile: format ${repr(v)}`)
  const device = Object.hasOwn(lock, 'device') ? lock.device : null
  if (device !== devKey) throw new ModError(`lockfile: made for the ${device === null ? 'None' : str(device)}, not the ${devKey}`)
  idVal(Object.hasOwn(lock, 'id') ? lock.id : null, 'id')
  const anc = Object.hasOwn(lock, 'ancestors') ? lock.ancestors : []
  if (!Array.isArray(anc)) throw new ModError('lockfile: "ancestors" is a list')
  for (const a of anc) idVal(a, 'ancestor')
  const sp = lock.split
  if (!isDict(sp) || !sameKeys(sp, ['track', 'pattern']) || !Object.values(sp).every(x => whole(x) && x >= 0))
    throw new ModError('lockfile: "split" gives bytes per track and per pattern')
  const cap = capacity(devKey, sp as Lock['split'])
  const s0 = spec(devKey)
  if (cap.project < 0 || 8 * sp.track > s0.live.track || 8 * sp.pattern > s0.live.pattern)
    throw new ModError(`lockfile: the split ${sp.track}/${sp.pattern} does not fit the store`)
  for (const k of ['generation', 'next']) {
    const d = lock[k]
    if (!isDict(d) || !sameKeys(d, SCOPES) || !SCOPES.every(s => whole(d[s])))
      throw new ModError(`lockfile: "${k}" gives a number per scope`)
  }
  for (const s of SCOPES) {
    if (!(lock.generation[s] >= 1 && lock.generation[s] <= 255)) throw new ModError(`lockfile: generation ${repr(lock.generation[s])} of ${s}`)
    if (!(lock.next[s] >= 0 && lock.next[s] <= cap[s])) throw new ModError(`lockfile: next bit ${repr(lock.next[s])} of ${s}`)
  }
  for (const k of ['fields', 'freed']) {
    const fs = lock[k]
    if (!Array.isArray(fs)) throw new ModError(`lockfile: "${k}" is a list`)
    for (const f of fs) {
      if (!isDict(f) || !sameKeys(f, ['mod', 'sym', 'scope', 'offset', 'bits']) || typeof f.mod !== 'string'
        || typeof f.sym !== 'string' || !(SCOPES as readonly unknown[]).includes(f.scope) || !whole(f.offset) || f.offset < 0
        || !whole(f.bits) || !(f.bits >= 1 && f.bits <= MAX_BITS))
        throw new ModError(`lockfile: a bad entry in "${k}"`)
      if (k === 'fields' && !(f.offset >= 0 && f.offset <= lock.next[f.scope] - f.bits))
        throw new ModError(`lockfile: field ${f.sym} of ${f.mod} lies past its scope's used bits`)
    }
  }
}

const bump = (g: number) => (g >= 255 ? 1 : g + 1)
const byModSym = (a: { mod: string; sym: string }, b: { mod: string; sym: string }) =>
  a.mod < b.mod ? -1 : a.mod > b.mod ? 1 : a.sym < b.sym ? -1 : a.sym > b.sym ? 1 : 0
const ceilDiv = (a: number, b: number) => Math.floor((a + b - 1) / b)

/** lock: a lockfile, or null for a new one; decls: [mod id, sym, scope, bits]. -> [the new lockfile, notes]. The
 * lockfile given is not changed. store.allocate. */
export function allocate(lock0: Lock | null, decls: Decl[], devKey: string, core: string | null = null): [Lock, string[]] {
  const notes: string[] = []
  let lock: Lock
  let isNew: boolean
  if (lock0 === null) {
    lock = fresh(devKey)
    isNew = true
  } else {
    checkLock(lock0, devKey)
    lock = structuredClone(lock0)
    isNew = false
  }
  const want = new Map<string, [string, number]>()
  const keyOf = (mid: string, sym: string) => mid + '\0' + sym
  for (const [mid, sym, scope, bits] of decls) {
    if ([...want.keys()].some(k => k.split('\0')[1] === sym)) throw new ModError(`the storage field ${sym} is declared by two mods`)
    want.set(keyOf(mid, sym), [scope, bits])
  }
  const kept: Field[] = [], gone: Field[] = []
  for (const f of lock.fields) {
    const w = want.get(keyOf(f.mod, f.sym))
    if (w && w[0] === f.scope && w[1] === f.bits) kept.push(f)
    else gone.push(f)
  }
  const have = new Set(kept.map(f => keyOf(f.mod, f.sym)))
  const added: (Omit<Field, 'offset'> & { offset: number | null })[] = [...want.keys()]
    .map(k => { const [mod, sym] = k.split('\0'); return { mod, sym } })
    .sort(byModSym)
    .filter(({ mod, sym }) => !have.has(keyOf(mod, sym)))
    .map(({ mod, sym }) => { const [scope, bits] = want.get(keyOf(mod, sym))!; return { mod, sym, scope, offset: null, bits } })
  // a freed field back with the same scope and width takes its old place: no field was given those bits since, so
  // its values are still there
  const back = new Set<string>()
  for (const f of added) {
    const same = lock.freed.filter(g => g.mod === f.mod && g.sym === f.sym && g.scope === f.scope && g.bits === f.bits)
    if (same.length) {
      const g = same[same.length - 1]
      f.offset = g.offset
      // the first entry equal to it, as Python's list.remove
      lock.freed.splice(lock.freed.findIndex(h => byField(h, g) === 0), 1)
      back.add(keyOf(f.mod, f.sym))
    }
  }
  for (const f of gone) lock.freed.push({ ...f })
  const fields = [...kept, ...added] as Field[]
  let cap = capacity(devKey, lock.split, core)
  const repacked = new Set<string>()

  const repack = (scope: string) => {
    repacked.add(scope)
    lock.generation[scope] = bump(lock.generation[scope])
    lock.freed = lock.freed.filter(f => f.scope !== scope)
    let at = 0
    for (const f of fields.filter(f => f.scope === scope).sort(byModSym)) {
      f.offset = at
      at += f.bits
    }
    lock.next[scope] = at
    notes.push(`${scope} storage repacked (generation ${lock.generation[scope]}): older projects and sounds load it at defaults`)
  }
  const need = (scope: string) => fields.filter(f => f.scope === scope).reduce((n, f) => n + f.bits, 0)
  const pending = (scope: string) => added.filter(f => f.scope === scope && f.offset === null)

  const sp = spec(devKey)
  const split = { ...lock.split }
  for (const k of ['track', 'pattern'] as const) {
    const n = need(k)
    if (n > sp.live[k]) throw new ModError(`the mods need ${n} bits per ${k}; the ${devKey} keeps at most ${sp.live[k]}`)
    const fits = lock.next[k] + pending(k).reduce((a, f) => a + f.bits, 0) <= 8 * split[k]
    if (!fits && n > 8 * split[k]) split[k] = ceilDiv(n, 8)
  }
  if (split.track !== lock.split.track || split.pattern !== lock.split.pattern) {
    // the pattern tracks come first in the store: they move only when their own size changes
    const moved = split.track !== lock.split.track ? ['track', 'pattern', 'project'] : ['pattern', 'project']
    lock.split = split
    cap = capacity(devKey, split, core)
    if (cap.project < need('project'))
      throw new ModError(`the mods need ${sp.items.track * split.track + sp.items.pattern * split.pattern + ceilDiv(need('project'), 8)} bytes of pattern-track, pattern and project storage; the ${devKey} has ${sp.store}`)
    notes.push(`the tagged store now keeps ${split.track} B per pattern track and ${split.pattern} B per pattern`)
    for (const k of moved) repack(k)
  }
  for (const s of SCOPES) {
    if (need(s) > cap[s]) {
      const more = capacity(devKey, lock.split)[s]
      throw new ModError(`the mods need ${need(s)} bits of ${s} storage; the ${devKey} has ${cap[s]}${more > cap[s] ? ` with core ${core} (${more} with a newer core)` : ''}`)
    }
    if (fields.some(f => f.scope === s && f.offset !== null && f.offset + f.bits > cap[s])) {
      repack(s)                   // placed by a newer core, past what this one keeps
      continue
    }
    const pend = pending(s)
    if (!pend.length) continue
    if (lock.next[s] + pend.reduce((a, f) => a + f.bits, 0) <= cap[s]) {
      for (const f of pend) {
        f.offset = lock.next[s]
        lock.next[s] += f.bits
      }
    } else repack(s)
  }
  for (const f of gone) {
    const resized = want.has(keyOf(f.mod, f.sym))
    if (resized || repacked.has(f.scope))
      notes.push(`${f.mod} ${f.sym} (${f.scope}): ${resized ? 'changed size' : 'no longer in the build'}, its data is dropped`)
    else notes.push(`${f.mod} ${f.sym} (${f.scope}): no longer in the build; its bits are kept, so it gets its values back if it returns`)
  }
  for (const f of added) {
    if (back.has(keyOf(f.mod, f.sym)) && !repacked.has(f.scope))
      notes.push(`${f.mod} ${f.sym}: back at bit ${f.offset} of ${f.scope} storage, with the values it had`)
    else notes.push(`${f.mod} ${f.sym}: ${f.bits} bits of ${f.scope} storage at bit ${f.offset}`)
  }
  lock.fields = fields.sort((a, b) => SCOPE_NUM[a.scope] - SCOPE_NUM[b.scope] || a.offset - b.offset)
  if (isNew) {
    lock.id = idStr(layoutId(lock))
    notes.unshift(`a new lockfile, layout ID ${lock.id}: projects and sounds saved by builds with other layouts load their mod data at defaults`)
  } else if (gone.length || added.length) {
    const old = lock.id
    lock.id = idStr(layoutId(lock))
    const seen = new Set([idVal(lock.id, 'id')]), anc: string[] = []
    for (const a of [old, ...lock.ancestors]) {             // a layout can come back: no ID twice
      const v = idVal(a, 'ancestor')
      if (!seen.has(v)) {
        seen.add(v)
        anc.push(idStr(v))
      }
    }
    lock.ancestors = anc.slice(0, MAX_ANCESTORS)
    notes.push(`layout ID ${lock.id} (from ${old}, whose projects it still reads)`)
  }
  return [lock, notes]
}

/** -> [{name: value}, the IDs' table entries] for the linker. store.symbols. */
export function symbols(lock: Lock): [Map<string, number>, Uint8Array[]] {
  const g = lock.generation, sp = lock.split
  const out = new Map<string, number>()
  out.set('__store_gen', ((g.sound << 24) | (g.kit << 16) | (g.global << 8) | g.track) >>> 0)
  out.set('__store_gen2', ((g.pattern << 24) | (g.project << 16) | (sp.track << 8) | sp.pattern) >>> 0)
  for (const f of lock.fields) out.set(f.sym, ((SCOPE_NUM[f.scope] << 28) | (f.bits << 22) | f.offset) >>> 0)
  const ids = [idVal(lock.id, 'id'), ...lock.ancestors.map(a => idVal(a, 'ancestor'))]
  return [out, ids.map(i => new Uint8Array([i >>> 24, (i >>> 16) & 255, (i >>> 8) & 255, i & 255]))]
}

export const dumps = (lock: Lock): string => pyDumps(lock, 1) + '\n'

export const TRAILER = 'ELKSTORE'

/** The lockfile as the build carries it, after the mods' image: TRAILER, its length (4 bytes, big-endian), then
 * dumps(lock). Nothing reads it on the unit: it is there so the lockfile can be taken back out (fromImage). */
export function trailer(lock: Lock): Uint8Array {
  const text = new TextEncoder().encode(dumps(lock))
  const out = new Uint8Array(12 + text.length)
  out.set(new TextEncoder().encode(TRAILER))
  new DataView(out.buffer).setUint32(8, text.length)
  out.set(text, 12)
  return out
}

/** A built main OS image -> the lockfile it carries, or null. */
export function fromImage(image: Uint8Array): Lock | null {
  const m = new TextEncoder().encode(TRAILER)
  let at = -1
  for (let i = image.length - m.length; i >= 0 && at < 0; i--) if (m.every((b, j) => image[i + j] === b)) at = i
  if (at < 0) return null
  const n = new DataView(image.buffer, image.byteOffset).getUint32(at + 8)
  let lock: unknown
  try {
    lock = JSON.parse(new TextDecoder('ascii', { fatal: true }).decode(image.subarray(at + 12, at + 12 + n)))
  } catch {
    throw new ModError("the build's lockfile cannot be read")
  }
  checkLock(lock, (lock as any)?.device)
  return lock as Lock
}
