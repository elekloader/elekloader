// SPDX-License-Identifier: GPL-3.0-or-later
// The linker's checks that need no firmware: tests/test_units.py's counterparts.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { DIGITAKT_MK1, DIGITONE_MK1, targetOf } from '../src/devices.ts'
import { Mod2, check } from '../src/link.ts'

test('a parameter slot claims its sound slot (soundslot:<n>), from its own bytes or the stock image', () => {
  const dn = DIGITONE_MK1, r = dn.releases.find(x => x.version === '1.43')!
  const image = new Uint8Array(r.mainLen)
  image.set([0, 0, 0, 65], 0x100)                       // a long a descriptor shares with the stock
  const hex8 = (n: number) => n.toString(16).padStart(8, '0')
  const desc = (slot: number) => [['hex', '000000b6' + '00000001' + hex8(slot) + '00'.repeat(56)]]
  const mod = (id: string, parts: unknown[], names: string[] = [], relocs: unknown[] = []) => new Mod2({
    elemod: 2, id, version: '1', target: targetOf(dn, r),
    sections: { '.run': { align: 4, parts } }, symbols: { p: ['.run', 0] }, relocs,
    contribute: [{ to: 'core_params', data: '00000000', relocs: [[0, 'abs32', 'sym:p', 0]] }],
    resources: { names },
  }, id)
  const a = mod('a', desc(26)), b = mod('b', desc(26))
  assert.deepEqual(a.impliedNames(image), ['soundslot:26'])
  assert.deepEqual(a.names, [])
  assert.ok(check([a, b], image).includes('a 1 and b 1 both claim soundslot:26'))
  assert.deepEqual(a.names, ['soundslot:26'])           // check() adds it
  assert.ok(check([mod('p', desc(0)), mod('m', desc(26), ['soundslot:0'])], image).includes('p 1 and m 1 both claim soundslot:0'))
  const c = mod('c', [['hex', '000000b7' + '00000001'], ['stock', '0x' + hex8(dn.mainLoad + 0x100), 4], ['hex', '00'.repeat(56)]])
  assert.deepEqual(c.impliedNames(image), ['soundslot:65'])
  assert.deepEqual(mod('d', desc(79)).impliedNames(image), [])
  assert.deepEqual(mod('e', desc(26), [], [['.run', 8, 'abs32', 'abs', 26]]).impliedNames(image), [])
  const dt = DIGITAKT_MK1, t = new Mod2({
    elemod: 2, id: 't', version: '1', target: targetOf(dt, dt.releases.find(x => x.version === '1.53')!),
    sections: { '.run': { align: 4, parts: desc(26) } }, symbols: { p: ['.run', 0] },
    contribute: [{ to: 'core_params', data: '00000000', relocs: [[0, 'abs32', 'sym:p', 0]] }],
  }, 't')
  assert.deepEqual(t.impliedNames(new Uint8Array(0)), [])
})
