// SPDX-License-Identifier: GPL-3.0-or-later
// Mod storage: the lockfile and the allocator (docs/ADAPTING.md, "Mod storage"). Ported from elekloader's store.py
// (GPL-2.0-or-later): the same lockfile, the same places for the same fields, the same messages.
//
// A core that keeps mod storage (core-ar2 0.2) imports `__store_gen`. Mods declare fields in their mod.json
// ("storage": [{"sym", "scope", "bits"}]) and the linker gives each field's symbol a descriptor: its scope in bits
// 28..31, its width in bits 22..27 and its bit offset in bits 0..21. The lockfile keeps every field where it was from
// one build to the next; a freed field that comes back takes its old place, any other new or resized field takes bits
// no field has used, a scope that runs out is repacked under a new generation, and any change gives the lockfile its
// new layout's ID (a hash of the layout) with the old one kept as an ancestor. A sound field declared with
// "lock": true also gets a lock code, 42..127 (core-ar2 0.8), kept in "codes", "freed_codes" and "next_code", keys a
// lockfile has only once a build had a lockable field. A lockable field declared with "cc": true also gets a CC from the
// device's pool, kept in "ccs" and "freed_ccs" by the field whatever its place or code, and not part of the layout ID.
// A lockable field declared with "lfo": true is also an LFO destination: its code has LFO_FLAG in core_lock_codes, and
// a build with one places the core's own field core_lfo_dst. A lockable field declared with "smooth": false jumps where the
// core smooths the others (core-ar2 0.9): its code has SMOOTH_OFF in core_lock_codes, not part of the layout ID either.
// A lockable field declared with "mod": true is also a velocity and aftertouch destination (core-ar2 0.9): its code has
// ROUTE_FLAG in core_lock_codes, and a route keeps the code in its own byte, so it needs no room. On the Analog Four
// MKII (core-a4 0.5) codes are numbered per track kind, 0x70..0x7f each (synth: a sound field; FX: a kit field; CV: a
// kit field declared "track": "cv"): each code entry has its "kind", "next_code" is one per kind, the kind is part of
// the layout ID, and a CV field's code has CV_FLAG in core_lock_codes.
// A field declared with "init": N (core-ar2 0.10) reads N where its bits are zero: the core keeps value ^ init. The
// lockfile keeps the init with the field ("init", only when not 0; format 2) and it is part of the layout ID, so a
// field whose init changes is a new field. Its descriptor carries its slot in core_field_inits (1..63) in bits
// 16..21, which no offset needs (no scope keeps 2^16 bits).
// A field declared with "handle": true (core-ar2 0.11) holds a mod's handle on something kept elsewhere, 0 for none:
// placed, kept and read like any field, not in the lockfile and not part of the layout ID. The linker gives the core
// the table core_handles, each handle field's descriptor in the lockfile's order. A handle has no "lock" and no init.
// A mod that declares "knobs": n (core-ar2 0.13) gets a run of n knob ids from the device's first mod id up (knobs),
// kept in "knobs", "freed_knobs" and "next_knob" so screens and test sheets stay the same; not stored data, so not part
// of the layout ID. A mod keeps its run while its count stays, gets it back when it returns, keeps its first id when
// a new count has room there, and otherwise takes never-used ids past every other run; freed runs are given out again
// only when the never-used ids run out.
// Nothing here is random.
import { sha } from './bytes.ts'
import { ModError, cmpVersions, int } from './elemod.ts'
import { dumps as pyDumps, repr, str } from './py.ts'

export const FORMAT = 2                        // 2 only for a lockfile with a field's init (core-ar2 0.10)
export const SCOPES = ['sound', 'kit', 'global', 'track', 'pattern', 'project'] as const
export type Scope = typeof SCOPES[number]
const SCOPE_NUM: Record<string, number> = Object.fromEntries(SCOPES.map((s, i) => [s, i + 1]))
const MAX_ANCESTORS = 15
const MAX_BITS = 32
export const GEN_SYMS = ['__store_gen', '__store_gen2']
export const IDS_TABLE = 'core_store_ids'
export const CODES_TABLE = 'core_lock_codes'
export const CCS_TABLE = 'core_lock_ccs'
export const LOCK_DST = 'core_lfo_dst'
export const FX_LOCK_DST = 'core_fx_lfo_dst'   // core-ar2 0.9: the FX LFO's code, a kit field
export const LFO_FLAG = 0x100                  // in core_lock_codes: the field is an LFO destination
export const SMOOTH_OFF = 0x200                // in core_lock_codes: the field's value jumps (core-ar2 0.9)
export const ROUTE_FLAG = 0x400                // in core_lock_codes: a velocity/aftertouch destination (core-ar2 0.9)
export const CV_FLAG = 0x800                   // in core_lock_codes: a kit field of the CV track (core-a4 0.5)
const FIRST_CODE = 42, LAST_CODE = 127         // a device's codes unless its spec says others
export const INITS_TABLE = 'core_field_inits'  // core-ar2 0.10: the fields' inits, by slot
export const HANDLES_TABLE = 'core_handles'    // core-ar2 0.11: the handle fields' descriptors
export const KNOB_TOP = 0xFFFF                 // the last knob id: page knob lists hold 16 bits (abs16)
const MAX_INITS = 63                           // slots 1..63, in the descriptor's bits 16..21
const LOCK_SCOPES = ['sound', 'kit']
const KIND_NAMES: Record<string, string> = { synth: 'the synth tracks', fx: 'the FX track', cv: 'the CV track' }

export type Field = { mod: string; sym: string; scope: string; offset: number; bits: number; init?: number }
export type Lock = {
  elekloader_store: number; device: string; id: string; ancestors: string[]
  split: { track: number; pattern: number }; generation: Record<string, number>; next: Record<string, number>
  fields: Field[]; freed: Field[]
  codes?: Code[]; freed_codes?: Code[]; next_code?: number | Record<string, number>
  ccs?: Cc[]; freed_ccs?: Cc[]
  knobs?: Knob[]; freed_knobs?: Knob[]; next_knob?: number
}
export type Knob = { mod: string; first: number; count: number }
export type Code = { mod: string; sym: string; code: number; kind?: string }
export type Cc = { mod: string; sym: string; cc: number }
export type Decl = [string, string, string, number]          // mod id, sym, scope, bits

type Spec = { fixed: Record<string, number>; coreFixed: [string, Record<string, number>][]; live: Record<string, number>; store: number; items: Record<string, number>; split: { track: number; pattern: number }; ccs?: number[]; lockDst?: number; fxLockDst?: number; fxCcsTaken?: number[]; codes?: [number, number]; kinds?: string[]; noLfo?: boolean }
const range = (a: number, b: number) => Array.from({ length: b - a }, (_, i) => a + i)
const DEVICES: Record<string, Spec> = {
  'analog-rytm-mk2': {
    fixed: { sound: 24, kit: 64, global: 40 },
    coreFixed: [['0.3', { sound: 56 }]],      // from that core's version on (0.3: OS 1.74 only)
    live: { track: 512, pattern: 512 },        // the most the split can give an item
    store: 4352 - 10,                           // bytes after the header
    items: { track: 129 * 13, pattern: 129 },
    split: { track: 2, pattern: 4 },            // a new lockfile's
    // the CCs no table of stock's maps on a track's channel and that are not the performances' (0x401de1f4), in the
    // order fields get them
    ccs: [8, 9, ...range(48, 64), ...range(65, 70), 84, 85, ...range(110, 117), 119],
    lockDst: 7,                                 // core_lfo_dst: a code - 41, 0 for none
    fxLockDst: 7,                               // core_fx_lfo_dst (core-ar2 0.9): the same, for the FX LFO
    // of the pool, the CCs stock maps on the FX channel (0x413dcb14): a kit field (the FX track's) gets none of them
    fxCcsTaken: [84, 85],
  },
  'analog-four-mk2': {                          // core-a4 (mods/core-a4/store.c)
    fixed: { sound: 32, kit: 128, global: 64 },
    coreFixed: [],
    live: { track: 512, pattern: 512 },
    store: 4352 - 10,
    items: { track: 129 * 6, pattern: 129 },
    split: { track: 2, pattern: 4 },
    // core-a4 0.5: codes per track kind (stock's synth, FX and CV parameter tables), 0x70..0x7f each; every source is
    // a destination of a "mod" field, so no "lfo"
    codes: [0x70, 0x7f],
    kinds: ['synth', 'fx', 'cv'],
    noLfo: true,
    // core-a4 0.5: the CCs no table of stock's maps on a track's channel (the synth kind's and the controller kinds'
    // MISC, TRACK and PERF, nor bank select 0 and 32, from the records at 0x40186af4), those the FX and CV kinds map
    // first, so the synth tracks' fields take them before the CCs every kind leaves free
    ccs: [12, 13, 14, 15, 28, 29, 30, 31, 35, 36, 37, 39, 40, 41, 42, 43, 44, 45, 46, 47, 60, 61, 62, 63, 96, 97, 100, 101],
    // of the pool, the CCs the FX (12..14, 28..31) or CV (12..15) tables map: a kit field gets none of them
    fxCcsTaken: [12, 13, 14, 15, 28, 29, 30, 31],
  },
}

export const supports = (devKey: string) => Object.hasOwn(DEVICES, devKey)

/** The bits of core_lfo_dst on the device, 0 where the core keeps none. store.lock_dst_bits. */
export function lockDstBits(devKey: string): number {
  return spec(devKey).lockDst ?? 0
}

/** The bits of core_fx_lfo_dst on the device, 0 where the core keeps none. store.fx_lock_dst_bits. */
export function fxLockDstBits(devKey: string): number {
  return spec(devKey).fxLockDst ?? 0
}

/** -> [first, last] lock code on the device. store.code_range. */
export function codeRange(devKey: string): [number, number] {
  return spec(devKey).codes ?? [FIRST_CODE, LAST_CODE]
}

/** -> the device's track kinds that number codes apart, or [] where one range serves every track. store.kinds. */
export function kinds(devKey: string): string[] {
  return spec(devKey).kinds ?? []
}

/** A lockable field's kind on the device: null where codes have no kinds. store.kind_of. */
export function kindOf(devKey: string, scope: string | undefined, cv: boolean): string | null {
  if (!kinds(devKey).length) return null
  return scope === 'sound' ? 'synth' : cv ? 'cv' : 'fx'
}

/** The lockable fields' flags the device can't take; scopes: mod\0sym -> scope. Throws '<mod>: storage field ...'.
 * store.check_flags. */
export function checkFlags(devKey: string, scopes: Map<string, string>, lfos: [string, string][] = [], routes: [string, string][] = [],
  cvs: [string, string][] = [], ccs: [string, string][] = []): void {
  const sp = spec(devKey)
  const sorted = (ks: [string, string][]) => [...ks].sort((a, b) => cmpStr(a[0], b[0]) || cmpStr(a[1], b[1]))
  for (const [m, sy] of sorted(lfos))
    if (sp.noLfo) throw new ModError(`${m}: storage field ${repr(sy)}: on the ${devKey} every modulation source can target a "mod" field; "lfo" is the Analog Rytm MKII's`)
  for (const [m, sy] of sorted(routes))
    if (scopes.get(m + '\0' + sy) === 'kit' && !kinds(devKey).length)
      throw new ModError(`${m}: storage field ${repr(sy)}: a kit field (the FX track's) cannot be a velocity or aftertouch destination: the FX track has no routes`)
  for (const [m, sy] of sorted(cvs))
    if (!kinds(devKey).includes('cv')) throw new ModError(`${m}: storage field ${repr(sy)}: the ${devKey} has no CV track`)
  for (const [m, sy] of sorted(ccs))
    if (!sp.ccs?.length) throw new ModError(`${m}: storage field ${repr(sy)}: the ${devKey} keeps no CCs for mod parameters`)
}

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

/** The oldest core after `core` that gives `scope` the newest core's room, for a message: a version that exists, so the
 * message stays true on an OS that core is not made for (whose refusal then says so). store._room_core. */
function roomCore(devKey: string, split: { track: number; pattern: number }, scope: string, core: string): string | null {
  const most = capacity(devKey, split)[scope]
  for (const [v] of [...spec(devKey).coreFixed].sort((a, b) => cmpVersions(a[0], b[0])))
    if (cmpVersions(v, core) > 0 && capacity(devKey, split, v)[scope] === most) return v
  return null
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

/** A mod document's "storage" -> the syms of its lockable fields, in order. parseDecls checks the rest. */
export function parseLocks(m: Record<string, any>): string[] {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return st.filter((f: unknown) => isDict(f) && f.lock === true).map((f: Record<string, any>) => f.sym)
}

/** A mod document's "storage" -> the syms of its fields that are LFO destinations, in order. */
export function parseLfos(m: Record<string, any>): string[] {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return st.filter((f: unknown) => isDict(f) && f.lfo === true).map((f: Record<string, any>) => f.sym)
}

/** A mod document's "storage" -> the syms of its fields declared "smooth": false, in order. */
export function parseRough(m: Record<string, any>): string[] {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return st.filter((f: unknown) => isDict(f) && f.smooth === false).map((f: Record<string, any>) => f.sym)
}

/** A mod document's "storage" -> the syms of its fields declared "mod": true (velocity and aftertouch destinations), in
 * order. */
export function parseRoutes(m: Record<string, any>): string[] {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return st.filter((f: unknown) => isDict(f) && f.mod === true).map((f: Record<string, any>) => f.sym)
}

/** A mod document's "storage" -> the syms of its fields declared "track": "cv" (core-a4 0.5), in order. */
export function parseCvs(m: Record<string, any>): string[] {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return st.filter((f: unknown) => isDict(f) && f.track === 'cv').map((f: Record<string, any>) => f.sym)
}

/** A mod document's "storage" -> the syms of its fields with a CC, in order. */
export function parseCcs(m: Record<string, any>): string[] {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return st.filter((f: unknown) => isDict(f) && f.cc === true).map((f: Record<string, any>) => f.sym)
}

/** A mod document's "storage" -> {sym: init} of its fields with an init other than 0. parseDecls checks the rest. */
export function parseInits(m: Record<string, any>): Map<string, number> {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return new Map(st.filter((f: unknown) => isDict(f) && f.init).map((f: Record<string, any>) => [f.sym, f.init]))
}

/** A mod document's "storage" -> the syms of its fields declared "handle": true, in order. store.parse_handles. */
export function parseHandles(m: Record<string, any>): string[] {
  const st = Object.hasOwn(m, 'storage') && Array.isArray(m.storage) ? m.storage : []
  return st.filter((f: unknown) => isDict(f) && f.handle === true).map((f: Record<string, any>) => f.sym)
}

/** A mod document's "storage" -> [sym, scope, bits][]. */
export function parseDecls(m: Record<string, any>): [string, string, number][] {
  const out: [string, string, number][] = []
  const seen = new Set<string>()
  const st = Object.hasOwn(m, 'storage') ? m.storage : []
  if (!Array.isArray(st)) throw new ModError('"storage" is a list of fields')
  for (const f of st) {
    if (!isDict(f) || Object.keys(f).some(k => !['sym', 'scope', 'bits', 'lock', 'cc', 'lfo', 'smooth', 'mod', 'track', 'init', 'handle'].includes(k)))
      throw new ModError('a storage field is {"sym", "scope", "bits"}, and "lock" for a lockable one')
    for (const k of ['lock', 'cc', 'lfo', 'smooth', 'mod', 'handle'])
      if (Object.hasOwn(f, k) && typeof f[k] !== 'boolean')
        throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: "${k}" is true or false`)
    if (f.cc && !f.lock)
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: only a lockable field ("lock": true) can have a CC`)
    if (f.lfo && !f.lock)
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: only a lockable field ("lock": true) can be an LFO destination`)
    if (f.mod && !f.lock)
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: only a lockable field ("lock": true) can be a velocity or aftertouch destination`)
    if (Object.hasOwn(f, 'smooth') && !f.lock)
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: only a lockable field ("lock": true) is smoothed`)
    if (f.handle && f.lock)
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: a handle ("handle": true) is not a parameter: it cannot be lockable`)
    if (f.handle && f.init)
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: a handle ("handle": true) has no init: 0 is none`)
    if (f.lock && !LOCK_SCOPES.includes(f.scope))
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: only ${LOCK_SCOPES.join(' and ')} fields can be lockable`)
    if (Object.hasOwn(f, 'track') && (f.track !== 'cv' || f.scope !== 'kit' || !f.lock))
      throw new ModError(`storage field ${repr(Object.hasOwn(f, 'sym') ? f.sym : null)}: "track" is "cv", on a lockable kit field (the CV track's)`)
    const sym = Object.hasOwn(f, 'sym') ? f.sym : null
    const scope = Object.hasOwn(f, 'scope') ? f.scope : null
    const bits = Object.hasOwn(f, 'bits') ? f.bits : null
    if (typeof sym !== 'string' || !sym || !/^[A-Za-z_][A-Za-z0-9_]*$/.test(sym))
      throw new ModError(`storage field ${repr(sym)}: "sym" is a C name`)
    if (!(SCOPES as readonly unknown[]).includes(scope))
      throw new ModError(`storage field ${sym}: scope ${repr(scope)} (${SCOPES.join(', ')})`)
    if (!whole(bits) || !(bits >= 1 && bits <= MAX_BITS))
      throw new ModError(`storage field ${sym}: bits ${repr(bits)} (1 to ${MAX_BITS})`)
    if (Object.hasOwn(f, 'init') && (!whole(f.init) || !(f.init >= 0 && f.init < 2 ** bits)))
      throw new ModError(`storage field ${sym}: init ${repr(f.init)} (0 to ${2 ** bits - 1}, what its ${bits} bits hold)`)
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
    what.push([...lock[k]].sort(byField).map(f => [f.scope, f.offset, f.bits, f.mod, f.sym, ...(f.init ? [f.init] : [])]))
  if (lock.codes?.length || lock.freed_codes?.length) {   // a lockfile with no codes keeps its ID
    const nc = lock.next_code
    if (isDict(nc)) {                                        // codes per track kind (core-a4 0.5): the kind counts
      const ks = kinds(lock.device)
      what.push([ks.map(k => nc[k]), ...(['codes', 'freed_codes'] as const).flatMap(k =>
        [...lock[k]!].sort((a, b) => ks.indexOf(a.kind!) - ks.indexOf(b.kind!) || a.code - b.code).map(c => [c.code, c.mod, c.sym, c.kind]))])
    } else
      what.push([nc, ...(['codes', 'freed_codes'] as const).flatMap(k =>
        [...lock[k]!].sort((a, b) => a.code - b.code).map(c => [c.code, c.mod, c.sym]))])
  }
  return parseInt(sha(new TextEncoder().encode(pyDumps(what))).slice(0, 8), 16) || 1
}

export function fresh(devKey: string): Lock {
  const sp = spec(devKey)
  const lock: Lock = {
    elekloader_store: 1, device: devKey, id: '', ancestors: [], split: { ...sp.split },
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
      const hasInit = isDict(f) && Object.hasOwn(f, 'init')
      if (!isDict(f) || !sameKeys(f, hasInit ? ['mod', 'sym', 'scope', 'offset', 'bits', 'init'] : ['mod', 'sym', 'scope', 'offset', 'bits'])
        || typeof f.mod !== 'string'
        || typeof f.sym !== 'string' || !(SCOPES as readonly unknown[]).includes(f.scope) || !whole(f.offset) || f.offset < 0
        || !whole(f.bits) || !(f.bits >= 1 && f.bits <= MAX_BITS)
        || (hasInit && (v < 2 || !whole(f.init) || !(f.init > 0 && f.init < 2 ** f.bits))))
        throw new ModError(`lockfile: a bad entry in "${k}"`)
      if (k === 'fields' && !(f.offset >= 0 && f.offset <= lock.next[f.scope] - f.bits))
        throw new ModError(`lockfile: field ${f.sym} of ${f.mod} lies past its scope's used bits`)
    }
  }
  if (['codes', 'freed_codes', 'next_code'].some(k => Object.hasOwn(lock, k))) {
    const [first, last] = codeRange(devKey)
    const ks = kinds(devKey)
    const nc = lock.next_code
    if (ks.length) {                            // one range a track kind (core-a4 0.5)
      if (!isDict(nc) || !sameKeys(nc, ks) || !ks.every(k => whole(nc[k]) && nc[k] >= first && nc[k] <= last + 1))
        throw new ModError(`lockfile: "next_code" ${repr(Object.hasOwn(lock, 'next_code') ? nc : null)}`)
    } else if (!whole(nc) || !(nc >= first && nc <= last + 1))
      throw new ModError(`lockfile: "next_code" ${repr(Object.hasOwn(lock, 'next_code') ? nc : null)}`)
    const seen = new Set<string>()
    for (const k of ['codes', 'freed_codes']) {
      const cs = lock[k]
      if (!Array.isArray(cs)) throw new ModError(`lockfile: "${k}" is a list`)
      for (const c of cs) {
        if (!isDict(c) || !sameKeys(c, ks.length ? ['mod', 'sym', 'code', 'kind'] : ['mod', 'sym', 'code']) || typeof c.mod !== 'string'
          || typeof c.sym !== 'string' || (ks.length && !ks.includes(c.kind)) || !whole(c.code)
          || !(c.code >= first && c.code < (ks.length ? (nc as Record<string, number>)[c.kind] : (nc as number)))
          || seen.has(`${c.kind}\0${c.code}`))
          throw new ModError(`lockfile: a bad entry in "${k}"`)
        seen.add(`${c.kind}\0${c.code}`)
      }
    }
    const placed = new Map((lock.fields as Field[]).filter(f => LOCK_SCOPES.includes(f.scope)).map(f => [f.mod + '\0' + f.sym, f.scope]))
    if ((lock.codes as Code[]).some(c => !placed.has(c.mod + '\0' + c.sym)))
      throw new ModError('lockfile: a code for a field that is not a placed sound or kit field')
    if (ks.length && (lock.codes as Code[]).some(c => (placed.get(c.mod + '\0' + c.sym) === 'sound') !== (c.kind === 'synth')))
      throw new ModError("lockfile: a code of a kind its field's scope does not have")
  }
  if (Object.hasOwn(lock, 'ccs') || Object.hasOwn(lock, 'freed_ccs')) {
    const pool = spec(devKey).ccs ?? [], seen = new Set<number>()
    for (const k of ['ccs', 'freed_ccs']) {
      const cs = lock[k]
      if (!Array.isArray(cs)) throw new ModError(`lockfile: "${k}" is a list`)
      for (const c of cs) {
        if (!isDict(c) || !sameKeys(c, ['mod', 'sym', 'cc']) || typeof c.mod !== 'string' || typeof c.sym !== 'string'
          || !whole(c.cc) || !pool.includes(c.cc) || seen.has(c.cc))
          throw new ModError(`lockfile: a bad entry in "${k}"`)
        seen.add(c.cc)
      }
    }
    const kits = new Set((lock.fields as Field[]).filter(f => f.scope === 'kit').map(f => f.mod + '\0' + f.sym))
    const fxTaken = spec(devKey).fxCcsTaken ?? []
    if ((lock.ccs as Cc[]).some(c => kits.has(c.mod + '\0' + c.sym) && fxTaken.includes(c.cc)))
      throw new ModError('lockfile: a kit field with a CC stock uses on the FX channel')
    const coded = new Set(((lock.codes ?? []) as Code[]).map(c => c.mod + '\0' + c.sym))
    if ((lock.ccs as Cc[]).some(c => !coded.has(c.mod + '\0' + c.sym)))
      throw new ModError('lockfile: a CC for a field that has no lock code')
  }
  checkKnobs(lock)
}

function checkKnobs(lock: Record<string, any>): void {
  if (!['knobs', 'freed_knobs', 'next_knob'].some(k => Object.hasOwn(lock, k))) return
  const nk = lock.next_knob
  if (!whole(nk) || !(nk >= 0 && nk <= KNOB_TOP + 1)) throw new ModError(`lockfile: "next_knob" ${repr(nk)}`)
  const seen = new Set<string>()
  for (const k of ['knobs', 'freed_knobs']) {
    const ks = lock[k]
    if (!Array.isArray(ks)) throw new ModError(`lockfile: "${k}" is a list`)
    for (const r of ks) {
      if (!isDict(r) || !sameKeys(r, ['mod', 'first', 'count']) || typeof r.mod !== 'string' || !whole(r.first)
        || !whole(r.count) || r.count < 1 || r.first < 0 || r.first + r.count > nk || (k === 'knobs' && seen.has(r.mod)))
        throw new ModError(`lockfile: a bad entry in "${k}"`)
      if (k === 'knobs') seen.add(r.mod)
    }
  }
  const runs = (lock.knobs as Knob[]).map(r => [r.first, r.first + r.count - 1]).sort((a, b) => a[0] - b[0] || a[1] - b[1])
  if (runs.some((r, i) => i && r[0] <= runs[i - 1][1])) throw new ModError("lockfile: two mods' knob ids overlap")
}

/** Give each mod in want (mod id -> count) a run of knob ids from first up, in lock (changed in place; {} for a build
 * without a lockfile), past the runs in fixed ([first, last]: ids mods' entries name themselves). Not part of the
 * layout ID. -> [mod id -> its first id, notes]. Throws ModError. store.knobs. */
export function knobs(lock: Partial<Lock>, want: Map<string, number>, first: number, fixed0: [number, number][] = [],
  top: number = KNOB_TOP): [Map<string, number>, string[]] {
  const had = lock.knobs ?? [], freed0 = lock.freed_knobs ?? []
  if (!want.size && !had.length && !freed0.length) return [new Map(), []]
  const fixed = fixed0.filter(([, b]) => b >= first)
  let nxt = Math.max(first, lock.next_knob ?? first)
  const notes: string[] = [], runs: Knob[] = []
  let freed: Knob[] = freed0.map(r => ({ ...r }))
  const clash = (a: number, n: number, taken: [number, number][]) => taken.filter(([x, y]) => a <= y && x <= a + n - 1).map(([, y]) => y)
  const span = (r: Knob): [number, number] => [r.first, r.first + r.count - 1]
  for (const r of had) {
    if (want.get(r.mod) === r.count && !clash(r.first, r.count, fixed).length) { runs.push({ ...r }); continue }
    freed.push({ ...r })
    if (!want.has(r.mod)) notes.push(`${r.mod}: knob ids ${r.first}..${r.first + r.count - 1} freed, and kept for it until the unused ids run out`)
  }
  const placed = new Set(runs.map(r => r.mod))
  for (const mod of [...want.keys()].filter(m => !placed.has(m)).sort(cmpStr)) {
    const n = want.get(mod)!
    let taken = [...fixed, ...runs.map(span), ...freed.filter(r => r.mod !== mod).map(span)]
    let at: number | null = null
    for (const r of freed.filter(r => r.mod === mod).reverse())
      if (r.first + n - 1 <= top && !clash(r.first, n, taken).length) { at = r.first; break }
    freed = freed.filter(r => r.mod !== mod)
    if (at !== null) notes.push(`${mod}: back with knob ids ${at}..${at + n - 1}`)
    else {
      for (const again of [false, true]) {
        if (again) {                       // the never-used ids are gone: give the freed runs out again
          if (!freed.length) { at = null; break }
          notes.push(`knob ids freed by ${[...new Set(freed.map(r => r.mod))].sort(cmpStr).join(', ')} given out again`)
          freed = []
          taken = [...fixed, ...runs.map(span)]
        }
        let a = again ? first : nxt
        for (;;) {
          const hit = clash(a, n, taken)
          if (!hit.length) break
          a = Math.max(...hit) + 1
        }
        at = a
        if (a + n - 1 <= top) break
        at = null
      }
      if (at === null) throw new ModError(`${mod} asks for ${n} knob ids, and ids ${first}..${top} have no run that long free`)
      notes.push(`${mod}: knob ids ${at}..${at + n - 1}`)
    }
    runs.push({ mod, first: at, count: n })
    nxt = Math.max(nxt, at + n)
  }
  lock.knobs = runs.sort((a, b) => a.first - b.first)
  lock.freed_knobs = freed
  lock.next_knob = Math.max(nxt, ...[...runs, ...freed].map(r => r.first + r.count))
  return [new Map(runs.map(r => [r.mod, r.first])), notes]
}

const bump = (g: number) => (g >= 255 ? 1 : g + 1)
const byModSym = (a: { mod: string; sym: string }, b: { mod: string; sym: string }) =>
  a.mod < b.mod ? -1 : a.mod > b.mod ? 1 : a.sym < b.sym ? -1 : a.sym > b.sym ? 1 : 0
const ceilDiv = (a: number, b: number) => Math.floor((a + b - 1) / b)

/** lock: a lockfile, or null for a new one; decls: [mod id, sym, scope, bits]; locks: [mod id, sym] of the lockable
 * fields; ccs: those of them with a CC; inits: [mod id, sym, init] of the fields with an init other than 0 (core-ar2
 * 0.10); cvs: those declared "track": "cv" (core-a4 0.5). -> [the new lockfile, notes]. The lockfile given is not
 * changed. store.allocate. */
export function allocate(lock0: Lock | null, decls: Decl[], devKey: string, core: string | null = null,
  locks: [string, string][] = [], ccs: [string, string][] = [], inits: [string, string, number][] = [],
  cvs: [string, string][] = []): [Lock, string[]] {
  const notes: string[] = []
  const initOf = new Map(inits.filter(([, , v]) => v).map(([m, sy, v]) => [m + '\0' + sy, v]))
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
  const want = new Map<string, [string, number, number]>()
  const keyOf = (mid: string, sym: string) => mid + '\0' + sym
  for (const [mid, sym, scope, bits] of decls) {
    if ([...want.keys()].some(k => k.split('\0')[1] === sym)) throw new ModError(`the storage field ${sym} is declared by two mods`)
    want.set(keyOf(mid, sym), [scope, bits, initOf.get(keyOf(mid, sym)) ?? 0])
  }
  const kept: Field[] = [], gone: Field[] = []
  for (const f of lock.fields) {
    const w = want.get(keyOf(f.mod, f.sym))
    if (w && w[0] === f.scope && w[1] === f.bits && w[2] === (f.init ?? 0)) kept.push(f)
    else gone.push(f)
  }
  const have = new Set(kept.map(f => keyOf(f.mod, f.sym)))
  const added: (Omit<Field, 'offset'> & { offset: number | null })[] = [...want.keys()]
    .map(k => { const [mod, sym] = k.split('\0'); return { mod, sym } })
    .sort(byModSym)
    .filter(({ mod, sym }) => !have.has(keyOf(mod, sym)))
    .map(({ mod, sym }) => {
      const [scope, bits, init] = want.get(keyOf(mod, sym))!
      return init ? { mod, sym, scope, offset: null, bits, init } : { mod, sym, scope, offset: null, bits }
    })
  // a freed field back with the same scope and width takes its old place: no field was given those bits since, so
  // its values are still there
  const back = new Set<string>()
  for (const f of added) {
    const same = lock.freed.filter(g => g.mod === f.mod && g.sym === f.sym && g.scope === f.scope && g.bits === f.bits
      && (g.init ?? 0) === (f.init ?? 0))
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
      throw new ModError(`the mods need ${need(s)} bits of ${s} storage; the ${devKey} has ${cap[s]}${more > cap[s] ? ` with core ${core} (${more} with core ${roomCore(devKey, lock.split, s, core as string)})` : ''}`)
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
    const w = want.get(keyOf(f.mod, f.sym))
    if (w || repacked.has(f.scope))
      notes.push(`${f.mod} ${f.sym} (${f.scope}): ${!w ? 'no longer in the build' : w[0] === f.scope && w[1] === f.bits ? 'changed its init' : 'changed size'}, its data is dropped`)
    else notes.push(`${f.mod} ${f.sym} (${f.scope}): no longer in the build; its bits are kept, so it gets its values back if it returns`)
  }
  for (const f of added) {
    if (back.has(keyOf(f.mod, f.sym)) && !repacked.has(f.scope))
      notes.push(`${f.mod} ${f.sym}: back at bit ${f.offset} of ${f.scope} storage, with the values it had`)
    else notes.push(`${f.mod} ${f.sym}: ${f.bits} bits of ${f.scope} storage at bit ${f.offset}`)
  }
  lock.fields = fields.sort((a, b) => SCOPE_NUM[a.scope] - SCOPE_NUM[b.scope] || a.offset - b.offset)
  // format 2 from the first init: an older elekloader refuses it rather than reading the inits' fields as 0
  if (lock.fields.some(f => f.init) || lock.freed.some(f => f.init)) lock.elekloader_store = 2
  const scopeOf = new Map(fields.map(f => [keyOf(f.mod, f.sym), f.scope]))
  const cv = new Set(cvs.map(([m, sy]) => keyOf(m, sy)))
  const recoded = setCodes(lock, new Map(locks.map(([m, sy]) => [keyOf(m, sy), kindOf(devKey, scopeOf.get(keyOf(m, sy)), cv.has(keyOf(m, sy)))])),
    new Set(kept.map(f => keyOf(f.mod, f.sym))),
    new Set(added.filter(f => back.has(keyOf(f.mod, f.sym))).map(f => keyOf(f.mod, f.sym))), notes, devKey)
  setCcs(lock, new Set(ccs.map(([m, sy]) => keyOf(m, sy))), spec(devKey).ccs ?? [], notes,
    new Set(fields.filter(f => f.scope === 'kit').map(f => keyOf(f.mod, f.sym))), spec(devKey).fxCcsTaken ?? [])
  if (isNew) {
    lock.id = idStr(layoutId(lock))
    notes.unshift(`a new lockfile, layout ID ${lock.id}: projects and sounds saved by builds with other layouts load their mod data at defaults`)
  } else if (gone.length || added.length || repacked.size || recoded) {   // a repack alone (a field past an older core's room) moves fields too
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

/** Give each lockable field (keys mod\0sym in want, to its kind: null where the device has no kinds) its lock code, in
 * lock (changed in place). kept: the fields that kept their place; back: freed fields that came back to theirs.
 * -> whether any code changed. store._codes. */
function setCodes(lock: Lock, want: Map<string, string | null>, kept: Set<string>, back: Set<string>, notes: string[], devKey: string): boolean {
  const had = lock.codes ?? [], hadFreed = lock.freed_codes ?? []
  if (!want.size && !had.length && !hadFreed.length) return false   // a lockfile that never had a lockable field stays as it was
  const [first, last] = codeRange(devKey)
  const ks = kinds(devKey)
  const key = (c: { mod: string; sym: string }) => c.mod + '\0' + c.sym
  const codes: Code[] = [], freed: Code[] = hadFreed.map(c => ({ ...c }))
  const nxt = new Map<string | null, number>()
  if (ks.length) {
    const nc = lock.next_code as Record<string, number> | undefined
    for (const k of ks) nxt.set(k, nc ? nc[k] : first)
  } else nxt.set(null, (lock.next_code as number | undefined) ?? first)
  const of = (k: string | null | undefined) => (k ? ` (${KIND_NAMES[k].slice(4)})` : '')
  let changed = false
  for (const c of had) {
    const k = key(c)
    if (want.has(k) && kept.has(k) && (c.kind ?? null) === want.get(k)) codes.push({ ...c })
    else {
      freed.push({ ...c })
      changed = true
      notes.push(`${c.mod} ${c.sym}: lock code ${c.code}${of(c.kind)} ${!want.has(k) || !kept.has(k) ? 'kept for it while it is out of the build' : 'freed'}`)
    }
  }
  const have = new Set(codes.map(key))
  const todo = [...want.keys()].filter(k => !have.has(k)).map(k => { const [mod, sym] = k.split('\0'); return { mod, sym } }).sort(byModSym)
  for (const { mod, sym } of todo) {
    const k = mod + '\0' + sym
    const kind = want.get(k)!
    const mine = freed.filter(c => (c.kind ?? null) === kind)
    const same = mine.filter(c => key(c) === k)
    let c: Code
    if (same.length && back.has(k)) {          // back in its old place: its old code, with its rows
      c = same[same.length - 1]
      freed.splice(freed.indexOf(c), 1)
      notes.push(`${mod} ${sym}: back with lock code ${c.code}${of(kind)}`)
    } else if (nxt.get(kind)! <= last) {
      c = { mod, sym, code: nxt.get(kind)! }
      if (kind) c.kind = kind
      nxt.set(kind, nxt.get(kind)! + 1)
      notes.push(`${mod} ${sym}: lock code ${c.code}${of(kind)}`)
    } else if (mine.length) {                   // the never-used codes are gone: the oldest freed one
      const old = mine[0]
      freed.splice(freed.indexOf(old), 1)
      c = { mod, sym, code: old.code }
      if (kind) c.kind = kind
      notes.push(`${mod} ${sym}: lock code ${c.code}${of(kind)} (was ${old.mod} ${old.sym}'s)`)
    } else throw new ModError(`the mods have more than ${last - first + 1} lockable parameters${kind ? ' on ' + KIND_NAMES[kind] : ''}`)
    for (const d of freed.filter(d => key(d) === k)) freed.splice(freed.indexOf(d), 1)   // a field has one code
    codes.push(c)
    changed = true
  }
  lock.codes = codes.sort((a, b) => (ks.length ? ks.indexOf(a.kind!) - ks.indexOf(b.kind!) : 0) || a.code - b.code)
  lock.freed_codes = freed
  lock.next_code = ks.length ? Object.fromEntries(ks.map(k => [k, nxt.get(k)!])) : nxt.get(null)!
  return changed
}

/** Give each field (keys mod\0sym in want) its CC from pool, in lock (changed in place); a field in kits (the FX
 * track's) none in fxTaken. Not part of the layout ID: a CC says nothing of where data is. store._ccs. */
function setCcs(lock: Lock, want: Set<string>, pool: number[], notes: string[], kits: Set<string> = new Set(), fxTaken: number[] = []): void {
  const had = lock.ccs ?? [], hadFreed = lock.freed_ccs ?? []
  if (!want.size && !had.length && !hadFreed.length) return
  const key = (c: { mod: string; sym: string }) => c.mod + '\0' + c.sym
  const ccs: Cc[] = [], freed: Cc[] = hadFreed.map(c => ({ ...c }))
  for (const c of had) {
    if (want.has(key(c)) && !(kits.has(key(c)) && fxTaken.includes(c.cc))) ccs.push({ ...c })
    else {
      freed.push({ ...c })
      notes.push(`${c.mod} ${c.sym}: CC ${c.cc} freed, and kept for it until the unused CCs run out`)
    }
  }
  const used = new Set([...ccs, ...freed].map(c => c.cc))
  const unused = pool.filter(cc => !used.has(cc))
  const have = new Set(ccs.map(key))
  const todo = [...want].filter(k => !have.has(k)).map(k => { const [mod, sym] = k.split('\0'); return { mod, sym } }).sort(byModSym)
  for (const { mod, sym } of todo) {
    const k = mod + '\0' + sym
    const ok = (cc: number) => !(kits.has(k) && fxTaken.includes(cc))
    const same = freed.filter(c => key(c) === k && ok(c.cc))
    const fresh = unused.filter(ok), older = freed.filter(c => ok(c.cc))
    let c: Cc
    if (same.length) {
      c = same[same.length - 1]
      freed.splice(freed.indexOf(c), 1)
      notes.push(`${mod} ${sym}: back with CC ${c.cc}`)
    } else if (fresh.length) {
      unused.splice(unused.indexOf(fresh[0]), 1)
      c = { mod, sym, cc: fresh[0] }
      notes.push(`${mod} ${sym}: CC ${c.cc}`)
    } else if (older.length) {                  // the never-used CCs are gone: the one freed longest ago
      const old = older[0]
      freed.splice(freed.indexOf(old), 1)
      c = { mod, sym, cc: old.cc }
      notes.push(`${mod} ${sym}: CC ${c.cc} (was ${old.mod} ${old.sym}'s)`)
    } else {
      notes.push(`${mod} ${sym}: no CC left (all ${pool.length} are taken); it is on MIDI by its NRPN only`)
      continue
    }
    ccs.push(c)
  }
  lock.ccs = ccs.sort((a, b) => a.cc - b.cc)
  lock.freed_ccs = freed
}

/** -> the placed fields with an init, in their slots' order (slot 1 first). Throws ModError. store._inited. */
function inited(lock: Lock): Field[] {
  const out = lock.fields.filter(f => f.init)
  if (out.length > MAX_INITS) throw new ModError(`the mods give ${out.length} storage fields an init; the core keeps at most ${MAX_INITS}`)
  return out
}

/** -> {mod\0sym: descriptor} of the placed fields: scope in bits 28..31, width in 22..27, the init's slot in 16..21
 * (0: none), the offset in 0..15. store.descriptors. */
export function descriptors(lock: Lock): Map<string, number> {
  const slot = new Map(inited(lock).map((f, i) => [f.mod + '\0' + f.sym, i + 1]))
  return new Map(lock.fields.map(f => [f.mod + '\0' + f.sym,
    ((SCOPE_NUM[f.scope] << 28) | (f.bits << 22) | ((slot.get(f.mod + '\0' + f.sym) ?? 0) << 16) | f.offset) >>> 0]))
}

/** -> the table core_field_inits' entries: each init, 4 bytes, by slot. store.init_entries. */
export function initEntries(lock: Lock): Uint8Array[] {
  return inited(lock).map(f => { const out = new Uint8Array(4); new DataView(out.buffer).setUint32(0, f.init!); return out })
}

/** -> the table core_handles' entries: the descriptor of each placed field in handles ([mod, sym]), 4 bytes each, in the
 * lockfile's order. store.handle_entries. */
export function handleEntries(lock: Lock, handles: [string, string][] = []): Uint8Array[] {
  const desc = descriptors(lock), want = new Set(handles.map(([m, sy]) => m + '\0' + sy))
  return lock.fields.filter(f => want.has(f.mod + '\0' + f.sym))
    .map(f => { const out = new Uint8Array(4); new DataView(out.buffer).setUint32(0, desc.get(f.mod + '\0' + f.sym)!); return out })
}

/** -> [{name: value}, the IDs' table entries] for the linker. store.symbols. */
export function symbols(lock: Lock): [Map<string, number>, Uint8Array[]] {
  const g = lock.generation, sp = lock.split
  const out = new Map<string, number>()
  out.set('__store_gen', ((g.sound << 24) | (g.kit << 16) | (g.global << 8) | g.track) >>> 0)
  out.set('__store_gen2', ((g.pattern << 24) | (g.project << 16) | (sp.track << 8) | sp.pattern) >>> 0)
  const desc = descriptors(lock)
  for (const f of lock.fields) out.set(f.sym, desc.get(f.mod + '\0' + f.sym)!)
  const ids = [idVal(lock.id, 'id'), ...lock.ancestors.map(a => idVal(a, 'ancestor'))]
  return [out, ids.map(i => new Uint8Array([i >>> 24, (i >>> 16) & 255, (i >>> 8) & 255, i & 255]))]
}

/** -> the table core_lock_codes' entries: {field descriptor, code}, 8 bytes each, by code; the code of a field in lfos
 * ([mod, sym]) has LFO_FLAG set, of one in rough SMOOTH_OFF, of one in routes ROUTE_FLAG, of a CV field CV_FLAG (by kind,
 * then code, where codes have kinds). store.code_entries. */
export function codeEntries(lock: Lock, lfos: [string, string][] = [], rough: [string, string][] = [], routes: [string, string][] = []): Uint8Array[] {
  const desc = descriptors(lock)
  const lfo = new Set(lfos.map(([m, sy]) => m + '\0' + sy))
  const jump = new Set(rough.map(([m, sy]) => m + '\0' + sy))
  const route = new Set(routes.map(([m, sy]) => m + '\0' + sy))
  return (lock.codes ?? []).map(c => {
    const out = new Uint8Array(8), dv = new DataView(out.buffer)
    dv.setUint32(0, desc.get(c.mod + '\0' + c.sym)!)
    dv.setUint32(4, c.code | (lfo.has(c.mod + '\0' + c.sym) ? LFO_FLAG : 0) | (jump.has(c.mod + '\0' + c.sym) ? SMOOTH_OFF : 0) | (route.has(c.mod + '\0' + c.sym) ? ROUTE_FLAG : 0)
      | (c.kind === 'cv' ? CV_FLAG : 0))
    return out
  })
}

/** -> the table core_lock_ccs' entries: {field descriptor, CC}, 8 bytes each, by CC. store.cc_entries. */
export function ccEntries(lock: Lock): Uint8Array[] {
  const desc = descriptors(lock)
  return (lock.ccs ?? []).map(c => {
    const out = new Uint8Array(8), dv = new DataView(out.buffer)
    dv.setUint32(0, desc.get(c.mod + '\0' + c.sym)!)
    dv.setUint32(4, c.cc)
    return out
  })
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
