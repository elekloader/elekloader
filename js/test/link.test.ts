// SPDX-License-Identifier: GPL-3.0-or-later
// The linker's checks that need no firmware: tests/test_units.py's counterparts.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { ANALOG_RYTM_MK2, DIGITONE_MK2, DIGITAKT_MK1, DIGITONE_MK1, OCTATRACK, devices, targetOf } from '../src/devices.ts'
import { sha } from '../src/bytes.ts'
import { Mod, check as check1 } from '../src/elemod.ts'
import { Mod2, check, imageProblems } from '../src/link.ts'

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

test('a machine claims its slot (machine:<n>) on the Analog Rytm MKII', () => {
  const ar = ANALOG_RYTM_MK2, r = ar.releases.find(x => x.version === '1.74')!
  const image = new Uint8Array(r.mainLen)
  const mod = (id: string, slot: number) => new Mod2({
    elemod: 2, id, version: '1', target: targetOf(ar, r),
    sections: { '.run': { align: 4, parts: [['hex', slot.toString(16).padStart(8, '0') + '00'.repeat(92)]] } },
    symbols: { m: ['.run', 0] },
    contribute: [{ to: 'core_machines', data: '00000000', relocs: [[0, 'abs32', 'sym:m', 0]] }],
  }, id)
  assert.deepEqual(mod('a', 36).impliedNames(image), ['machine:36'])
  assert.ok(check([mod('a', 36), mod('b', 36)], image).includes('a 1 and b 1 both claim machine:36'))
  assert.ok(!check([mod('c', 36), mod('d', 37)], image).some(p => p.includes('claim')))
  assert.deepEqual(mod('e', 53).impliedNames(image), [])
})

test('DN2 synth and filter claims use separate stable ID namespaces', () => {
  const dn = DIGITONE_MK2, r = dn.releases[0], image = new Uint8Array(r.mainLen)
  const mod = (id: string, type: number, table = 'mk2_voice_synths') => new Mod2({
    elemod: 2, id, version: '1', target: targetOf(dn, r),
    sections: { '.run': { align: 4, parts: [['hex', type.toString(16).padStart(8, '0') + '00'.repeat(12)]] } },
    symbols: { m: ['.run', 0] },
    contribute: [{ to: table, data: '00000000', relocs: [[0, 'abs32', 'sym:m', 0]] }],
  }, id)
  assert.deepEqual(mod('a', 5).impliedNames(image), ['mk2-synth:5'])
  assert.ok(check([mod('a', 5), mod('b', 5)], image).includes('a 1 and b 1 both claim mk2-synth:5'))
  assert.ok(!check([mod('a', 5), mod('b', 31)], image).some(p => p.includes('claim')))
  assert.deepEqual(mod('outside', 32).impliedNames(image), [])
  assert.deepEqual(mod('filter', 6, 'mk2_voice_filters').impliedNames(image), ['mk2-filter:6'])
  assert.ok(check([mod('a', 6, 'mk2_voice_filters'), mod('b', 6, 'mk2_voice_filters')], image)
    .includes('a 1 and b 1 both claim mk2-filter:6'))
  assert.ok(!check([mod('synth', 6), mod('filter', 6, 'mk2_voice_filters')], image).some(p => p.includes('claim')))
  assert.deepEqual(mod('pages', 7, 'mk2_voice_pages').impliedNames(image), ['mk2-pages:7'])
  assert.ok(check([mod('a', 7, 'mk2_voice_pages'), mod('b', 7, 'mk2_voice_pages')], image)
    .includes('a 1 and b 1 both claim mk2-pages:7'))
  assert.ok(!check([mod('synth', 7, 'mk2_voice_synths'), mod('pages', 7, 'mk2_voice_pages')], image)
    .some(p => p.includes('claim')))
})

test('DN2 machine bundles claim every pointer entry', () => {
  const dn = DIGITONE_MK2, r = dn.releases[0], image = new Uint8Array(r.mainLen)
  const mod = (id: string, slots: number[], table: string) => new Mod2({
    elemod: 2, id, version: '1', target: targetOf(dn, r),
    sections: { '.run': { align: 4, parts: [['hex', slots.map(slot => slot.toString(16).padStart(8, '0') + '00'.repeat(12)).join('')]] } },
    symbols: Object.fromEntries(slots.map((_, n) => [`m${n}`, ['.run', n * 16]])),
    contribute: [{ to: table, data: '00000000'.repeat(slots.length),
      relocs: slots.map((_, n) => [n * 4, 'abs32', `sym:m${n}`, 0]) }],
  }, id)
  for (const [table, prefix] of [['mk2_voice_synths', 'mk2-synth'], ['mk2_voice_filters', 'mk2-filter']]) {
    const bundle = mod('bundle', [6, 7], table)
    assert.deepEqual(bundle.impliedNames(image), [`${prefix}:6`, `${prefix}:7`])
    assert(check([bundle, mod('custom', [7], table)], image).includes(`bundle 1 and custom 1 both claim ${prefix}:7`))
    assert(!check([mod('bundle', [6, 7], table), mod('custom', [8], table)], image).some(p => p.includes('claim')))
  }
})

test('a site in bytes the device keeps stock is refused, in both formats, with the range and its reason', () => {
  for (const d of devices()) {
    for (const [lo, hi, why] of d.keep) {
      assert.ok(d.mainLoad <= lo && lo < hi && why, d.key)
      assert.ok(!d.imageFree.some(([a, b]) => a < hi && lo < b), d.key)
      assert.ok(!d.relocatable.some(([a, n]) => a < hi && lo < a + n), d.key)
      assert.ok(!d.protected.some(([a, b]) => a < hi && lo < b), d.key)
    }
  }
  assert.deepEqual(devices().filter(d => d.keep.length).map(d => d.key), ['octatrack'])
  const ot = OCTATRACK, r = ot.releases.find(x => x.version === '1.40C')!
  const [[lo, hi, why]] = ot.keep
  assert.deepEqual([lo, hi], [0x400d24d0, 0x400d24e0])
  assert.deepEqual(ot.imageFree[1], [0x400d24e0, 0x400d2ce0])
  const image = new Uint8Array(r.mainLen)                 // the kept bytes read zero in stock
  const sites = (at: number, n: number) => [{ addr: '0x' + at.toString(16), len: n, new: 'ff'.repeat(n), kind: 'data',
    stock_sha256: sha(new Uint8Array(n)) }]
  const whole = (at: number, n: number) => new Mod({ elemod: 1, id: 'w', version: '1', target: targetOf(ot, r), sites: sites(at, n) }, 'w')
  const mod2 = (at: number, n: number) => new Mod2({ elemod: 2, id: 'm', version: '1', target: targetOf(ot, r), sections: {},
    sites: sites(at, n) }, 'm')
  const core = new Mod2({ elemod: 2, id: 'core', version: '0.3', target: targetOf(ot, r),
    sections: { '.boot': { align: 4, parts: [['hex', '4e75']] } } }, 'core')
  for (const [at, n] of [[0x400d24d0, 1812], [0x400d24dc, 4], [0x400d24c0, 17]]) {
    const want = `m 1 site 0x${at.toString(16)} writes into 0x400d24d0-0x400d24e0, which stays stock on the Octatrack: ${why}`
    assert.deepEqual(check([core, mod2(at, n)], image), [want])
    assert.deepEqual(check1([whole(at, n)], image), [want.replace('m 1', 'w 1')])
  }
  assert.deepEqual(check([core, mod2(0x400d24e0, 1812)], image), [])
  assert.deepEqual(check1([whole(0x400d24e0, 1812)], image), [])
  assert.deepEqual(check1([whole(0x400d24c0, 16)], image), [])
})

test('the finished image against the stock one: link.py\'s image_problems, line for line', () => {
  const ot = OCTATRACK, r = ot.releases.find(x => x.version === '1.40C')!
  const stock = new Uint8Array(r.mainLen).fill(0x4e).map((_, i) => (i % 2 ? 0x71 : 0x4e))      // nops
  const end = ot.mainLoad + stock.length
  const problems = (img: Uint8Array, declared: [number, number, string][] = [], tailAt = end) => imageProblems(ot, stock, img, declared, tailAt)
  const tail = (img: Uint8Array) => Uint8Array.from([...img, 0x74, 0x61, 0x69, 0x6c])
  const stray = (at: number, n = 1, spacing = 1) => {
    const img = stock.slice()
    for (let i = 0; i < n; i++) img[at - ot.mainLoad + spacing * i] ^= 0xff
    return tail(img)
  }
  const hx = (v: number) => '0x' + v.toString(16).padStart(8, '0')
  // as it is: nothing differs but the tail
  assert.deepEqual(problems(tail(stock)), [])
  assert.deepEqual(problems(Uint8Array.from([...stock, 1])), [])
  // the tail starts where the stock image ends; the image is not shorter
  assert.deepEqual(problems(tail(stock), [], end + 4), [`the appended code starts at ${hx(end + 4)}, not at the end of the stock image, ${hx(end)}`])
  assert.deepEqual(problems(stock.slice(0, -4)), [`the linked image (${stock.length - 4} bytes) is shorter than the stock main OS (${stock.length} bytes)`])
  // a declaration inside the image
  assert.deepEqual(problems(tail(stock), [[end - 8, end + 4, 'a site']]), ['a site lies outside the stock image'])
  assert.deepEqual(problems(tail(stock), [[end - 8, end, 'a site']]), [])
  // bytes that differ and nothing declares
  const at = ot.mainLoad + 0x4000
  const why = ', and no site, DSP section or table of the mods declares it'
  assert.deepEqual(problems(stray(at, 6)), [`${hx(at)}-${hx(at + 6)} differs from stock${why}`])
  assert.deepEqual(problems(stray(at, 6), [[at, at + 4, 'a site']]), [`${hx(at + 4)}-${hx(at + 6)} differs from stock${why}`])
  assert.deepEqual(problems(stray(at, 6), [[at - 2, at + 8, 'a site']]), [])
  const many = problems(stray(at, 10, 16))
  assert.equal(many.length, 9)
  assert.equal(many[8], '2 more runs of bytes differ from stock outside every declaration')
  assert.deepEqual(problems(stray(at, 8, 16)).length, 8)
  // a protected or a kept range is stock whatever declares it
  const [plo, phi, pwhy] = ot.protected[0], [klo, khi, kwhy] = ot.keep[0]
  assert.deepEqual(problems(stray(plo + 8, 2), [[plo + 8, plo + 10, 'a site']]), [`${hx(plo)}-${hx(phi)} (${pwhy}) is not stock in the linked image`])
  assert.deepEqual(problems(stray(klo + 4, 2), [[klo + 4, klo + 6, 'a site']]), [`${hx(klo)}-${hx(khi)} (${kwhy}) is not stock in the linked image`])
  assert.deepEqual(problems(stray(plo - 16, 2), [[plo - 16, plo - 14, 'a site']]), [])
})

test('two mods that claim overlapping ranges of one space are refused (SPACE:0xLO-0xHI, half-open)', () => {
  const dn = DIGITONE_MK1, r = dn.releases.find(x => x.version === '1.43')!
  const image = new Uint8Array(r.mainLen)
  const mod = (id: string, names: string[]) => new Mod2({
    elemod: 2, id, version: '1', target: targetOf(dn, r),
    sections: { '.run': { align: 4, parts: [['hex', '00000000']] } }, symbols: {}, relocs: [],
    resources: { names },
  }, id)
  const msg = 'a 1 claims card:0x100-0x110 and b 1 claims card:0x10f-0x120: the ranges overlap'
  assert.ok(check([mod('a', ['card:0x100-0x110']), mod('b', ['card:0x10f-0x120'])], image).includes(msg))
  for (const other of ['card:0x110-0x120', 'drive:0x100-0x110', 'card:0x120-0x100'])
    assert.ok(!check([mod('a', ['card:0x100-0x110']), mod('b', [other])], image).some(x => x.includes('ranges overlap')), other)
})
