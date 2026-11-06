// SPDX-License-Identifier: GPL-3.0-or-later
// The DN2's observed boot-record subset. Paired with elekloader/sharc.py; no external parser code.
import { equal, fromHex, sha } from './bytes.ts'
import type { SharcProfile } from './devices.ts'
import { ModError, isDict, pyEq } from './elemod.ts'
import { PyFloat, TypeError_, pyType, repr, str } from './py.ts'

export const LOAD = 0x0001, FILL = 0x0101, FIRST = 0x5001, INIT = 0x0801, FINAL = 0x8001
const KINDS = [LOAD, FILL, FIRST, INIT, FINAL]
const uint32 = (n: number): boolean => Number.isInteger(n) && n >= 0 && n <= 0xffffffff

/** Canonical loader-byte interval. Size is bytes in every address view.
 * Allocation excludes protected ranges; an existing record may only be mapped. */
export function byteSpan(profile: SharcProfile, address: number, size: number, unit = 'loader-byte', checkProtected = true): [number, number] {
  if (!uint32(address) || !uint32(size) || !size)
    throw new ModError('SHARC address and nonempty byte count must be uint32')
  let intervals: [number, number, number][]
  if (unit === 'loader-byte') {
    intervals = profile.l1Aliases.map(([lo, hi, base]) => [base, base + hi - lo, base])
    intervals.push(...[profile.l2Bytes, profile.ddrBytes].map(([lo, hi]): [number, number, number] => [lo, hi, lo]))
  } else if (unit === 'core-byte') {
    intervals = [...profile.l1Aliases]
    intervals.push(...[profile.l2Bytes, profile.ddrBytes].map(([lo, hi]): [number, number, number] => [lo, hi, lo]))
  } else if (unit === 'pc16') {
    intervals = profile.l1Aliases.map(([lo, hi, base]) => [lo / 2, hi / 2, base])
    intervals.push(profile.l2Pc16)
  } else throw new ModError(`unsupported SHARC address unit: ${unit}`)
  const scale = unit === 'pc16' ? 2 : 1
  for (const [lo, hi, base] of intervals) {
    const start = base + (address - lo) * scale, end = start + size
    if (lo <= address && address < hi && end <= base + (hi - lo) * scale && end <= 0x100000000) {
      if (checkProtected && profile.protected.some(([begin, stop]) => start < stop && begin < end))
        throw new ModError('SHARC byte span overlaps a protected region')
      return [start, end]
    }
  }
  throw new ModError('SHARC byte span is outside the supported address map')
}

/** Map a loader byte address to an instruction pointer, without truncation. */
export function pc16(profile: SharcProfile, address: number): number {
  byteSpan(profile, address, 2)
  const intervals: [number, number, number][] = profile.l1Aliases.map(([lo, hi, base]) => [lo / 2, hi / 2, base])
  for (const [lo, hi, base] of [...intervals, profile.l2Pc16]) {
    if (base <= address && address < base + 2 * (hi - lo) && (address - base) % 2 === 0)
      return lo + (address - base) / 2
  }
  throw new ModError('SHARC callback has no aligned PC16 address')
}

export type Record = { offset: number; flags: number; target: number; count: number; argument: number; raw: Uint8Array }

/** Read an ordered stream without normalizing headers, loads or overlaps. */
export function parse(stream: Uint8Array, profile: SharcProfile): Record[] {
  if (stream.length > profile.decodedMax) throw new ModError('SHARC decoded stream exceeds the profile limit')
  const records: Record[] = [], firsts: [number, number][] = []
  let offset = 0, active: number | null = null, final = false
  const view = new DataView(stream.buffer, stream.byteOffset, stream.byteLength)
  while (offset < stream.length) {
    const fail = (message: string): never => { throw new ModError(`SHARC boot record at +0x${offset.toString(16)}: ${message}`) }
    if (stream.length - offset < 16) fail('truncated header')
    const header = stream.subarray(offset, offset + 16)
    const code = view.getUint32(offset, true), target = view.getUint32(offset + 4, true)
    const count = view.getUint32(offset + 8, true), argument = view.getUint32(offset + 12, true)
    if (code >>> 24 !== 0xad) fail('invalid header signature')
    if (header.reduce((a, b) => a ^ b, 0)) fail('invalid header checksum')
    const flags = code & 0xffff
    if (!KINDS.includes(flags)) fail(`unsupported flags 0x${flags.toString(16).padStart(4, '0')}`)
    const end = offset + 16 + (flags === LOAD ? count : 0)
    if (end > stream.length) fail('truncated payload')
    if (flags === LOAD || flags === FILL) {
      if (active === null) fail('load or fill outside a boot image')
      if (!count || target % 4 || count % 4) fail('load or fill address/count must be nonempty and four-byte aligned')
      if (flags === LOAD && argument) fail('load argument must be zero')
      try { byteSpan(profile, target, count, 'loader-byte', false) } catch (error) { fail((error as Error).message) }
    } else {
      if (count) fail('entry record requires zero count')
      try { byteSpan(profile, target, 2, 'pc16', false) } catch (error) { fail((error as Error).message) }
      if (flags === FIRST) {
        if (active !== null) fail('FIRST before the preceding boot image ends')
        active = target
        firsts.push([offset, argument])
      } else {
        if (active === null || target !== active || argument) fail('entry does not match its FIRST boot image')
        active = null
        if (flags === FINAL) {
          final = true
          if (end !== stream.length) fail('bytes after FINAL')
        }
      }
    }
    records.push({ offset, flags, target, count, argument, raw: Uint8Array.from(stream.subarray(offset, end)) })
    offset = end
  }
  if (!final) throw new ModError('SHARC boot stream has no FINAL record')
  for (const [index, [at, argument]] of firsts.entries()) {
    const expected = index + 1 < firsts.length ? firsts[index + 1][0] : stream.length
    if (at + 16 + argument !== expected)
      throw new ModError(`SHARC FIRST at +0x${at.toString(16)}: wrong next-boot-image distance`)
  }
  return records
}

function header(flags: number, target: number, count: number, argument: number): Uint8Array {
  const raw = new Uint8Array(16), view = new DataView(raw.buffer)
  for (const [i, n] of [(0xad000000 | flags) >>> 0, target, count, argument].entries()) view.setUint32(4 * i, n, true)
  raw[2] = raw.reduce((a, b) => a ^ b, 0)
  return raw
}

function join(chunks: Uint8Array[]): Uint8Array {
  const out = new Uint8Array(chunks.reduce((n, chunk) => n + chunk.length, 0))
  let offset = 0
  for (const chunk of chunks) { out.set(chunk, offset); offset += chunk.length }
  return out
}

/** One insertion before FINAL. Only the profile's qualified loader-byte ranges are eligible. */
export function insert(stream: Uint8Array, profile: SharcProfile,
  loads: [number, Uint8Array][] = [], fills: [number, number, number][] = [],
  patches: [number, Uint8Array, Uint8Array][] = []): Uint8Array {
  const records = parse(stream, profile)
  if (sha(stream) !== profile.decodedSha256) throw new ModError('SHARC stock DSP hash does not match the profile')
  const additions: [number, number, number, number, Uint8Array][] = [], spans: [number, number][] = []
  for (const [target, data] of loads) {
    if (!(data instanceof Uint8Array)) throw new ModError('SHARC load payload must be bytes')
    spans.push([target, data.length])
    additions.push([LOAD, target, data.length, 0, data])
  }
  for (const [target, count, word] of fills) {
    if (!uint32(word)) throw new ModError('SHARC fill word must be uint32')
    spans.push([target, count])
    additions.push([FILL, target, count, word, new Uint8Array()])
  }
  if (!additions.length && !patches.length) return Uint8Array.from(stream)
  const patched = Uint8Array.from(stream)
  for (const [target, old, replacement] of patches) {
    if (!(old instanceof Uint8Array) || !(replacement instanceof Uint8Array) || !old.length ||
      old.length !== replacement.length || target % 2 || old.length % 2)
      throw new ModError('SHARC patch needs equal nonempty instruction bytes')
    const [lo, hi] = byteSpan(profile, target, old.length, 'loader-byte', false)
    const qualified = profile.dataSites?.[target]
    if (!(qualified && equal(old, fromHex(qualified[0])!) && equal(replacement, fromHex(qualified[1])!)) &&
      !profile.patchRanges?.some(([start, end]) => start <= lo && hi <= end))
      throw new ModError('SHARC patch is outside the profile patch range')
    const overlaps = records.filter(r => (r.flags === LOAD || r.flags === FILL) && r.target < hi && lo < r.target + r.count)
    if (overlaps.length !== 1 || overlaps[0].flags !== LOAD || overlaps[0].target > lo || hi > overlaps[0].target + overlaps[0].count)
      throw new ModError('SHARC patch needs one unambiguous stock load')
    const at = overlaps[0].offset + 16 + lo - overlaps[0].target
    if (!patched.subarray(at, at + old.length).every((b, i) => b === old[i]))
      throw new ModError('SHARC patch stock instruction does not match')
    patched.set(replacement, at)
  }
  const occupied = records.filter(r => r.flags === LOAD || r.flags === FILL).map(r => byteSpan(profile, r.target, r.count, 'loader-byte', false))
  for (const [target, count] of spans) {
    const [lo, hi] = byteSpan(profile, target, count)
    if (target % 4 || count % 4) throw new ModError('SHARC new load or fill must be four-byte aligned')
    if (!profile.ownedRanges.some(([start, end]) => start <= lo && hi <= end))
      throw new ModError('SHARC new load or fill is outside the profile-owned ranges')
    if (occupied.some(([start, end]) => lo < end && start < hi))
      throw new ModError('SHARC new load or fill overlaps an existing load or fill')
    occupied.push([lo, hi])
  }
  const extra = additions.reduce((n, [, , , , data]) => n + 16 + data.length, 0)
  if (stream.length + extra > profile.decodedMax) throw new ModError('SHARC decoded stream exceeds the profile limit')
  const first = records.findLast(r => r.flags === FIRST)!
  if (first.argument + extra > 0xffffffff) throw new ModError('SHARC FIRST distance overflows uint32')
  const inserted = additions.flatMap(([flags, target, count, word, data]) => [header(flags, target, count, word), data])
  const result = join([patched.subarray(0, first.offset), header(FIRST, first.target, 0, first.argument + extra),
    patched.subarray(first.offset + 16, records.at(-1)!.offset), ...inserted, records.at(-1)!.raw])
  parse(result, profile)
  return result
}

export type SharcPayload = {
  segments: [number, 'code' | 'data', Uint8Array][]
  zero: [number, number][]
  entries: { [name: string]: number } | null
  stackBytes: number
  owner: [number, number]
}

/** Validate the narrow prelinked format-2 payload; offsets are owner bytes. */
export function modulePayload(doc: any, profile: SharcProfile | undefined, core = false): SharcPayload {
  if (!profile?.abi) throw new ModError('SHARC payload has no supported device ABI')
  if (!doc || typeof doc !== 'object' || Array.isArray(doc) ||
    Object.keys(doc).sort().join(',') !== 'abi,entries,segments,stack_bytes,zero')
    throw new ModError('SHARC payload needs exactly abi, segments, zero, entries, stack_bytes')
  const abi = core ? profile.abi : profile.l2Abi
  if (!abi || doc.abi !== abi) throw new ModError('SHARC ABI does not match the device profile')
  const owner = core ? profile.coreRange! : profile.l2AppRange!
  const size = owner[1] - owner[0]
  const spans: [number, number][] = [], segments: SharcPayload['segments'] = [], zero: [number, number][] = []
  if (!Array.isArray(doc.segments) || !doc.segments.length) throw new ModError('SHARC payload needs code segments')
  for (const item of doc.segments) {
    if (!item || typeof item !== 'object' || Array.isArray(item) || Object.keys(item).sort().join(',') !== 'hex,kind,offset_bytes')
      throw new ModError('SHARC segment needs exactly offset_bytes, kind, hex')
    const off = item.offset_bytes, kind = item.kind, h = item.hex
    if (!uint32(off) || (kind !== 'code' && kind !== 'data') || typeof h !== 'string' || h.length > size * 2 ||
      !/^(?:[0-9a-fA-F]{2})+$/.test(h)) throw new ModError('SHARC segment has invalid offset, kind or hex bytes')
    const data = Uint8Array.from(h.match(/../g)!.map((pair: string) => parseInt(pair, 16)))
    if (off % 4 || data.length % 4 || off + data.length > size)
      throw new ModError('SHARC segment is unaligned or exceeds its owner')
    spans.push([off, off + data.length]); segments.push([off, kind, data])
  }
  if (!segments.some(([, kind]) => kind === 'code')) throw new ModError('SHARC payload has no code')
  if (!Array.isArray(doc.zero)) throw new ModError('SHARC zero ranges must be an array')
  for (const item of doc.zero) {
    if (!item || typeof item !== 'object' || Array.isArray(item) || Object.keys(item).sort().join(',') !== 'offset_bytes,size_bytes')
      throw new ModError('SHARC zero range needs exactly offset_bytes, size_bytes')
    const off = item.offset_bytes, count = item.size_bytes
    if (!uint32(off) || !uint32(count) || !count || off % 4 || count % 4 || off + count > size)
      throw new ModError('SHARC zero range is unaligned or exceeds its owner')
    spans.push([off, off + count]); zero.push([off, count])
  }
  let at = core ? 12 : 0
  for (const [lo, hi] of spans.sort(([a], [b]) => a - b)) {
    if (lo !== at) throw new ModError('SHARC payload has a gap or overlap in its owner')
    at = hi
  }
  if (at !== size) throw new ModError('SHARC payload has a gap or overlap in its owner')
  const entries = doc.entries
  if (entries !== null) {
    const entryKeys = core ? 'hook_offset_bytes' : 'init_offset_bytes,render_offset_bytes'
    if (!entries || typeof entries !== 'object' || Array.isArray(entries) || Object.keys(entries).sort().join(',') !== entryKeys)
      throw new ModError('SHARC entries do not match core/mod role')
    for (const off of Object.values(entries))
      if (!uint32(off as number) || (off as number) % 2 || !segments.some(([start, kind, data]) =>
        kind === 'code' && start <= (off as number) && (off as number) < start + data.length))
        throw new ModError('SHARC entry is not in owned code')
  } else if (core) throw new ModError('SHARC core needs a hook entry')
  if (entries !== null && !core) {
    const guard = profile.codeGuardBytes!
    for (const [start, kind, data] of segments) {
      if (kind !== 'code') continue
      if (data.length < guard || !data.slice(-guard).every((b, i) => b === (i % 2 ? 0 : 1)))
        throw new ModError('SHARC mod code needs its profiled NOP read-ahead guard')
      if (Object.values(entries).some(off => start + data.length - guard <= (off as number) && (off as number) < start + data.length))
        throw new ModError('SHARC entry is in the instruction read-ahead guard')
    }
  }
  const stack = doc.stack_bytes
  if (!uint32(stack) || stack > profile.stackMax!)
    throw new ModError('SHARC stack contribution exceeds the profile limit')
  if (entries === null && stack) throw new ModError('inert SHARC payload must declare zero stack')
  return { segments, zero, entries, stackBytes: stack, owner }
}

// DN2 machine kernels (dn2-kernel-v1): machines-dn2's dispatcher and every mod's kernels, relocatable,
// placed anywhere in the profile's pool by placeKernels. sharc.py's kernel part.
export const KERNEL_ABI = 'dn2-kernel-v1'
export const DISPATCH = ['dn2_voice_hook.', 'dn2_filter_hook.', 'dn2_filter_capture.', 'dn2_voice_table.', 'dn2_filter_table.']
// Each kind's ids, as machines-dn2 stores them, and its table: SDK and mk2 synths share the voice table.
export const KERNEL_IDS: { [kind: string]: [number, number] } = { sdk: [5, 127], 'mk2-voice': [5, 31], 'mk2-filter': [6, 31] }
export const KERNEL_TABLE: { [kind: string]: 'voice' | 'filter' } = { sdk: 'voice', 'mk2-voice': 'voice', 'mk2-filter': 'filter' }
export const TABLE_IDS = 128
export const KNOBS_MAX = 15                  // ELEK_UI_KNOB_MAX
export const RAW_MAX = 32512                 // a knob's raw value, 0..127.0 in 8.8

export type KernelKind = 'sdk' | 'mk2-voice' | 'mk2-filter'
export type Kernel = { kind: KernelKind; id: number; entry: number; knobs?: [number, number][]; state_bytes?: number }
export type KernelPayload = {
  abi: string; code: Uint8Array; data: Uint8Array; codePc16: number; dataAddress: number; dataAlign: number
  relocs: ['code' | 'data', number, number, 'code' | 'data'][]; kernels: Kernel[]; exports: { [name: string]: ['code' | 'data', number] }
}

const int = (v: unknown): v is number => typeof v === 'number' && Number.isInteger(v)
const uint = (v: unknown): v is number => int(v) && uint32(v)
const int32 = (v: unknown): v is number => int(v) && v >= -0x80000000 && v <= 0x7fffffff
/** `v in d` for a dict d with string keys: Python refuses an unhashable v. */
function inKeys(v: unknown, keys: object): boolean {
  if (Array.isArray(v) || isDict(v)) throw new TypeError_(`unhashable type: '${pyType(v)}'`)
  return typeof v === 'string' && Object.hasOwn(keys, v)
}

function kernelHex(value: unknown, what: string): Uint8Array {
  if (typeof value !== 'string' || !/^(?:[0-9a-fA-F]{2})*$/.test(value)) throw new ModError(`${what} must be hex bytes`)
  return fromHex(value)!
}

/** Validate a relocatable kernel artifact (sdk.sharc.kernel_artifact or machine_artifact). sharc.kernel_payload. */
export function kernelPayload(doc: any, profile: SharcProfile | undefined): KernelPayload {
  if (!isDict(doc) || !profile || doc.abi !== profile.kernelAbi)
    throw new ModError(`SHARC kernel: abi must be ${KERNEL_ABI} on this device`)
  const keys = ['abi', 'code', 'code_pc16', 'data', 'data_address', 'data_align', 'exports', 'kernels', 'relocs']
  if (Object.keys(doc).sort().join(',') !== keys.join(','))
    throw new ModError('SHARC kernel needs exactly ' + keys.join(', '))
  const [code, data] = (['code', 'data'] as const).map(k => kernelHex(doc[k], 'SHARC kernel ' + k))
  if (!code.length || code.length % 2) throw new ModError('SHARC kernel: code must be whole 16-bit parcels')
  const align = doc.data_align
  if (!int(align) || ![4, 8, 16, 32, 64, 128].includes(align) || !uint(doc.code_pc16) || !uint(doc.data_address) ||
    doc.data_address % align) throw new ModError('SHARC kernel: invalid base or data alignment')
  const sizes = { code: code.length, data: data.length }, relocs: KernelPayload['relocs'] = [], taken = new Set<string>()
  for (const r of Array.isArray(doc.relocs) ? doc.relocs : [null]) {
    const ok = Array.isArray(r) && r.length === 4 && inKeys(r[0], sizes) && uint(r[1]) &&
      (r[0] === 'code' ? pyEq(r[2], 3) || pyEq(r[2], 4) : pyEq(r[2], 4)) && (r[3] === 'code' || r[3] === 'data') &&
      r[1] + Number(r[2] instanceof PyFloat ? r[2].value : r[2]) <= sizes[r[0] as 'code' | 'data']
    if (ok && !int(r[2])) throw new TypeError_(`'${pyType(r[2])}' object cannot be interpreted as an integer`)   // range(r[2])
    if (!ok || Array.from({ length: r[2] }, (_, i) => `${r[0]}:${r[1] + i}`).some(k => taken.has(k)))
      throw new ModError(`SHARC kernel: invalid relocation ${repr(r)}`)
    for (let i = 0; i < r[2]; i++) taken.add(`${r[0]}:${r[1] + i}`)
    relocs.push([r[0], r[1], r[2], r[3]])
  }
  const exports = doc.exports
  if (!isDict(exports) || Object.entries(exports).some(([n, v]) => !DISPATCH.includes(n) || !Array.isArray(v) ||
    v.length !== 2 || !inKeys(v[0], sizes) || !uint(v[1]) || v[1] >= sizes[v[0] as 'code' | 'data']))
    throw new ModError(`SHARC kernel: exports are the dispatcher's labels, ${DISPATCH.join(', ')}`)
  const names = Object.keys(exports)
  if (names.length && (names.length !== DISPATCH.length || !DISPATCH.every(n => names.includes(n))))
    throw new ModError('SHARC kernel: a dispatcher defines all of ' + DISPATCH.join(', '))
  const kernels: Kernel[] = [], seen = new Set<string>()
  for (const k of Array.isArray(doc.kernels) ? doc.kernels : [null]) {
    const given = isDict(k) ? Object.keys(k) : []
    if (!isDict(k) || !['kind', 'id', 'entry'].every(n => given.includes(n)) ||
      given.some(n => !['kind', 'id', 'entry', 'state_bytes', 'knobs'].includes(n)) || !inKeys(k.kind, KERNEL_IDS) ||
      ('knobs' in k && k.kind !== 'sdk') || ('state_bytes' in k && k.kind === 'sdk'))
      throw new ModError('SHARC kernel: a kernel is {kind: sdk, id, entry, knobs} or ' +
        '{kind: mk2-voice|mk2-filter, id, entry[, state_bytes]}')
    const kind = k.kind as KernelKind, mid = k.id, entry = k.entry
    const [first, last] = KERNEL_IDS[kind]
    if (!int(mid) || !(first <= mid && mid <= last))
      throw new ModError(`SHARC kernel: ${kind} id ${str(mid)} is outside ${first}..${last}`)
    const space = `${KERNEL_TABLE[kind]}:${mid}`
    if (seen.has(space)) throw new ModError(`SHARC kernel: two ${KERNEL_TABLE[kind]} kernels with id ${mid}`)
    seen.add(space)
    if (!uint(entry) || 2 * entry >= code.length) throw new ModError(`SHARC kernel: ${kind} ${mid} entry is outside its code`)
    const out: Kernel = { kind, id: mid, entry }
    if (kind === 'sdk') {
      const knobs = k.knobs
      if (!Array.isArray(knobs) || knobs.length > KNOBS_MAX || knobs.some(r =>
        !Array.isArray(r) || r.length !== 2 || !r.every(int32) || r[0] >= r[1]))
        throw new ModError(`SHARC kernel: sdk ${mid} knobs are up to ${KNOBS_MAX} [min, max] pairs of int32, min below max`)
      out.knobs = knobs.map(([a, b]) => [a, b])
    } else {
      const state = 'state_bytes' in k ? k.state_bytes : 0
      if (!uint(state) || state % 4) throw new ModError(`SHARC kernel: ${kind} ${mid} state_bytes must be whole 32-bit words`)
      out.state_bytes = state
    }
    kernels.push(out)
  }
  if (!kernels.length && !names.length) throw new ModError('SHARC kernel: no kernel and no dispatcher')
  return { abi: KERNEL_ABI, code, data, codePc16: doc.code_pc16, dataAddress: doc.data_address, dataAlign: align, relocs,
    kernels, exports: Object.fromEntries(Object.entries(exports).map(([n, v]) => [n, [(v as any)[0], (v as any)[1]]])) }
}

/** The kernel's [code, data] moved to PC16 codePc16 and byte dataAddress: each relocation is a code or data
 * address field (big-endian in code, as the executable stores it; little-endian in data) that moves with its kind. */
export function rebase(artifact: KernelPayload, codePc16: number, dataAddress: number): [Uint8Array, Uint8Array] {
  if (dataAddress % artifact.dataAlign)
    throw new ModError(`SHARC kernel: data base 0x${dataAddress.toString(16)} is not ${artifact.dataAlign}-byte aligned`)
  const out = { code: artifact.code.slice(), data: artifact.data.slice() }
  const delta = { code: codePc16 - artifact.codePc16, data: dataAddress - artifact.dataAddress }
  for (const [section, at, width, kind] of artifact.relocs) {
    const field = out[section].subarray(at, at + width), order = section === 'code' ? [...field.keys()] : [...field.keys()].reverse()
    const modulus = 2 ** (8 * width)
    let value = order.reduce((n, i) => n * 256 + field[i], 0)
    value = ((value + delta[kind]) % modulus + modulus) % modulus
    for (const i of [...order].reverse()) { field[i] = value % 256; value = Math.floor(value / 256) }
  }
  return [out.code, out.data]
}

/** The NOPs after a code block: the read-ahead guard, plus one parcel when the code ends mid-word. */
function guard(code: Uint8Array): Uint8Array {
  return Uint8Array.from({ length: 64 + 2 * ((code.length >> 1) % 2) }, (_, i) => (i % 2 ? 0 : 1))
}

/** The executable's byte order and the loader's differ by each 16-bit parcel's two bytes. */
export function parcels(code: Uint8Array): Uint8Array {
  const out = code.slice()
  for (let i = 0; i + 1 < out.length; i += 2) [out[i], out[i + 1]] = [code[i + 1], code[i]]
  return out
}

/** A 48-bit absolute JUMP to PC16 pc, in loader order. */
export const jump = (pc: number): Uint8Array => parcels(Uint8Array.of(0x06, 0x3e, 0x00, pc >>> 16 & 0xff, pc >>> 8 & 0xff, pc & 0xff))

const u32 = (...words: number[]): Uint8Array => {
  const out = new Uint8Array(4 * words.length), view = new DataView(out.buffer)
  words.forEach((w, i) => view.setUint32(4 * i, w, true))
  return out
}

/** An SDK machine's knob map for the dispatcher: the count, then per knob min, span / RAW_MAX and span % RAW_MAX. */
export function knobMap(knobs: [number, number][]): Uint8Array {
  const words = [knobs.length]
  for (const [lo, hi] of knobs) {
    const span = ((hi - lo) % 2 ** 32 + 2 ** 32) % 2 ** 32
    words.push((lo % 2 ** 32 + 2 ** 32) % 2 ** 32, Math.floor(span / RAW_MAX), span % RAW_MAX)
  }
  return u32(...words)
}

export type KernelLayout = { pool: [number, number]; used: number; mods: { id: string; code: [number, number]; data: [number, number] }[] }

/** Lay the dispatcher (the payload with exports) and every kernel out in the profile's pool: each block of code
 * takes its guard; data keeps its alignment; each kernel gets 16 slots of state, an SDK machine its knob map.
 * The dispatcher's tables name each kernel by id. Throws ModError when the pool is too small. sharc.place_kernels. */
export function placeKernels(payloads: [string, KernelPayload][], profile: SharcProfile):
  [[number, Uint8Array][], [number, number, number][], [number, Uint8Array, Uint8Array][], KernelLayout] {
  const [lo, hi] = profile.pool!
  const dispatcher = payloads.find(([, p]) => Object.keys(p.exports).length)![1]
  const order = [...payloads.filter(([, p]) => p === dispatcher), ...payloads.filter(([, p]) => p !== dispatcher)]
  let at = lo
  const placed = new Map<string, [number, number]>(), loads: [number, Uint8Array][] = [], fills: [number, number, number][] = []
  const layout: KernelLayout['mods'] = []
  const take = (size: number, align = 4): number => { const start = Math.ceil(at / align) * align; at = start + size; return start }
  for (const [mid, p] of order) {
    const codeAt = take(p.code.length + guard(p.code).length), dataAt = take(p.data.length, p.dataAlign)
    placed.set(mid, [pc16(profile, codeAt), dataAt])
    layout.push({ id: mid, code: [codeAt, p.code.length + guard(p.code).length], data: [dataAt, p.data.length] })
  }
  const tables = { voice: new Uint8Array(TABLE_IDS * 16), filter: new Uint8Array(TABLE_IDS * 16) }
  for (const [mid, p] of order) for (const k of p.kernels) {
    const knobs = k.kind === 'sdk' ? knobMap(k.knobs!) : new Uint8Array(0)
    const knobAt = knobs.length ? take(knobs.length) : 0
    if (knobs.length) loads.push([knobAt, knobs])
    const each = k.kind === 'sdk' ? profile.kernelStateBytes! : k.state_bytes!
    const state = each ? take(16 * each, 8) : 0
    if (each) fills.push([state, 16 * each, 0])
    tables[KERNEL_TABLE[k.kind]].set(u32(placed.get(mid)![0] + k.entry, state, each, knobAt), 16 * k.id)
  }
  if (at > hi) throw new ModError(`DN2 kernels need ${at - lo} bytes; the machine pool has ${hi - lo}`)
  for (const [mid, p] of order) {
    const [code, data] = rebase(p, ...placed.get(mid)!)
    if (p === dispatcher) {
      for (const kind of ['voice', 'filter'] as const) {
        const [section, off] = p.exports[`dn2_${kind}_table.`]
        if (section !== 'data' || off + tables[kind].length > data.length)
          throw new ModError(`SHARC dispatcher: dn2_${kind}_table. is not ${tables[kind].length} bytes of data`)
        data.set(tables[kind], off)
      }
    }
    const codeAt = layout.find(e => e.id === mid)!.code[0]
    const tail = guard(code), block = new Uint8Array(code.length + tail.length)
    block.set(parcels(code)); block.set(tail, code.length)
    loads.push([codeAt, block])
    if (data.length) {
      const padded = new Uint8Array(data.length + (-data.length & 3))
      padded.set(data)
      loads.push([placed.get(mid)![1], padded])
    }
  }
  const hook = (label: string) => placed.get(order[0][0])![0] + dispatcher.exports[label][1]
  const patches: [number, Uint8Array, Uint8Array][] = []
  for (const [key, label] of [['voiceHook', 'dn2_voice_hook.'], ['filterHook', 'dn2_filter_hook.']] as const) {
    const [site, old] = profile[key]!
    patches.push([site, fromHex(old)!, jump(hook(label))])
  }
  const [site, old] = profile.filterCapture!
  patches.push([site, fromHex(old)!, u32(hook('dn2_filter_capture.'))])
  for (const key of ['voiceStack', 'filterStack'] as const) {   // each kind's L1 stack: the guard word, then zeros
    const [sLo, sHi] = profile[key]!
    loads.push([sLo, u32(profile.stackGuard!)])
    fills.push([sLo + 4, sHi - sLo - 4, 0])
  }
  return [loads, fills, patches, { pool: [lo, hi], used: at - lo, mods: layout }]
}
