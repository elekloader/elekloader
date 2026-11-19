// SPDX-License-Identifier: GPL-3.0-or-later
// Elektron's data container: what the Data router (`/projects`, `/soundbanks`, and the +Drive loader's `/mods`) sends
// and takes (docs/FORMAT.md, "The data container"). Ported from elekloader's container.py (GPL-2.0-or-later): the same
// bytes and the same messages.
//
//   header (31 bytes, big-endian): u32 magic 0xac11d303, u8 header version (2), u16 family id, u16 device id, u32 OS
//   release, u32 file type, i32 file version, i32 file index, u32 payload size, u8 is-compressed, u8 footer size (12)
//   body: the payload
//   footer (12 bytes): u32 hash (zlib.crc32(body, 0xffffffff)), u32 the body's size as sent, u32 magic 0xaaa1daaa
import { ModError } from './elemod.ts'
import { crc32 } from './zip.ts'

export const MAGIC = 0xac11d303, TAIL = 0xaaa1daaa
export const HEADER = 31, FOOTER = 12
export const VERSION = 2
export const EMOD = 0x454d4f44              // 'EMOD': a +Drive mod file, or another file of /mods
export const EMOD_VERSION = 2

export type Head = {
  version: number; family: number; device: number; release: number; type: number
  file_version: number; index: number; size: number; compressed: boolean
}

/** The footer's hash: CRC-32 from 0xffffffff, as stock and the boot linker compute it. container.crc. */
export const crc = (body: Uint8Array): number => crc32(body, 0xffffffff)

const hex8 = (n: number) => '0x' + (n >>> 0).toString(16).padStart(8, '0')

function header(h: Omit<Head, 'compressed'> & { compressed: boolean | number }): Uint8Array {
  const b = new Uint8Array(HEADER), v = new DataView(b.buffer)
  v.setUint32(0, MAGIC); b[4] = h.version; v.setUint16(5, h.family); v.setUint16(7, h.device)
  v.setUint32(9, h.release >>> 0); v.setUint32(13, h.type >>> 0); v.setInt32(17, h.file_version); v.setInt32(21, h.index)
  v.setUint32(25, h.size >>> 0); b[29] = h.compressed ? 1 : 0; b[30] = FOOTER
  return b
}

function footer(body: Uint8Array): Uint8Array {
  const b = new Uint8Array(FOOTER), v = new DataView(b.buffer)
  v.setUint32(0, crc(body)); v.setUint32(4, body.length); v.setUint32(8, TAIL)
  return b
}

const join = (...p: Uint8Array[]) => {
  const out = new Uint8Array(p.reduce((n, x) => n + x.length, 0))
  let o = 0
  for (const x of p) { out.set(x, o); o += x.length }
  return out
}

/** A file's bytes -> the container the Data router takes (never compressed). container.wrap. */
export function wrap(body: Uint8Array, family: number, device: number, release: number, fileType = EMOD,
  version = EMOD_VERSION, index = 0): Uint8Array {
  return join(header({ version: VERSION, family, device, release, type: fileType, file_version: version, index,
    size: body.length, compressed: false }), body, footer(body))
}

function fields(raw: Uint8Array): Head & { footer: number } {
  const v = new DataView(raw.buffer, raw.byteOffset)
  return { version: raw[4], family: v.getUint16(5), device: v.getUint16(7), release: v.getUint32(9), type: v.getUint32(13),
    file_version: v.getInt32(17), index: v.getInt32(21), size: v.getUint32(25), compressed: !!raw[29], footer: raw[30] }
}

/** A container -> [its header's fields, its body]. ModError on a bad one; a compressed body is returned as it is.
 * container.parse. */
export function parse(raw: Uint8Array): [Head, Uint8Array] {
  if (raw.length < HEADER + FOOTER) throw new ModError(`not a data container: ${raw.length} bytes`)
  const v = new DataView(raw.buffer, raw.byteOffset, raw.length)
  if (v.getUint32(0) !== MAGIC) throw new ModError(`not a data container (magic ${hex8(v.getUint32(0))})`)
  const { footer: fsize, ...head } = fields(raw)
  if (fsize !== FOOTER) throw new ModError(`a data container with a ${fsize}-byte footer`)
  const body = raw.slice(HEADER, raw.length - FOOTER)
  const f = raw.length - FOOTER, hsh = v.getUint32(f), sent = v.getUint32(f + 4), tail = v.getUint32(f + 8)
  if (tail !== TAIL) throw new ModError(`the data container's footer magic is ${hex8(tail)}`)
  if (sent !== body.length) throw new ModError(`the data container's footer says ${sent} bytes; the body has ${body.length}`)
  if (!head.compressed && head.size !== body.length)
    throw new ModError(`the data container's header says ${head.size} bytes; the body has ${body.length}`)
  if (crc(body) !== hsh) throw new ModError('the data container\'s hash does not match its body')
  return [head, body]
}

/** Only the header's fields, from the first 31 bytes of a download. container.header_of. */
export function headerOf(raw: Uint8Array): Head {
  if (raw.length < HEADER || new DataView(raw.buffer, raw.byteOffset).getUint32(0) !== MAGIC)
    throw new ModError('not a data container header')
  const { footer: _f, ...head } = fields(raw.subarray(0, HEADER))
  return head
}

/** A parsed container back to its bytes. container.rewrap. */
export const rewrap = (head: Head, body: Uint8Array): Uint8Array => join(header(head), body, footer(body))
