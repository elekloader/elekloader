// SPDX-License-Identifier: GPL-3.0-or-later
// The linker's checks that need no firmware: tests/test_units.py's counterparts.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { ANALOG_FOUR_MK2, ANALOG_RYTM_MK2, DIGITONE_MK2, DIGITAKT_MK1, DIGITONE_MK1, OCTATRACK, devices, targetOf } from '../src/devices.ts'
import { sha } from '../src/bytes.ts'
import { Mod, check as check1 } from '../src/elemod.ts'
import { Mod2, check, ddrFor, imageProblems, link, payloadId } from '../src/link.ts'

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

test('knob id ranges on the Analog Rytm MKII (core_params, core_param_locks)', () => {
  const ar = ANALOG_RYTM_MK2, r = ar.releases.find(x => x.version === '1.74')!
  const image = new Uint8Array(r.mainLen)
  const hex8 = (n: number) => n.toString(16).padStart(8, '0')
  const mod = (id: string, ...entries: [string, number, number][]) => new Mod2({
    elemod: 2, id, version: '1', target: targetOf(ar, r),
    sections: { '.run': { align: 4, parts: [['hex', entries.map(([, a, b]) => hex8(a) + hex8(b)).join('')]] } },
    symbols: Object.fromEntries(entries.map((_e, n) => [`e${n}`, ['.run', 8 * n]])),
    contribute: entries.map(([t], n) => ({ to: t, data: '00000000', relocs: [[0, 'abs32', `sym:e${n}`, 0]] })),
  }, id)
  const ids = (problems: string[]) => problems.filter(p => p.includes('ids'))
  const a = mod('a', ['core_params', 560, 573], ['core_param_locks', 560, 573])
  assert.deepEqual(a.idRanges(image), [['core_params', 560, 573], ['core_param_locks', 560, 573]])
  assert.deepEqual(ids(check([a, mod('b', ['core_params', 1, 4]), mod('c', ['core_params', 1, 4])], image)), [])
  assert.deepEqual(ids(check([mod('s', ['core_params', 470, 483])], image)), [
    "s 1: its core_params entry takes ids 470..483, and 469..554 are core's lockable parameters' ids; a mod's own ids start at 555"])
  assert.deepEqual(ids(check([mod('l', ['core_param_locks', 400, 460])], image)), [
    "l 1: its core_param_locks entry takes ids 400..460; that table's ids start at 555"])
  assert.ok(ids(check([a, mod('d', ['core_param_locks', 500, 561])], image))[0]
    .startsWith('d 1: its core_param_locks entry takes ids 500..561, and 469..554'))
  assert.deepEqual(ids(check([a, mod('e', ['core_param_locks', 570, 600])], image)), [
    'a 1 and e 1 both take ids 570..573 (core_params, core_param_locks)',
    'a 1 and e 1 both take ids 570..573 (core_param_locks, core_param_locks)'])
  assert.deepEqual(mod('f', ['core_params', 9, 3]).idRanges(image), [])
  assert.deepEqual(ids(check([mod('g', ['core_knob_faces', 39, 46])], image)), [])
  assert.deepEqual(ids(check([mod('h', ['core_knob_faces', 46, 470])], image)), [
    "h 1: its core_knob_faces entry takes ids 46..470, and 469..554 are core's lockable parameters' ids; a mod's own ids start at 555"])
})

test('mk1 machines claim their ids (machine:<id>, dnmachine:<id>)', () => {
  for (const [dev, table, prefix] of [[DIGITAKT_MK1, 'core_machines', 'machine'],
    [DIGITONE_MK1, 'sdk_alpha_machines', 'dnmachine']] as const) {
    const r = dev.releases[dev.releases.length - 1], image = new Uint8Array(r.mainLen)
    const mod = (id: string, slot: number) => new Mod2({
      elemod: 2, id, version: '1', target: targetOf(dev, r),
      sections: { '.run': { align: 4, parts: [['hex', slot.toString(16).padStart(8, '0') + '00'.repeat(28)]] } },
      symbols: { m: ['.run', 0] },
      contribute: [{ to: table, data: '00000000', relocs: [[0, 'abs32', 'sym:m', 0]] }],
    }, id)
    assert.deepEqual(mod('a', 30).impliedNames(image), [`${prefix}:30`])
    assert.ok(check([mod('a', 30), mod('b', 30)], image).includes(`a 1 and b 1 both claim ${prefix}:30`))
    assert.ok(!check([mod('a', 30), mod('b', 127)], image).some(p => p.includes('claim')))
    assert.deepEqual(mod('outside', 128).impliedNames(image), [])
  }
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
  assert.deepEqual(mod('sdk', 127, 'sdk_alpha_machines').impliedNames(image), ['mk2-synth:127'])
  assert.ok(check([mod('native', 7), mod('sdk', 7, 'sdk_alpha_machines')], image)
    .includes('native 1 and sdk 1 both claim mk2-synth:7'))
  assert.ok(!check([mod('sdk-a', 100, 'sdk_alpha_machines'), mod('sdk-b', 127, 'sdk_alpha_machines')], image)
    .some(p => p.includes('claim')))
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

test('the mods together add no more to a table than the profile\'s tableLimits (tests/test_link_refusals.py)', () => {
  const dn = DIGITONE_MK1, r = dn.releases.find(x => x.version === '1.44')!, image = new Uint8Array(r.mainLen)
  const core = new Mod2({ elemod: 2, id: 'core', version: '1', target: targetOf(dn, r),
    sections: { '.boot': { align: 4, parts: [['hex', '4e75']] } },
    collections: { sdk_alpha_machines: { entry: 4 } } }, 'core')
  const mod = (id: string, n: number) => new Mod2({ elemod: 2, id, version: '1', target: targetOf(dn, r), sections: {},
    contribute: [{ to: 'sdk_alpha_machines', data: '00000000'.repeat(n), relocs: [] }] }, id)
  const want = 'sdk_alpha_machines holds at most 15 entries on digitone-mk1 1.44; '
  assert.deepEqual(check([core, mod('a', 5), mod('b', 5), mod('c', 6)], image), [want + 'a 1, b 1 and c 1 add 16'])
  assert.deepEqual(check([core, mod('a', 16)], image), [want + 'a 1 adds 16'])
  assert.deepEqual(check([core, mod('a', 5), mod('b', 5), mod('c', 5)], image), [])
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

test('a payload id is link.py payload_id, and core-ar2 0.11 moves the Rytm\'s mods\' area', () => {
  assert.equal(payloadId('sidecar-check'), 0x3a4826f9)          // link.payload_id('sidecar-check')
  assert.equal(payloadId('porbxw'), payloadId('puscra'))         // the collision tests/test_link_refusals.py uses
  assert.notEqual(payloadId('porbxw'), payloadId('puscrb'))
  assert.deepEqual(ddrFor(ANALOG_RYTM_MK2, '0.10'), [0x47be0000, 0x47c00000])
  assert.deepEqual(ddrFor(ANALOG_RYTM_MK2, '0.11'), [0x47b10000, 0x47fff000])
  assert.deepEqual(ddrFor(ANALOG_RYTM_MK2, '0.20'), [0x47b10000, 0x47fff000])
})

test('core-ar2 0.21 starts the Rytm\'s mods\' area above stock\'s USB feedback ring', () => {
  const ring: [number, number] = [0x47b47440, 0x47b47480]
  const [lo, hi] = ddrFor(ANALOG_RYTM_MK2, '0.21')
  assert.deepEqual([lo, hi], [0x47b48000, 0x47fff000])
  assert.equal(lo % 0x1000, 0)
  assert.ok(lo >= ring[1])
  assert.deepEqual(ddrFor(ANALOG_RYTM_MK2, '0.22'), [lo, hi])
  const old = ddrFor(ANALOG_RYTM_MK2, '0.20')                    // the older cores' area still covers it
  assert.ok(old[0] < ring[1] && ring[0] < old[1])
})

test('core-a4mk2 0.6 moves the A4\'s mods\' area into the canary-checked gap', () => {
  assert.deepEqual(ddrFor(ANALOG_FOUR_MK2, '0.5'), [0x47be0000, 0x47c00000])
  assert.deepEqual(ddrFor(ANALOG_FOUR_MK2, '0.6'), [0x45650000, 0x47c00000])
})

test('knob ids from 555 past fixed ids, abs32 and abs16, kept by the lockfile (tests/test_link_refusals.py)', () => {
  const ar = ANALOG_RYTM_MK2, real = ar.releases.find(x => x.version === '1.74')!
  const image = new Uint8Array(real.mainLen)
  for (let i = 0; i < image.length; i += 2) image.set([0x4e, 0x71], i)
  const rel = { version: 'SYN', syxSha256: sha(new TextEncoder().encode('synthetic knobs')), mainSha256: sha(image), mainLen: image.length, binSha256: '' }
  ar.releases.push(rel)
  try {
    const target = targetOf(ar, rel)
    const hex8 = (n: number) => n.toString(16).padStart(8, '0')
    const core = (keeps: boolean) => ({ elemod: 2, id: 'core', version: '1', target,
      sections: { '.boot': { align: 4, len: 2, parts: [['hex', '4e75']] } }, symbols: { core_fn: ['.boot', 0] }, exports: ['core_fn'],
      collections: { ev_x: { entry: 4 }, core_params: { entry: 4 }, core_param_locks: { entry: 4 }, ...(keeps ? { core_store_ids: { entry: 4 } } : {}) },
      ...(keeps ? { imports: ['__store_gen', '__store_gen2'] } : {}) })
    const fixed = { elemod: 2, id: 'f', version: '1', target, requires: ['core'],
      sections: { '.run': { align: 4, len: 8, parts: [['hex', hex8(555) + hex8(560)]] } }, symbols: { e_f: ['.run', 0] },
      imports: ['core_fn'], contribute: [{ to: 'core_param_locks', data: '00000000', relocs: [[0, 'abs32', 'sym:e_f', 0]] }] }
    const knobbed = (id: string, count: number, short = 1) => ({ elemod: 2, id, version: '1', target, requires: ['core'], knobs: count,
      sections: { '.run': { align: 4, len: 12, parts: [['hex', '00'.repeat(12)]] } }, symbols: { ['e_' + id]: ['.run', 0] },
      imports: ['core_fn', '__knobs'],
      relocs: [['.run', 0, 'abs32', 'sym:__knobs', 0], ['.run', 4, 'abs32', 'sym:__knobs', count - 1], ['.run', 8, 'abs16', 'sym:__knobs', short]],
      contribute: [{ to: 'core_param_locks', data: '00000000', relocs: [[0, 'abs32', `sym:e_${id}`, 0]] }] })
    const mods = (...docs: Record<string, any>[]) => docs.map(d => new Mod2(structuredClone(d), d.id))
    const out = link(mods(core(false), fixed, knobbed('k', 4, 2), knobbed('j', 2)), image)
    assert.deepEqual([...out.knobs], [['j', [561, 2]], ['k', [563, 4]]])
    assert.deepEqual(out.knobNotes, ['j: knob ids 561..562', 'k: knob ids 563..566'])
    assert.equal(out.map.get('k:__knobs'), 563)
    const at = out.layout.run_load - ar.mainLoad + out.layout.sections['k .run'] - out.layout.ddr[0]
    const v = new DataView(out.image.buffer, out.image.byteOffset + at, 10)
    assert.deepEqual([v.getUint32(0), v.getUint32(4), v.getUint16(8)], [563, 566, 565])
    assert.deepEqual(check(mods(core(false), knobbed('k', 4, 4)), image), [
      'k 1: .run+0x8 is a abs16 reference to __knobs+4; one takes abs32 or abs16 with an addend below its "knobs" (4)'])
    assert.throws(() => new Mod2({ ...knobbed('k', 4), knobs: 1025 }, 'k'), { message: 'k: "knobs" is a count of knob ids, 0..1024' })
    const one = link(mods(core(true), knobbed('k', 4), knobbed('j', 2)), image)
    assert.deepEqual([...one.knobs], [['j', [555, 2]], ['k', [557, 4]]])
    const two = link(mods(core(true), knobbed('k', 4), knobbed('a', 3)), image, one.store)
    assert.deepEqual([...two.knobs], [['a', [561, 3]], ['k', [557, 4]]])
    assert.equal(two.store!.id, one.store!.id)
    const three = link(mods(core(true), knobbed('k', 4), knobbed('j', 2), knobbed('a', 3)), image, two.store)
    assert.deepEqual([...three.knobs], [['a', [561, 3]], ['j', [555, 2]], ['k', [557, 4]]])
    assert.deepEqual(three.knobNotes, ['j: back with knob ids 555..556'])
  } finally {
    ar.releases.splice(ar.releases.indexOf(rel), 1)
  }
})

test('machines by count and page ids by count, as tests/test_link_refusals.py', () => {
  const ar = ANALOG_RYTM_MK2, real = ar.releases.find(x => x.version === '1.74')!
  const image = new Uint8Array(real.mainLen)
  for (let i = 0; i < image.length; i += 2) image.set([0x4e, 0x71], i)
  const rel = { version: 'SYN', syxSha256: sha(new TextEncoder().encode('synthetic machines')), mainSha256: sha(image), mainLen: image.length, binSha256: '' }
  ar.releases.push(rel)
  try {
    const target = targetOf(ar, rel)
    const hex8 = (n: number) => n.toString(16).padStart(8, '0')
    const core = (keeps: boolean, version = '1') => ({ elemod: 2, id: 'core', version, target,
      sections: { '.boot': { align: 4, len: 2, parts: [['hex', '4e75']] } }, symbols: { core_fn: ['.boot', 0] }, exports: ['core_fn'],
      collections: { ev_x: { entry: 4 }, core_machines: { entry: 4 }, core_pages: { entry: 4 }, core_view_pages: { entry: 4 },
        ...(keeps ? { core_store_ids: { entry: 4 } } : {}) },
      ...(keeps ? { imports: ['__store_gen', '__store_gen2'] } : {}) })
    const machined = (id: string, wants: number[], slotAt: number | null = null) => {
      const n = slotAt === null ? wants.length : 1
      return { elemod: 2, id, version: '1', target, requires: ['core'],
        sections: { '.run': { align: 4, len: 4 * n, parts: [['hex', slotAt !== null ? hex8(slotAt) : '00'.repeat(4 * n)]] } },
        symbols: Object.fromEntries([...Array(n).keys()].map(i => [`e_${id}_${i}`, ['.run', 4 * i]])),
        ...(slotAt === null ? { machine_knobs: wants, imports: ['core_fn', '__machines'],
          relocs: [...Array(n).keys()].map(i => ['.run', 4 * i, 'abs32', 'sym:__machines', i]) } : { imports: ['core_fn'] }),
        contribute: [...Array(n).keys()].map(i => ({ to: 'core_machines', data: '00000000', relocs: [[0, 'abs32', `sym:e_${id}_${i}`, 0]] })) }
    }
    const mods = (...docs: Record<string, any>[]) => docs.map(d => new Mod2(structuredClone(d), d.id))
    const spare = { elemod: 2, id: 's', version: '1', target, requires: ['core'], imports: ['core_fn'], resources: { names: ['machine:45'] },
      sections: { '.run': { align: 4, len: 4, parts: [['hex', '4e714e75']] } } }
    const p = machined('p', [8, 4, 0]) as Record<string, any>
    p.relocs[1][2] = 'abs16'
    const out = link(mods(core(false), machined('h', [], 35), spare, p), image)
    assert.deepEqual([...out.machines], [['p', [36, 48, 52]]])
    assert.deepEqual(out.machineNotes, ['p: machine 0 in slot 36', 'p: machine 1 in slot 48', 'p: machine 2 in slot 52'])
    assert.equal(out.map.get('p:__machines+1'), 48)
    const at = out.layout.run_load - ar.mainLoad + out.layout.sections['p .run'] - out.layout.ddr[0]
    const v = new DataView(out.image.buffer, out.image.byteOffset + at, 12)
    assert.deepEqual([v.getUint32(0), v.getUint16(4), v.getUint16(6), v.getUint32(8)], [36, 48, 0, 52])
    assert.deepEqual([...link(mods(core(false, '0.12'), machined('q', [5, 3])), image).machines], [['q', [43, 34]]])
    assert.throws(() => link(mods(core(false), machined('q', [9])), image),
      { message: 'q asks for a machine with 9 knobs, and no machine slot with that many is free' })
    const over = machined('p', [4, 4]) as Record<string, any>
    over.relocs[1][4] = 2
    assert.deepEqual(check(mods(core(false), over), image), [
      'p 1: .run+0x4 is a abs32 reference to __machines+2; one takes abs32 or abs16 with an addend below its "machine_knobs" (2)'])
    assert.throws(() => new Mod2({ ...machined('p', [8]), machine_knobs: [17] }, 'p'),
      { message: 'p: "machine_knobs" is a list of up to 64 machines, each the knobs it needs (0..16)' })
    // the lockfile keeps them
    const one = link(mods(core(true), machined('p', [8]), machined('d', [8, 8])), image)
    assert.deepEqual([...one.machines], [['d', [35, 36]], ['p', [37]]])
    const two = link(mods(core(true), machined('p', [8]), machined('a', [8])), image, one.store)
    assert.deepEqual([...two.machines], [['a', [38]], ['p', [37]]])
    const three = link(mods(core(true), machined('p', [8]), machined('d', [8, 8]), machined('a', [8])), image, two.store)
    assert.deepEqual(three.machineNotes, ['d: machine 0 back in slot 35', 'd: machine 1 back in slot 36'])
    assert.throws(() => link(mods(core(true), machined('p', [8]), machined('h', [], 37)), image, three.store),
      (e: Error) => e.message.startsWith("machine slot 37 is claimed by number, and the lockfile gives it to p's machine 0"))
    // page ids
    const fixed = { elemod: 2, id: 'f', version: '1', target, requires: ['core'], imports: ['core_fn'],
      sections: { '.run': { align: 4, len: 16, parts: [['hex', hex8(12) + hex8(0) + hex8(8) + hex8(14)]] } },
      symbols: { pg: ['.run', 0], vp: ['.run', 8] },
      contribute: [{ to: 'core_pages', data: '00000000', relocs: [[0, 'abs32', 'sym:pg', 0]] },
        { to: 'core_view_pages', data: '00000000', relocs: [[0, 'abs32', 'sym:vp', 0]] }] }
    const paged = (id: string, n: number) => ({ elemod: 2, id, version: '1', target, requires: ['core'], pages: n,
      imports: ['core_fn', '__pages'], sections: { '.run': { align: 4, len: 8, parts: [['hex', '00'.repeat(8)]] } },
      relocs: [...Array(n).keys()].map(i => ['.run', 4 * i, 'abs32', 'sym:__pages', i]) })
    const pg = link(mods(core(false), fixed, paged('p', 2), paged('q', 1)), image)
    assert.deepEqual([...pg.pages], [['p', [15, 2]], ['q', [17, 1]]])
    assert.equal(pg.map.get('p:__pages'), 15)
  } finally {
    ar.releases.splice(ar.releases.indexOf(rel), 1)
  }
})

test('id pools: stable ids from a pool another mod declares, kept by the lockfile, as tests/test_link_refusals.py', () => {
  const ar = ANALOG_RYTM_MK2, real = ar.releases.find(x => x.version === '1.74')!
  const image = new Uint8Array(real.mainLen)
  for (let i = 0; i < image.length; i += 2) image.set([0x4e, 0x71], i)
  const rel = { version: 'SYN', syxSha256: sha(new TextEncoder().encode('synthetic pools')), mainSha256: sha(image), mainLen: image.length, binSha256: '' }
  ar.releases.push(rel)
  try {
    const target = targetOf(ar, rel)
    const core = { elemod: 2, id: 'core', version: '1', target,
      sections: { '.boot': { align: 4, len: 2, parts: [['hex', '4e75']] } }, symbols: { core_fn: ['.boot', 0] }, exports: ['core_fn'],
      collections: { ev_x: { entry: 4 }, core_store_ids: { entry: 4 } }, imports: ['__store_gen', '__store_gen2'] }
    const owner = (id = 'f', pools: Record<string, unknown> = { smpl: [1, 4] }) => ({ elemod: 2, id, version: '1', target, requires: ['core'],
      imports: ['core_fn'], id_pools: pools, sections: { '.run': { align: 4, len: 4, parts: [['hex', '4e714e75']] } } })
    const pooled = (id: string, n: number, add: number | null = null) => ({ elemod: 2, id, version: '1', target, requires: ['core'],
      pool_ids: { smpl: n }, imports: ['core_fn', '__pool_smpl'],
      sections: { '.run': { align: 4, len: 4 * n, parts: [['hex', '00'.repeat(4 * n)]] } },
      relocs: [...Array(n).keys()].map(i => ['.run', 4 * i, 'abs32', 'sym:__pool_smpl', add ?? i]) })
    const mods = (...docs: Record<string, any>[]) => docs.map(d => new Mod2(structuredClone(d), d.id))
    const one = link(mods(core, owner(), pooled('p', 2), pooled('d', 1)), image)
    assert.deepEqual(Object.fromEntries([...one.poolIds.get('smpl')!]), { d: [1], p: [2, 3] })
    assert.equal(one.map.get('p:__pool_smpl+1'), 3)
    const at = one.layout.run_load - ar.mainLoad + one.layout.sections['p .run'] - one.layout.ddr[0]
    const v = new DataView(one.image.buffer, one.image.byteOffset + at, 8)
    assert.deepEqual([v.getUint32(0), v.getUint32(4)], [2, 3])
    const two = link(mods(core, owner(), pooled('p', 2), pooled('a', 1)), image, one.store)
    assert.deepEqual(Object.fromEntries([...two.poolIds.get('smpl')!]), { a: [4], p: [2, 3] })
    const three = link(mods(core, owner(), pooled('p', 2), pooled('a', 1), pooled('d', 1)), image, two.store)
    assert.ok(three.poolNotes.includes('d: id 1 of pool smpl back'))
    assert.deepEqual(check(mods(core, pooled('p', 1)), image),
      ['p 1 asks for ids from the pool smpl ("pool_ids"), which no given mod declares'])
    assert.deepEqual(check(mods(core, owner(), owner('g'), pooled('p', 1)), image), ['f 1 and g 1 both declare the id pool smpl'])
    assert.deepEqual(check(mods(core, owner('f', { smpl: [1, 2] }), pooled('p', 2), pooled('d', 1)), image),
      ['the mods ask for 3 ids from the pool smpl, which has 2 (f 1 declares 1..2)'])
    assert.deepEqual(check(mods(core, owner(), pooled('p', 2, 2)), image), [
      'p 1: .run+0x0 is a abs32 reference to __pool_smpl+2; one takes abs32 or abs16 with an addend below its "pool_ids" (2)',
      'p 1: .run+0x4 is a abs32 reference to __pool_smpl+2; one takes abs32 or abs16 with an addend below its "pool_ids" (2)'])
    assert.throws(() => new Mod2({ ...owner(), id_pools: { smpl: [5, 4] } }, 'f'),
      { message: 'f: "id_pools" maps a pool\'s name (letters, digits, _) to [first, last], ids 0..65535, at most 4096' })
    assert.throws(() => new Mod2({ ...pooled('p', 1), pool_ids: { smpl: 0 } }, 'p'),
      { message: 'p: "pool_ids" maps a pool\'s name to the ids the mod asks for, 1..4096' })
    assert.throws(() => new Mod2({ ...pooled('p', 1), symbols: { __pool_x: ['.run', 0] } }, 'p'),
      { message: "p: __pool_x is the linker's (an id pool's ids), not a symbol of a mod" })
  } finally {
    ar.releases.splice(ar.releases.indexOf(rel), 1)
  }
})

test('a std1 mod subscribes through the core\'s std map (tests/test_link_refusals.py\'s std rows)', () => {
  const a4 = ANALOG_FOUR_MK2, r = a4.releases[0]
  const image = new Uint8Array(r.mainLen)
  const target = targetOf(a4, r)
  const core = (std: Record<string, unknown>, more: Record<string, unknown> = {}) => new Mod2({
    elemod: 2, id: 'core', version: '1', target, std,
    sections: { '.boot': { align: 4, len: 2, parts: [['hex', '4e75']] } },
    symbols: { core_fn: ['.boot', 0] }, exports: ['core_fn'],
    collections: { ev_x: { entry: 4 }, ...more },
  }, 'core')
  const sub = (to: string, std: boolean) => ({ to, ...(std ? { std: true } : {}), data: '00000000', relocs: [[0, 'abs32', 'sym:fn', 0]] })
  const mod = (id: string, events: string | null, ...contribute: Record<string, unknown>[]) => new Mod2({
    elemod: 2, id, version: '1', target, requires: ['core'], ...(events === null ? {} : { events }),
    sections: { '.run': { align: 4, len: 2, parts: [['hex', '4e75']] } }, symbols: { fn: ['.run', 0] },
    contribute,
  }, id)
  const stdOnly = (p: string[]) => p.filter(x => /std|map/.test(x))
  // = or prefix: the native table, beside a native mod's entry; thunk: the core's own std table
  const s = mod('s', 'std1', sub('ev_tick', true), sub('ev_key', true))
  const ok = check([core({ ev_tick: 'ev_x', ev_key: 'std_key' }, { std_key: { entry: 4 } }), mod('n', null, sub('ev_x', false)), s], image)
  assert.deepEqual(stdOnly(ok), [])
  assert.deepEqual(s.contribute.map(c => [c.event, c.to]), [['ev_tick', 'ev_x'], ['ev_key', 'std_key']])
  assert.deepEqual(stdOnly(check([core({ ev_tick: 'ev_x' }), mod('s', 'std1', sub('ev_key', true))], image)),
    ['s 1 subscribes to ev_key, which core 1 does not map'])
  assert.deepEqual(stdOnly(check([core({ ev_tick: 'ev_x' }), mod('s', 'std1', sub('ev_x', true))], image)),
    ['s 1 subscribes to ev_x, which core 1 does not map'])
  assert.deepEqual(stdOnly(check([core({}), mod('s', 'std1', sub('ev_key', true))], image)),
    ['s 1 subscribes to std1 events, and core 1 maps none'])
  assert.deepEqual(stdOnly(check([core({ ev_key: 'std_key' }), mod('n', null, sub('ev_x', false))], image)),
    ['core 1: its std map sends ev_key to std_key, which no given mod declares'])
  assert.throws(() => mod('m', 'std2'), /m: "events" is 'std2'; the standard event set is "std1"/)
  assert.throws(() => core({ ev_key: 3 }), /core: "std" maps each standard event's name to a table of the core/)
  assert.throws(() => mod('m', null, sub('ev_key', true)), /m contribution 0: a standard event's subscription \("std": true\) needs the mod's "events"/)
  assert.throws(() => new Mod2({ ...mod('m', null).doc, std: { ev_key: 'ev_x' } }, 'm'), /m: a "std" map is a core's \(the mod with \.boot\)/)
})
