// SPDX-License-Identifier: GPL-3.0-or-later
// The DN2's observed boot-record subset. Paired with elekloader/sharc.py; no external parser code.
import { sha } from './bytes.ts'
import type { SharcProfile } from './devices.ts'
import { ModError } from './elemod.ts'

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
    if (!profile.patchRanges?.some(([start, end]) => start <= lo && hi <= end))
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
  machineId?: number
  machineKind?: 'voice' | 'filter'
  segments: [number, 'code' | 'data', Uint8Array][]
  zero: [number, number][]
  entries: { [name: string]: number } | null
  stackBytes: number
  owner: [number, number]
}

/** Validate the narrow prelinked format-2 payload; offsets are owner bytes. */
export function modulePayload(doc: any, profile: SharcProfile | undefined, core = false): SharcPayload {
  if (!profile?.abi) throw new ModError('SHARC payload has no supported device ABI')
  const kind = !core ? (['voice', 'filter'] as const).find(k =>
    profile[`${k}Abi`] !== undefined && doc?.abi === profile[`${k}Abi`]) : undefined
  const keys = ['abi', 'entries', ...(kind ? ['machine_id'] : []), 'segments', 'stack_bytes', 'zero']
  if (!doc || typeof doc !== 'object' || Array.isArray(doc) || Object.keys(doc).sort().join(',') !== keys.join(','))
    throw new ModError(kind ? `SHARC ${kind} payload needs exactly abi, segments, zero, entries, stack_bytes, machine_id`
      : 'SHARC payload needs exactly abi, segments, zero, entries, stack_bytes')
  const abi = core ? profile.abi : kind ? profile[`${kind}Abi`] : profile.l2Abi
  if (!abi || doc.abi !== abi) throw new ModError('SHARC ABI does not match the device profile')
  const owner = core ? profile.coreRange! : kind ? profile[`${kind}Range`]! : profile.l2AppRange!
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
    const entryKeys = core ? 'hook_offset_bytes' : kind ? 'render_offset_bytes' : 'init_offset_bytes,render_offset_bytes'
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
  if (kind) {
    const prefix = profile[`${kind}AdapterBytes`]!, state = size - profile[`${kind}StateBytes`]!
    const first = kind === 'voice' ? 5 : 6
    if (!uint32(doc.machine_id) || doc.machine_id < first || doc.machine_id >= 32)
      throw new ModError(`SHARC ${kind} machine_id must be ${first}..31`)
    if (entries === null) throw new ModError(`SHARC ${kind} payload needs a render entry`)
    if (segments[0][0] !== 0 || segments[0][1] !== 'code' || segments[0][2].length !== prefix ||
      sha(segments[0][2]) !== profile[`${kind}AdapterSha256`])
      throw new ModError(`SHARC ${kind} adapter does not match the qualified prefix`)
    if (!zero.some(([off, count]) => off === state && count === profile[`${kind}StateBytes`]) ||
      segments.slice(1).some(([off, , data]) => off < prefix || off + data.length > state) ||
      zero.some(([off, count]) => off !== state && (off < prefix || off + count > state)) ||
      entries.render_offset_bytes < prefix)
      throw new ModError(`SHARC ${kind} mod overlaps the adapter reservation`)
  }
  const stack = doc.stack_bytes
  if (kind && (!uint32(stack) || stack > profile[`${kind}StackMax`]!))
    throw new ModError(`SHARC ${kind} stack exceeds the profile limit`)
  if (!uint32(stack) || stack > profile.stackMax!) throw new ModError('SHARC stack contribution exceeds the profile limit')
  if (entries === null && stack) throw new ModError('inert SHARC payload must declare zero stack')
  return { segments, zero, entries, stackBytes: stack, owner,
    ...(kind ? { machineId: doc.machine_id, machineKind: kind } : {}) }
}
