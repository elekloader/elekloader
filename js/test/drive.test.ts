// SPDX-License-Identifier: GPL-3.0-or-later
// The build-checker, the data container and the Data router's framing against what elekloader's Python gave
// (test/drive-vectors.json, from tools/drive_vectors.py): tests/test_drive.py's fixtures on its made-up Analog Rytm MKII
// build, each set refused or loaded with the same codes, details, notes and lines.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { ANALOG_RYTM_MK2 } from '../src/devices.ts'
import { fromHex, toHex } from '../src/bytes.ts'
import * as container from '../src/container.ts'
import * as datarpc from '../src/datarpc.ts'
import * as drive from '../src/drive.ts'
import { ModError } from '../src/elemod.ts'

const V = JSON.parse(readFileSync(new URL('./drive-vectors.json', import.meta.url), 'utf8'))
const enc = new TextEncoder()
const file = (f: string | { hex: string } | null) => (f === null ? null : typeof f === 'string' ? enc.encode(f) : fromHex(f.hex)!)

test('the build-checker: drive.py\'s results, set for set', () => {
  ANALOG_RYTM_MK2.releases.push({ ...V.release, binSha256: '' })
  try {
    const build = drive.loadBuild(fromHex(V.build)!)
    assert.equal(build.label(), V.label)
    for (const c of V.cases) {
      const r = drive.check(build, c.files.map(([n, f]: [string, any]) => [n, file(f)]))
      assert.deepEqual(drive.summary(r), c.summary, c.name)
      assert.equal(drive.statusText(r), c.status, c.name)
      assert.deepEqual(drive.resultLines(r), c.lines, c.name)
    }
  } finally {
    ANALOG_RYTM_MK2.releases.pop()
  }
})

test('every host refusal code has a case refused with it and one that loads', () => {
  const host = drive.CODES.map(([n]) => n).filter(n => !['ok', 'missing', 'changed', 'range'].includes(n))
  for (const code of host) {
    assert.ok(V.cases.some((c: any) => c.name === `${code}: refused` && c.summary.choices.some((x: any) => x.reason === code)), code)
    assert.ok(V.cases.some((c: any) => c.name === `${code}: loads` && c.summary.choices.every((x: any) => x.state === 'loaded')), code)
  }
})

test('the data container: the Python\'s bytes, and its refusals', () => {
  const body = fromHex(V.container.body)!
  const raw = container.wrap(body, 2, 5, 0x30313736)
  assert.equal(toHex(raw), V.container.wrapped)
  const [head, got] = container.parse(raw)
  assert.deepEqual(got, body)
  assert.deepEqual([head.family, head.device, head.release, head.type, head.file_version, head.compressed],
    [2, 5, 0x30313736, container.EMOD, 2, false])
  assert.equal(toHex(container.rewrap(head, got)), V.container.wrapped)
  assert.deepEqual(container.headerOf(raw), head)
  for (const [i, why] of [[40, 'hash'], [raw.length - 1, 'magic']] as [number, string][]) {
    const bad = raw.slice()
    bad[i] ^= 1
    assert.throws(() => container.parse(bad), (e: Error) => e instanceof ModError && e.message.includes(why))
  }
  // the build descriptor in a container reads as the bare one
  ANALOG_RYTM_MK2.releases.push({ ...V.release, binSha256: '' })
  try {
    assert.equal(drive.loadBuild(fromHex(V.container.build)!).crc, drive.loadBuild(fromHex(V.build)!).crc)
  } finally {
    ANALOG_RYTM_MK2.releases.pop()
  }
})

test('/mods/active is written and read as the Python does', () => {
  assert.equal(drive.activeText([['a.elemod', enc.encode('x')], ['b c.elemod', new Uint8Array(0)]]), V.active.text)
  for (const [text, parsed] of V.active.parsed) assert.deepEqual(drive.parseActive(text), parsed, text)
  for (const bad of ['#x', ' a', 'a\nb', '']) assert.throws(() => drive.activeText([[bad, new Uint8Array(0)]]), ModError)
})

test('the 7-bit packing: the Python\'s bytes, and back', () => {
  for (const [data, packed] of V.pack7) {
    assert.equal(toHex(datarpc.pack7(fromHex(data)!)), packed)
    assert.equal(toHex(datarpc.unpack7(fromHex(packed)!)), data)
  }
})

/** A port that replays `elekloader mods`' recorded exchange: each message sent must be the one the CLI sent, and the
 * replies come back as the fake unit gave them. */
class Replay implements datarpc.Port {
  log: [string, string][]
  queue: Uint8Array[] = []
  constructor(log: [string, string][]) { this.log = [...log] }
  send(raw: Uint8Array) {
    const next = this.log.shift()
    assert.ok(next && next[0] === 'send', 'a message the CLI did not send')
    assert.equal(toHex(raw), next[1])
    while (this.log.length && this.log[0][0] === 'reply') this.queue.push(fromHex(this.log.shift()![1])!)
  }
  async receive() { return this.queue.shift() ?? null }
}

test('the Data router client: the CLI\'s requests, byte for byte, over its recorded exchange', async () => {
  ANALOG_RYTM_MK2.releases.push({ ...V.release, binSha256: '' })
  try {
    let seq = 0, head: container.Head | null = null
    for (const s of V.exchanges) {
      const port = new Replay(s.log), mods = new datarpc.Mods(new datarpc.Client(port, 200))
      mods.c.seq = seq
      let out: unknown
      try {
        if (s.op === 'build') {
          const [h, body] = await mods.get('.build')
          head = h
          out = { crc: drive.loadBuild(body).crc, head: h }
        } else if (s.op === 'put') out = (await mods.put(s.args[0], fromHex(s.args[1])!, head!)) ?? null
        else if (s.op === 'ls') out = await mods.ls()
        else if (s.op === 'get') out = toHex((await mods.get(s.args[0]))[1])
        else if (s.op === 'rm') out = (await mods.rm(s.args[0])) ?? null
      } catch (e) {
        if (!(e instanceof datarpc.RpcError)) throw e
        out = { error: e.message }
      }
      assert.deepEqual(out, s.out, `${s.op} ${s.args[0] ?? ''}`)
      assert.equal(port.log.length, 0, `${s.op}: the CLI sent more`)
      seq = mods.c.seq
    }
  } finally {
    ANALOG_RYTM_MK2.releases.pop()
  }
})
