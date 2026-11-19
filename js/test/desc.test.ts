// SPDX-License-Identifier: GPL-3.0-or-later
// The build descriptor (core_desc) against what elekloader's linker gave (test/desc-vectors.json, from
// tools/desc_vectors.py): on a made-up Analog Rytm MKII release, the same image, map, layout and descriptor bytes, with
// and without a mod that imports core_desc, and the same refusals.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { ANALOG_RYTM_MK2 } from '../src/devices.ts'
import { sha, toHex } from '../src/bytes.ts'
import { Mod2, link } from '../src/link.ts'
import { ABI, decode, encode, F_LOADER, F_STAGE_TOP, PRE_CODE, PRE_DATA, SITE_COPIED, SITE_SITE } from '../src/desc.ts'
import { crc32 } from '../src/zip.ts'
import { ModError } from '../src/elemod.ts'

const V = JSON.parse(readFileSync(new URL('./desc-vectors.json', import.meta.url), 'utf8'))

test('the build descriptor: link.py\'s bytes and refusals, case for case', () => {
  const image = new Uint8Array(V.release.mainLen)
  for (let i = 0; i < image.length; i += 2) image.set([0x4e, 0x71], i)   // nops, as the Bench's image
  assert.equal(sha(image), V.release.mainSha256)
  ANALOG_RYTM_MK2.releases.push({ ...V.release, binSha256: '' })
  try {
    for (const c of V.cases) {
      let out
      try {
        out = link(c.docs.map((d: Record<string, any>) => new Mod2(structuredClone(d), d.id)), image)
      } catch (e) {
        if (!(e instanceof ModError)) throw e
        assert.equal(e.message, c.error, c.name)
        continue
      }
      assert.equal(c.error, undefined, `${c.name}: linked, the Python refused`)
      assert.equal(sha(out.image), c.image_sha256, c.name)
      assert.deepEqual(Object.fromEntries(out.map), c.map, c.name)
      assert.deepEqual(JSON.parse(JSON.stringify(out.layout)), c.layout, c.name)
      if (c.desc) {
        const [at, end] = out.layout.desc!
        const o = image.length + out.layout.run_load - out.layout.boot[0] + at - out.layout.ddr[0]
        assert.equal(toHex(out.image.slice(o, o + end - at)), c.desc, c.name)
      } else {
        assert.equal(out.layout.desc, undefined, c.name)
      }
    }
  } finally {
    ANALOG_RYTM_MK2.releases.pop()
  }
})

test('the build descriptor keeps whole orders (ABI 1.1), and reads a 1.0 block\'s from its keys', () => {
  const c = V.cases.find((x: { name: string }) => x.name === 'whole orders, -1000000 and 1000000')
  const blk = Uint8Array.from(c.desc.match(/../g).map((h: string) => parseInt(h, 16)))
  const d = decode(blk)
  assert.deepEqual(d.abi, ABI)
  const x = d.tables.find(t => t.name === 'ev_x')!, r = d.tables.find(t => t.name === 'ev_render_x')!
  assert.deepEqual(x.entries, [[-1000000, 2], [-1000000, 2], [50, 1]])
  assert.deepEqual(r.entries, [[1000000, 2]])
  // as ABI 1.0 wrote it: no orders, the keys' (held to 0..65534) stand
  const v = new DataView(blk.buffer)
  v.setUint16(6, 0)
  for (let i = 0; i < v.getUint16(0x24); i++) v.setUint32(v.getUint32(0x30) + 24 * i + 20, 0)
  v.setUint32(0x0c, 0)
  v.setUint32(0x0c, crc32(blk))
  const old = decode(blk)
  assert.deepEqual([old.sites, old.prelink], [[], []])
  assert.deepEqual(old.tables.find(t => t.name === 'ev_x')!.entries, [[0, 2], [0, 2], [50, 1]])
  assert.deepEqual(old.tables.find(t => t.name === 'ev_render_x')!.entries, [[65534, 2]])
  assert.deepEqual(old.tables.find(t => t.name === 'core_store_ids')!.entries, [[0x7fffffff, 0]])
})

test('ABI 1.3: the flags say whether the loader stages files at the area\'s top, and a 1.2 block has none', () => {
  const c = V.cases.find((x: { name: string }) => x.name === 'whole orders, -1000000 and 1000000')
  const d = decode(Uint8Array.from(c.desc.match(/../g).map((h: string) => parseInt(h, 16))))
  assert.deepEqual([d.abi, d.flags], [[1, 3], 0])
  const top = encode({ ...d, flags: F_STAGE_TOP })
  assert.equal(decode(top).flags, F_STAGE_TOP)
  const v = new DataView(top.buffer)              // as ABI 1.2 wrote it: the word is not read
  v.setUint16(6, 2)
  v.setUint32(0x0c, 0)
  v.setUint32(0x0c, crc32(top))
  assert.equal(decode(top).flags, 0)
})

test('ABI 1.2: the lock tables open to the loader alone, the site map, and pre-link ranges read back', () => {
  const c = V.cases.find((x: { name: string }) => x.name === 'the lock tables for the loader, a copied block')
  const d = decode(Uint8Array.from(c.desc.match(/../g).map((h: string) => parseInt(h, 16))))
  const t = Object.fromEntries(d.tables.map(x => [x.name, x]))
  for (const [n, spare] of [['core_lock_codes', 32], ['core_lock_ccs', 32], ['core_field_inits', 32], ['core_handles', 32], ['core_store_ids', 16]] as const)
    assert.deepEqual([t[n].flags, t[n].capacity - t[n].count], [F_LOADER, spare], n)
  assert.equal(t.core_lock_params.capacity, 32)
  assert.ok(d.sites!.some(([, , k, mi]) => k === SITE_SITE && mi === 2))
  assert.ok(d.sites!.some(([, , k, mi]) => k === SITE_COPIED && mi === 2))
  const pre: [number, number, number][] = [[0x40000500, 0x40000600, PRE_CODE], [0x40100000, 0x40100010, PRE_DATA]]
  const again = decode(encode({ ...d, prelink: pre }))
  assert.deepEqual([again.sites, again.prelink], [d.sites, pre])
})
