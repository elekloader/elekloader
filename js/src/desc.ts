// SPDX-License-Identifier: GPL-3.0-or-later
// The build descriptor, `core_desc` (docs/FORMAT.md, "The build descriptor"). Ported from elekloader's desc.py
// (GPL-2.0-or-later): the same block, byte for byte. A mod that imports core_desc makes the linker write it after the
// tables: the modules, their exports, every table with its room for more entries and each entry's sort key, the names
// the modules claim, the RAM line and the mod storage's room. desc.py's docstring gives the layout.
import { crc32 } from './zip.ts'
import { ModError } from './elemod.ts'

export const SYM = 'core_desc'
export const ABI: [number, number] = [1, 0]
export const HEADER = 0x60
export const SPARE = 16                     // a table's spare entries, unless its collection says
export const F_MORE = 1, F_RENDER = 2, F_DSP = 4
export const LINKER_ORDER = 0xffff          // the key's order of an entry the linker makes itself
export const KIND_CORE = 0, KIND_FLASHED = 1, KIND_LOADER = 2
export const CLAIM_NAME = 1
export const STAGING = ['drive_staging', 'drive_staging_size'] as const
const MODULE = 16, EXPORT = 8, TABLE = 24, CLAIM = 8, AREA = 20, SCOPE = 16

export type DescModule = { name: string; version: string; kind: number; flags: number; exports: [string, number][] }
export type DescTable = { name: string; address: number; entry: number; count: number; capacity: number; flags: number; keys: number[] }
export type DescFields = {
  family: number; device: number; release: number; deviceKey: string; os: string
  modules: DescModule[]; tables: DescTable[]; claims: [number, number, string][]
  area: [number, number, number, number, number]
  storage: { trailer: [number, number]; scopes: [string, number, number, number][] }
}

const utf8 = new TextEncoder()

/** A descriptor's fields (link.ts gathers them) -> the block's bytes. desc.encode. */
export function encode(d: DescFields): Uint8Array {
  const pool: number[] = [], at = new Map<string, number>()
  const str = (s: string) => {
    if (!at.has(s)) {
      at.set(s, pool.length)
      pool.push(...utf8.encode(s), 0)
    }
    return at.get(s)!
  }
  str(d.deviceKey)
  str(d.os)
  const exports = d.modules.flatMap(m => m.exports)
  const nkeys = d.tables.reduce((n, t) => n + t.keys.length, 0)
  const oMod = HEADER, oExp = oMod + MODULE * d.modules.length, oTab = oExp + EXPORT * exports.length
  const oClm = oTab + TABLE * d.tables.length, oKey = oClm + CLAIM * d.claims.length, oArea = oKey + 4 * nkeys
  const oSto = oArea + AREA, oPool = oSto + 12 + SCOPE * d.storage.scopes.length
  // names first, so the pool's size is known before the block is laid out (as desc.py adds them while it packs)
  d.modules.forEach(m => { str(m.name); str(m.version) })
  for (const [nm] of exports) str(nm)
  for (const t of d.tables) str(t.name)
  for (const [, , nm] of d.claims) str(nm)
  for (const [nm] of d.storage.scopes) str(nm)
  const poolLen = pool.length, size = oPool + poolLen + ((4 - (poolLen % 4)) % 4)
  const b = new Uint8Array(size), v = new DataView(b.buffer)
  const u32 = (o: number, x: number) => v.setUint32(o, ((x % 2 ** 32) + 2 ** 32) % 2 ** 32)
  b.set([0x45, 0x4c, 0x4b, 0x44], 0)                      // 'ELKD'
  v.setUint16(4, ABI[0]); v.setUint16(6, ABI[1]); u32(8, size)
  v.setUint16(0x10, d.family); v.setUint16(0x12, d.device); u32(0x14, d.release)
  u32(0x18, oPool); u32(0x1c, poolLen)
  v.setUint16(0x20, d.modules.length); v.setUint16(0x22, exports.length)
  v.setUint16(0x24, d.tables.length); v.setUint16(0x26, d.claims.length)
  ;[oMod, oExp, oTab, oClm, oKey, oArea, oSto].forEach((x, i) => u32(0x28 + 4 * i, x))
  u32(0x44, str(d.deviceKey)); u32(0x48, str(d.os))
  let o = oMod, first = 0
  d.modules.forEach((m, i) => {
    u32(o, str(m.name)); u32(o + 4, str(m.version)); b[o + 8] = m.kind; b[o + 9] = m.flags
    v.setUint16(o + 10, i); v.setUint16(o + 12, first); v.setUint16(o + 14, m.exports.length)
    first += m.exports.length
    o += MODULE
  })
  for (const [nm, a] of exports) { u32(o, str(nm)); u32(o + 4, a); o += EXPORT }
  let k = oKey
  for (const t of d.tables) {
    u32(o, str(t.name)); u32(o + 4, t.address); v.setUint16(o + 8, t.entry); v.setUint16(o + 10, t.count)
    v.setUint16(o + 12, t.capacity); v.setUint16(o + 14, t.flags); u32(o + 16, k)
    k += 4 * t.keys.length
    o += TABLE
  }
  for (const [kind, mi, nm] of d.claims) { v.setUint16(o, kind); v.setUint16(o + 2, mi); u32(o + 4, str(nm)); o += CLAIM }
  for (const t of d.tables) for (const x of t.keys) { u32(o, x); o += 4 }
  for (const x of d.area) { u32(o, x); o += 4 }
  u32(o, d.storage.trailer[0]); u32(o + 4, d.storage.trailer[1]); v.setUint16(o + 8, d.storage.scopes.length)
  o += 12
  for (const [nm, room, used, gen] of d.storage.scopes) { u32(o, str(nm)); u32(o + 4, room); u32(o + 8, used); u32(o + 12, gen); o += SCOPE }
  b.set(pool, oPool)
  u32(0x0c, crc32(b))
  return b
}

export type Decoded = Omit<DescFields, 'modules'> & {
  abi: [number, number]; size: number; crc: number
  modules: (DescModule & { index: number })[]
}

/** The block at `at` of `blk` -> its fields. ModError on a bad block (or a RangeError on a block cut inside a
 * record, as desc.py's struct.error). desc.decode. */
export function decode(blk: Uint8Array, at = 0): Decoded {
  if (blk.length < at + 4 || blk[at] !== 0x45 || blk[at + 1] !== 0x4c || blk[at + 2] !== 0x4b || blk[at + 3] !== 0x44)
    throw new ModError(`no build descriptor at +0x${at.toString(16)}`)
  if (blk.length < at + 16) throw new RangeError('unpack_from requires a buffer of at least 16 bytes')
  const v0 = new DataView(blk.buffer, blk.byteOffset + at)
  const major = v0.getUint16(4), minor = v0.getUint16(6), size = v0.getUint32(8), crc = v0.getUint32(12)
  if (major !== ABI[0]) throw new ModError(`build descriptor ABI ${major}, this reader reads ${ABI[0]}`)
  const b = blk.slice(at, at + size)
  if (b.length !== size || size < HEADER) throw new ModError('the build descriptor is cut short')
  const z = b.slice()
  z.fill(0, 0x0c, 0x10)
  if (crc32(z) !== crc) throw new ModError('the build descriptor\'s CRC does not match')
  const v = new DataView(b.buffer)
  const u16 = (o: number) => v.getUint16(o), u32 = (o: number) => v.getUint32(o)
  const pAt = u32(0x18)
  const dec = new TextDecoder('utf-8', { fatal: true })
  const name = (off: number) => {
    const o = pAt + off, end = b.indexOf(0, o)
    if (end < 0) throw new RangeError('substring not found')
    return dec.decode(b.subarray(o, end))
  }
  const [nmod, nexp, ntab, nclm] = [u16(0x20), u16(0x22), u16(0x24), u16(0x26)]
  const [oMod, oExp, oTab, oClm, , oArea, oSto] = Array.from({ length: 7 }, (_, i) => u32(0x28 + 4 * i))
  const exports: [string, number][] = Array.from({ length: nexp }, (_, i) => [name(u32(oExp + EXPORT * i)), u32(oExp + EXPORT * i + 4)])
  const modules = Array.from({ length: nmod }, (_, i) => {
    const o = oMod + MODULE * i, first = u16(o + 12), cnt = u16(o + 14)
    return { name: name(u32(o)), version: name(u32(o + 4)), kind: b[o + 8], flags: b[o + 9], index: u16(o + 10),
      exports: exports.slice(first, first + cnt) }
  })
  const tables = Array.from({ length: ntab }, (_, i) => {
    const o = oTab + TABLE * i, cnt = u16(o + 10), ko = u32(o + 16)
    return { name: name(u32(o)), address: u32(o + 4), entry: u16(o + 8), count: cnt, capacity: u16(o + 12), flags: u16(o + 14),
      keys: Array.from({ length: cnt }, (_, k) => u32(ko + 4 * k)) }
  })
  const claims = Array.from({ length: nclm }, (_, i): [number, number, string] =>
    [u16(oClm + CLAIM * i), u16(oClm + CLAIM * i + 2), name(u32(oClm + CLAIM * i + 4))])
  const nsc = u16(oSto + 8)
  const scopes = Array.from({ length: nsc }, (_, i): [string, number, number, number] => {
    const o = oSto + 12 + SCOPE * i
    return [name(u32(o)), u32(o + 4), u32(o + 8), u32(o + 12)]
  })
  return { abi: [major, minor], size, crc, family: u16(0x10), device: u16(0x12), release: u32(0x14),
    deviceKey: name(u32(0x44)), os: name(u32(0x48)), modules, tables, claims,
    area: Array.from({ length: 5 }, (_, i) => u32(oArea + 4 * i)) as [number, number, number, number, number],
    storage: { trailer: [u32(oSto), u32(oSto + 4)], scopes } }
}
