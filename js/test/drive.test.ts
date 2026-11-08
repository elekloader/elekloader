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
      const b = c.build ? drive.loadBuild(fromHex(c.build)!) : build   // a row's own build: a smaller area
      const r = drive.check(b, c.files.map(([n, f]: [string, any]) => [n, file(f)]))
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

/** A unit serving one file through a host whose MIDI input drops SysEx over 1 KB (Firefox's Web MIDI on Windows):
 * every reply longer than that is lost, and, as stock, an unfinished read can't be closed. */
class SmallBufferUnit implements datarpc.Port {
  queue: Uint8Array[] = []
  jobs = new Map<number, [Uint8Array, number]>()
  done = new Set<number>()
  nj = 1
  body: Uint8Array
  constructor(body: Uint8Array) { this.body = body }
  send(raw: Uint8Array) {
    const msg = datarpc.unframe(raw)!, seq = (msg[0] << 8) | msg[1], cmd = msg[4], a = msg.subarray(5)
    const u32 = (b: Uint8Array, at: number) => ((b[at] << 24) | (b[at + 1] << 16) | (b[at + 2] << 8) | b[at + 3]) >>> 0
    const be = (...v: number[]) => Uint8Array.from(v.flatMap(x => [x >>> 24, (x >>> 16) & 255, (x >>> 8) & 255, x & 255]))
    let ok = 1, extra = new Uint8Array(0)
    if (cmd === datarpc.ROPEN) {
      const j = ++this.nj, chunk = u32(a, a.indexOf(0) + 1)
      this.jobs.set(j, [container.wrap(this.body, 2, 5, 0x30313736), chunk])
      extra = Uint8Array.of(...be(j, chunk), 0)
    } else if (cmd === datarpc.READ) {
      const j = u32(a, 0), s = u32(a, 4), [data, c] = this.jobs.get(j)!
      const d = data.subarray(s * c, (s + 1) * c), last = (s + 1) * c >= data.length ? 1 : 0
      if (last) this.done.add(j)
      extra = Uint8Array.of(...be(j, s, 0), last, ...be(0, d.length), ...d)
    } else if (cmd === datarpc.RCLOSE) {
      ok = this.done.has(u32(a, 0)) ? 1 : 0
      extra = Uint8Array.of(...a.subarray(0, 4), 0, 0, 0, 0)
    }
    const reply = datarpc.frame(Uint8Array.of(0, 0, seq >> 8, seq & 255, cmd | 0x80, ok, ...extra))
    if (reply.length <= 1024) this.queue.push(reply)
  }
  async receive() { return this.queue.shift() ?? null }
}

test('a read whose replies are lost starts again with small chunks, and the client keeps that size', async () => {
  const body = new Uint8Array(256 * 44).map((_, i) => i & 255)
  const client = new datarpc.Client(new SmallBufferUnit(body), 100)
  assert.deepEqual(container.parse(await client.get('/mods/big.elemod'))[1], body)
  assert.equal(client.readChunk, 0x300)
  assert.deepEqual(container.parse(await client.get('/mods/big.elemod'))[1], body)
  assert.equal(client.readChunk, 0x300)
})

test('Mods.walk stops at DEPTH folders, and path() refuses a file a fourth folder down', async () => {
  // a route that did list a folder inside a third-level folder (the loader's own listing hides it)
  const tree = ['~T NAV/', '~T NAV/a.elemod', '~T NAV/~T A/', '~T NAV/~T A/b.elemod', '~T NAV/~T A/~T B/',
    '~T NAV/~T A/~T B/c.elemod', '~T NAV/~T A/~T B/~T C/', '~T NAV/~T A/~T B/~T C/d.elemod']
  const asked: string[] = []
  const client = {
    async ls(path: string) {
      asked.push(path)
      const rel = path === '/mods' ? '' : path.slice('/mods/'.length)
      return tree.filter(t => {
        const d = t.replace(/\/$/, '')
        return (d.includes('/') ? d.slice(0, d.lastIndexOf('/')) : '') === rel
      }).map(t => [t.replace(/\/$/, '').split('/').pop(), t.endsWith('/') ? 1 : 2, 100])
    },
  } as unknown as datarpc.Client
  const mods = new datarpc.Mods(client)
  assert.equal(datarpc.DEPTH, 3)
  assert.deepEqual((await mods.walk()).map(([p]) => p),
    ['~T NAV/a.elemod', '~T NAV/~T A/b.elemod', '~T NAV/~T A/~T B/c.elemod'])
  assert.ok(!asked.includes('/mods/~T NAV/~T A/~T B/~T C'))
  assert.equal(mods.path('~T NAV/~T A/~T B/c.elemod'), '/mods/~T NAV/~T A/~T B/c.elemod')
  assert.throws(() => mods.path('~T NAV/~T A/~T B/~T C/d.elemod'), ModError)
})

test('Mods.mkdir uploads nothing to FOLDER/.dir, then finds the folder in its parent, as the Python does', async () => {
  const calls: [string, string, Uint8Array?][] = []
  const client = {
    async put(path: string, data: Uint8Array) { calls.push(['put', path, data]) },
    async ls(path: string) { calls.push(['ls', path]); return path === '/mods/~T A' ? [['~T B', 1, 0]] : [] },
  } as unknown as datarpc.Client
  const mods = new datarpc.Mods(client)
  const head = { family: 2, device: 5, release: 0x30313736 } as container.Head
  await mods.mkdir('~T A/~T B', head)
  assert.equal(calls[0][1], '/mods/~T A/~T B/.dir')
  assert.equal(container.parse(calls[0][2]!)[1].length, 0)
  assert.deepEqual(calls[1], ['ls', '/mods/~T A'])
  await assert.rejects(mods.mkdir('~T C', head), ModError)
  calls.length = 0
  for (const bad of ['', '.x', '~T A/.x', 'a//b', 'a/b/c/d', '../x']) await assert.rejects(mods.mkdir(bad, head), ModError, bad)
  assert.equal(calls.length, 0)
})

test('Mods.move uploads "FROM\\nTO" to .mv, then reads both listings, as the Python does', async () => {
  const calls: string[] = []
  const listing: Record<string, datarpc.Entry[]> = { '/mods': [['~T A', 1, 0], ['~T C', 1, 0]], '/mods/~T A': [['x.elemod', 2, 9]], '/mods/~T C': [] }
  const client = {
    async put(path: string, data: Uint8Array) {
      calls.push(`put ${path} ${new TextDecoder().decode(container.parse(data)[1])}`)
      listing['/mods/~T C'] = [['x.elemod', 2, 9]]; listing['/mods/~T A'] = []
    },
    async ls(path: string) { calls.push(`ls ${path}`); return listing[path] },
  } as unknown as datarpc.Client
  const mods = new datarpc.Mods(client)
  const head = { family: 2, device: 5, release: 0x30313736 } as container.Head
  await mods.move('~T A/x.elemod', '~T C/x.elemod', head)
  assert.equal(calls[1], 'put /mods/.mv ~T A/x.elemod\n~T C/x.elemod')
  listing['/mods/~T C'] = [['x.elemod', 2, 9]]
  await assert.rejects(mods.move('~T C/x.elemod', '~T A/y.elemod', head, false), ModError)
  calls.length = 0
  for (const [from, to, folder] of [['~T A/x.elemod', '~T A/x.txt', false], ['active', 'a.elemod', false], ['~T A', '~T A/~T B', true],
    ['~T A', 'a/b/c/d', true], ['~T A', '.x', true], ['~T A', '~T A', true], ['a.elemod', 'a/b/c/d/a.elemod', false]] as [string, string, boolean][])
    await assert.rejects(mods.move(from, to, head, folder), ModError, `${from} ${to}`)
  assert.equal(calls.length, 0)
})
