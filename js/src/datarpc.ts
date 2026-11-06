// SPDX-License-Identifier: GPL-3.0-or-later
// The unit's Data router over USB MIDI (docs/FORMAT.md, "+Drive mods"). Ported from elekloader's datarpc.py
// (GPL-2.0-or-later): the same requests, byte for byte, and the same messages. A port is anything with send(bytes) and
// receive(timeout in ms) -> one whole SysEx message (F0 to F7) or null: Web MIDI on a page, a recording in a test.
//
// A message is F0 00 20 3C 10 00, the request in Elektron's 7-bit groups, F7. A request is a u16 sequence number, two
// zero bytes, a command byte and its arguments; the reply has the sequence number at +2, the command | 0x80 at +4
// and a status at +5 (1: done). datarpc.py's docstring lists the commands.
import * as container from './container.ts'
import { ModError } from './elemod.ts'
import { repr } from './py.ts'

export const HEAD = Uint8Array.of(0xf0, 0x00, 0x20, 0x3c, 0x10, 0x00)
export const LIST = 0x53, ROPEN = 0x54, READ = 0x55, RCLOSE = 0x56, WOPEN = 0x57, WRITE = 0x58, WCLOSE = 0x59, CLEAR = 0x5c
export const CHUNK = 0x2000
export const FOLDER = '/mods'

export class RpcError extends ModError {}

export interface Port {
  send(raw: Uint8Array): void | Promise<void>
  receive(timeoutMs: number): Promise<Uint8Array | null>
}

/** Bytes -> Elektron's 7-bit groups. */
export function pack7(data: Uint8Array): Uint8Array {
  const out: number[] = []
  for (let i = 0; i < data.length; i += 7) {
    const g = data.subarray(i, i + 7)
    let hi = 0
    g.forEach((b, k) => { if (b & 0x80) hi |= 0x40 >> k })
    out.push(hi, ...Array.from(g, b => b & 0x7f))
  }
  return Uint8Array.from(out)
}

/** Elektron's 7-bit groups -> bytes. */
export function unpack7(data: Uint8Array): Uint8Array {
  const out: number[] = []
  for (let i = 0; i < data.length; i += 8) {
    const hi = data[i]
    data.subarray(i + 1, i + 8).forEach((b, k) => out.push(b | (hi & (0x40 >> k) ? 0x80 : 0)))
  }
  return Uint8Array.from(out)
}

export function frame(msg: Uint8Array): Uint8Array {
  const p = pack7(msg), out = new Uint8Array(HEAD.length + p.length + 1)
  out.set(HEAD); out.set(p, HEAD.length); out[out.length - 1] = 0xf7
  return out
}

/** A SysEx message -> the message, or null if it is not one of these. */
export function unframe(raw: Uint8Array): Uint8Array | null {
  if (raw.length < 8 || HEAD.some((b, i) => raw[i] !== b) || raw[raw.length - 1] !== 0xf7) return null
  return unpack7(raw.subarray(6, raw.length - 1))
}

const latin1 = (s: string) => Uint8Array.from(s, c => c.charCodeAt(0) & 0xff)
const unlatin1 = (b: Uint8Array) => Array.from(b, c => String.fromCharCode(c)).join('')
const hex2 = (n: number) => n.toString(16).padStart(2, '0')
const be = (...w: [number, number][]) => {           // [value, bytes] -> big-endian
  const out: number[] = []
  for (const [v, n] of w) for (let k = n - 1; k >= 0; k--) out.push(Math.floor((v >>> 0) / 2 ** (8 * k)) & 0xff)
  return Uint8Array.from(out)
}
const cat = (...p: Uint8Array[]) => {
  const out = new Uint8Array(p.reduce((n, x) => n + x.length, 0))
  let o = 0
  for (const x of p) { out.set(x, o); o += x.length }
  return out
}
const rd32 = (m: Uint8Array, at: number) => new DataView(m.buffer, m.byteOffset).getUint32(at)
const path0 = (p: string) => cat(latin1(p), Uint8Array.of(0))

export type Entry = [name: string, kind: number, size: number]

/** Requests over a port. datarpc.Client. */
export class Client {
  seq = 0
  port: Port
  timeoutMs: number
  constructor(port: Port, timeoutMs = 5000) { this.port = port; this.timeoutMs = timeoutMs }

  async call(cmd: number, args: Uint8Array = new Uint8Array(0)): Promise<Uint8Array> {
    const seq = this.seq
    this.seq = (this.seq + 1) & 0xffff
    await this.port.send(frame(cat(be([seq, 2], [0, 2]), Uint8Array.of(cmd), args)))
    const end = Date.now() + this.timeoutMs
    for (;;) {
      const left = end - Date.now()
      if (left <= 0) throw new RpcError(`no reply from the unit to command 0x${hex2(cmd)}`)
      const raw = await this.port.receive(left)
      if (raw === null) continue
      const msg = unframe(raw)
      if (msg === null || msg.length < 6 || ((msg[2] << 8) | msg[3]) !== seq) continue
      if (msg[4] !== (cmd | 0x80)) throw new RpcError(`the unit answered command 0x${hex2(cmd)} with 0x${hex2(msg[4])}`)
      if (msg[5] !== 1) {
        const rest = msg.subarray(6), z = rest.indexOf(0)
        const text = unlatin1(z < 0 ? rest : rest.subarray(0, z))
        throw new RpcError(`the unit refused command 0x${hex2(cmd)}${text ? ': ' + text : ''}`)
      }
      return msg
    }
  }

  /** -> [[name, kind, size]] (kind 1 a folder, 2 an item). */
  async ls(path: string): Promise<Entry[]> {
    const msg = await this.call(LIST, cat(path0(path), be([0, 4], [0, 4], [1, 1])))
    const out: Entry[] = []
    let at = 18
    while (at < msg.length) {
      const end = msg.indexOf(0, at)
      if (end < 0) throw new RpcError('a listing cut short')
      const name = unlatin1(msg.subarray(at, end)), kind = msg[end + 2]
      at = end + 3
      if (kind === 1) { out.push([name, 1, 0]); at += 4 }
      else { out.push([name, 2, rd32(msg, at + 4)]); at += 12 }
    }
    return out
  }

  /** -> the bytes the unit serves (a data container). */
  async get(path: string): Promise<Uint8Array> {
    let msg = await this.call(ROPEN, cat(path0(path), be([CHUNK, 4], [0, 1])))
    const job = rd32(msg, 6), parts: Uint8Array[] = []
    try {
      for (let seq = 0; ; seq++) {
        msg = await this.call(READ, be([job, 4], [seq, 4]))
        const n = rd32(msg, 23)
        parts.push(msg.slice(27, 27 + n))
        if (msg[18]) break
      }
    } finally {
      await this.call(RCLOSE, be([job, 4]))
    }
    return cat(...parts)
  }

  /** Write `data` (a data container) to `path`. */
  async put(path: string, data: Uint8Array, progress?: (sent: number, total: number) => void): Promise<void> {
    let msg = await this.call(WOPEN, cat(be([data.length, 4]), path0(path)))
    const job = rd32(msg, 6)
    for (let seq = 0, at = 0; at < data.length; seq++, at += CHUNK) {
      const chunk = data.subarray(at, at + CHUNK)
      msg = await this.call(WRITE, cat(be([job, 4], [seq, 4], [container.crc(chunk), 4], [chunk.length, 4]), chunk))
      const took = rd32(msg, 14)
      if (took !== at + chunk.length) throw new RpcError(`the unit took ${took} bytes of ${at + chunk.length}`)
      progress?.(at + chunk.length, data.length)
    }
    msg = await this.call(WCLOSE, be([job, 4], [data.length, 4]))
    const kept = rd32(msg, 10)
    if (kept !== data.length) throw new RpcError(`the unit kept ${kept} bytes of ${data.length}`)
  }

  async clear(path: string): Promise<void> {
    await this.call(CLEAR, path0(path))
  }
}

/** `/mods` on a unit: files in and out of their containers. datarpc.Mods. */
export class Mods {
  c: Client
  folder: string
  constructor(c: Client, folder = FOLDER) { this.c = c; this.folder = folder.replace(/\/+$/, '') }

  path(name: string): string {
    if (!name || name.includes('/')) throw new ModError(`${repr(name)} is not a file name in ${this.folder}`)
    return `${this.folder}/${name}`
  }

  ls(): Promise<Entry[]> { return this.c.ls(this.folder) }

  /** -> [the container's header, the file's bytes]. */
  async get(name: string): Promise<[container.Head, Uint8Array]> {
    return container.parse(await this.c.get(this.path(name)))
  }

  /** Upload a file in a container with the unit's family, device and OS (from `head`). */
  put(name: string, raw: Uint8Array, head: container.Head, progress?: (sent: number, total: number) => void): Promise<void> {
    return this.c.put(this.path(name), container.wrap(raw, head.family, head.device, head.release), progress)
  }

  rm(name: string): Promise<void> { return this.c.clear(this.path(name)) }
}
