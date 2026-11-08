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
