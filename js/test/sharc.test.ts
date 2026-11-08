// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { sha } from '../src/bytes.ts'
import { DIGITONE_MK2, targetOf } from '../src/devices.ts'
import { ModError } from '../src/elemod.ts'
import { Mod2, check, link } from '../src/link.ts'
import * as formats from '../src/formats.ts'
import * as sharc from '../src/sharc.ts'

const corpus = JSON.parse(readFileSync(new URL('../../tests/fixtures/sharc.json', import.meta.url), 'utf8'))
const bytes = (hex: string): Uint8Array => Uint8Array.from(Buffer.from(hex, 'hex'))
const original = bytes(corpus.original)
const profile = DIGITONE_MK2.releases[0].sharc!
const inertArtifact = JSON.parse(readFileSync(new URL('../../examples/sharc-sine/sine.sharc.json', import.meta.url), 'utf8'))
inertArtifact.entries = null
inertArtifact.stack_bytes = 0
const synthetic = { ...profile, decodedSha256: sha(original), ownedRanges: [[0x20002000, 0x20002020] as [number, number]] }
const refusal = (fn: () => unknown, error: string) => assert.throws(fn, e => e instanceof ModError && e.message === error)

test('active DN2 keyed callbacks accept bounded mods and require the pinned bridge', () => {
  const rel = DIGITONE_MK2.releases[0], target = targetOf(DIGITONE_MK2, rel)
  const bridge = JSON.parse(readFileSync(new URL('../../mods/core-dn2/bridge.sharc.json', import.meta.url), 'utf8'))
  const core = (artifact: unknown) => new Mod2({ elemod: 2, id: 'core', version: '0.1', target,
    sections: { '.boot': { align: 4, parts: [['hex', '00000000']] } }, sharc: artifact }, 'core')
  const image = new Uint8Array(rel.mainLen)
  const problems = (bridgeArtifact: unknown, artifact: unknown) => check([core(bridgeArtifact), new Mod2({ elemod: 2, id: 'sine', version: '0.1', target,
    sections: {}, sharc: artifact }, 'sine')], image)
  const original = JSON.parse(readFileSync(new URL('../../examples/sharc-sine/sine.sharc.json', import.meta.url), 'utf8'))
  assert.deepEqual(problems(bridge, original), [])
  const asm = JSON.parse(readFileSync(new URL('../../examples/sharc-sine-asm/sine.sharc.json', import.meta.url), 'utf8'))
  assert.deepEqual(problems(bridge, asm), [])
  // Packaging acceptance does not qualify these synthetic bytes for execution.
  for (const source of [original, asm]) for (const mutation of ['code', 'data', 'entry', 'stack']) {
    const changed = structuredClone(source)
    if (mutation === 'code' || mutation === 'data') {
      const segment = changed.segments[mutation === 'data' ? 1 : 0]
      const data = bytes(segment.hex)
      data[4] ^= 1
      segment.hex = Buffer.from(data).toString('hex')
    } else if (mutation === 'entry') changed.entries.render_offset_bytes += 2
    else changed.stack_bytes -= 4
    assert.deepEqual(problems(bridge, changed), [], mutation)
  }
  for (const mutation of ['code', 'entry', 'stack']) {
    const changed = structuredClone(bridge)
    if (mutation === 'code') changed.segments[0].hex = '0000' + changed.segments[0].hex.slice(4)
    else if (mutation === 'entry') changed.entries.hook_offset_bytes += 2
    else changed.stack_bytes -= 4
    assert(problems(changed, original).some(p => p.includes('unqualified active SHARC core bytes or entry')), mutation)
  }
  // The master-output mod runs on the stock Audio Task's stack; only machines get their own.
  const over = structuredClone(original)
  over.stack_bytes = profile.stackMax! + 4
  refusal(() => sharc.modulePayload(over, profile), 'SHARC stack contribution exceeds the profile limit')
  const large = structuredClone(original)
  large.stack_bytes = profile.stackMax
  assert(problems(bridge, large).includes('active SHARC stack exceeds the profile limit'))
  for (const [entry, offset] of [['render_offset_bytes', 16384], ['init_offset_bytes', 1]] as const) {
    const changed = structuredClone(original)
    changed.entries[entry] = offset
    refusal(() => sharc.modulePayload(changed, profile), 'SHARC entry is not in owned code')
  }
  const changed = structuredClone(original)
  const changedCode = bytes(changed.segments[0].hex)
  changedCode[328] ^= 1
  changed.segments[0].hex = Buffer.from(changedCode).toString('hex')
  assert.deepEqual(problems(bridge, changed), [])

})

test('active code guard excludes callback entries; inert payloads execute no code', () => {
  assert.equal(profile.codeGuardBytes, 64)
  const original = JSON.parse(readFileSync(new URL('../../examples/sharc-sine/sine.sharc.json', import.meta.url), 'utf8'))
  const badTail = structuredClone(original)
  badTail.segments[0].hex = badTail.segments[0].hex.slice(0, -2) + '01'
  refusal(() => sharc.modulePayload(badTail, profile), 'SHARC mod code needs its profiled NOP read-ahead guard')
  badTail.entries = null; badTail.stack_bytes = 0
  sharc.modulePayload(badTail, profile)
  original.entries.render_offset_bytes = 16320
  refusal(() => sharc.modulePayload(original, profile), 'SHARC entry is in the instruction read-ahead guard')
  original.entries.render_offset_bytes = 16318
  sharc.modulePayload(original, profile)
})

test('prelinked inert payload covers only its DN2 owner', () => {
  const payload = sharc.modulePayload(inertArtifact, profile)
  assert.deepEqual(payload.owner, profile.l2AppRange)
  assert.equal(payload.entries, null)
  const bad = structuredClone(inertArtifact)
  bad.zero[0].size_bytes += 4
  refusal(() => sharc.modulePayload(bad, profile), 'SHARC zero range is unaligned or exceeds its owner')
  const active = structuredClone(inertArtifact)
  active.entries = { init_offset_bytes: 0, render_offset_bytes: 16384 }
  refusal(() => sharc.modulePayload(active, profile), 'SHARC entry is not in owned code')
})

for (const c of corpus.parse_cases) test(`SHARC parse: ${c.name}`, () => {
  const data = bytes(c.hex)
  if (c.error) refusal(() => sharc.parse(data, profile), c.error)
  else {
    const records = sharc.parse(data, profile)
    assert.equal(records.length, c.records)
    assert.deepEqual(Buffer.concat(records.map(r => r.raw)), Buffer.from(data))
    assert.deepEqual(sharc.insert(data, { ...profile, decodedSha256: sha(data) }), data)
  }
})

for (const c of corpus.span_cases) test(`SHARC address: ${c.name}`, () => {
  const action = () => sharc.byteSpan(profile, c.address, c.size, c.unit)
  if (c.error) refusal(action, c.error)
  else assert.deepEqual(action(), c.result)
})

for (const c of corpus.insert_cases) test(`SHARC insert: ${c.name}`, () => {
  const p = { ...synthetic, ownedRanges: c.owned ?? synthetic.ownedRanges, decodedMax: c.limit ?? profile.decodedMax }
  const action = () => sharc.insert(original, p, c.loads.map(([a, h]: [number, string]) => [a, bytes(h)]), c.fills)
  if (c.error) refusal(action, c.error)
  else assert.equal(Buffer.from(action()).toString('hex'), c.result)
  assert.equal(Buffer.from(original).toString('hex'), corpus.original)
})

test('SHARC golden keeps ordered overlap and rewrites only the last FIRST', () => {
  const result = sharc.insert(original, synthetic, [[0x20002000, bytes('11223344')]], [[0x20002010, 8, 0]])
  assert.equal(sha(result), corpus.expected_sha256)
  const records = sharc.parse(result, synthetic)
  assert.deepEqual(records.map(r => r.offset), [0, 16, 36, 52, 68, 88, 108, 124])
  assert.deepEqual(records.map(r => r.flags), [0x5001, 1, 0x801, 0x5001, 1, 1, 0x101, 0x8001])
  assert.deepEqual(records.filter(r => r.flags === sharc.FIRST).map(r => r.argument), [36, 72])
  assert.deepEqual([0, 1, 2, 4, 7].map(i => records[i].raw), sharc.parse(original, profile).filter((_, i) => i !== 3).map(r => r.raw))
  assert.equal(records[1].target, records[4].target)
  refusal(() => sharc.insert(result, synthetic), 'SHARC stock DSP hash does not match the profile')
  assert.deepEqual([records[6].count, records[6].raw.length], [8, 16])
})

test('SHARC decoded limit, hash identity and payload type', () => {
  assert.equal(sharc.parse(original, { ...profile, decodedMax: 104 }).length, 6)
  refusal(() => sharc.parse(original, { ...profile, decodedMax: 103 }), 'SHARC decoded stream exceeds the profile limit')
  refusal(() => sharc.insert(original, profile), 'SHARC stock DSP hash does not match the profile')
  refusal(() => sharc.insert(original, synthetic, [[0x20002000, [1, 2, 3, 4] as unknown as Uint8Array]]), 'SHARC load payload must be bytes')
})

test('SHARC keeps copies with Uint8Array and Node Buffer inputs', () => {
  for (const mutable of [original.slice(), Buffer.from(original)]) {
    const records = sharc.parse(mutable, profile), noop = sharc.insert(mutable, synthetic)
    mutable[16] = 0
    assert.deepEqual(records[1].raw, original.slice(16, 36))
    assert.deepEqual(noop, original)
  }
})

test('SHARC preserves a historical protected load during insertion elsewhere', () => {
  const data = bytes(corpus.parse_cases.find((c: {name: string}) => c.name === 'original_protected_load').hex)
  const p = { ...synthetic, decodedSha256: sha(data) }
  const result = sharc.insert(data, p, [[0x20002000, bytes('11223344')]])
  assert.deepEqual(sharc.parse(result, p)[1].raw, data.slice(16, 36))
})

test('SHARC instruction patch requires exact unique stock load', () => {
  const data = bytes(corpus.parse_cases.find((c: {name: string}) => c.name === 'original_protected_load').hex)
  const p = { ...synthetic, decodedSha256: sha(data), patchRanges: [[0x20001000, 0x20001004] as [number, number]] }
  const patched = sharc.insert(data, p, [], [], [[0x20001000, bytes('0506'), bytes('aabb')]])
  assert.equal(Buffer.from(patched.subarray(84, 88)).toString('hex'), 'aabb0708')
  const companion = { ...p, patchRanges: [], dataSites: { 0x20001000: ['0506', 'aabb'] as [string, string] } }
  assert.deepEqual(sharc.insert(data, companion, [], [], [[0x20001000, bytes('0506'), bytes('aabb')]]), patched)
  refusal(() => sharc.insert(data, companion, [], [], [[0x20001000, bytes('0506'), bytes('aacc')]]),
    'SHARC patch is outside the profile patch range')
  refusal(() => sharc.insert(data, p, [], [], [[0x20001000, bytes('0006'), bytes('aabb')]]),
    'SHARC patch stock instruction does not match')
  refusal(() => sharc.insert(data, { ...p, patchRanges: [] }, [], [], [[0x20001000, bytes('0506'), bytes('aabb')]]),
    'SHARC patch is outside the profile patch range')
  refusal(() => sharc.insert(original, { ...p, decodedSha256: sha(original) }, [], [], [[0x20001000, bytes('0506'), bytes('aabb')]]),
    'SHARC patch needs one unambiguous stock load')
})

test('SHARC owner-stock exact no-op', { skip: !process.env.ELEKLOADER_DN2_SYX }, async () => {
  const raw = readFileSync(process.env.ELEKLOADER_DN2_SYX!)
  const [parsed, device, release] = await formats.load(raw)
  assert.equal(device, DIGITONE_MK2)
  assert.equal(release, DIGITONE_MK2.releases[0])
  const stored = parsed.stored.get(profile.section)!, decoded = parsed.section(profile.section)
  assert.deepEqual([sha(stored), stored.length], [profile.storedSha256, profile.storedLen])
  assert.deepEqual([sha(decoded), decoded.length], [profile.decodedSha256, profile.decodedLen])
  const records = sharc.parse(decoded, profile)
  assert.equal(records.length, 95)
  assert.deepEqual(Buffer.concat(records.map(r => r.raw)), Buffer.from(decoded))
  assert.deepEqual(sharc.insert(decoded, profile), decoded)
  const spans = records.filter(r => r.flags === sharc.LOAD || r.flags === sharc.FILL)
    .map(r => sharc.byteSpan(profile, r.target, r.count, 'loader-byte', false))
  assert.equal(spans.reduce((n, [a, b], i) => n + spans.slice(i+1).filter(([c, d]) => a < d && c < b).length, 0), 15)
  refusal(() => sharc.insert(decoded, profile, [], [[0x200b0000, 4, 0]]), 'SHARC new load or fill is outside the profile-owned ranges')
  const patched = sharc.insert(decoded, profile, [], [], [[0x2838489a, bytes('8715ffff78ff'), bytes('3e06160006eb')]])
  const changed = [...decoded.keys()].filter(i => decoded[i] !== patched[i])
  assert.equal(changed.length, 6)
  assert.equal(Buffer.from(patched.subarray(changed[0], changed[0] + 6)).toString('hex'), '3e06160006eb')
  // core-dn2's DSP peek, as the linker inserts it with any SHARC mod: owned, and its hook matches stock.
  const [peekLo, peekHi] = profile.peekRange!, peek = bytes(profile.peekCode!), [site, old, replacement] = profile.peekHook!
  const [idleAddress, [idleOld, idleNew]] = Object.entries(profile.dataSites!)[0]
  const idleSite = Number(idleAddress)
  const withPeek = sharc.insert(decoded, profile, [[peekLo, peek]], [[peekLo + peek.length, peekHi - peekLo - peek.length, 0]],
    [[site, bytes(old), bytes(replacement)], [idleSite, bytes(idleOld), bytes(idleNew)]])
  const peekRecords = sharc.parse(withPeek, profile)
  assert(peekRecords.some(r => r.flags === sharc.LOAD && r.target === peekLo && Buffer.from(r.raw.subarray(16, 16 + peek.length)).equals(peek)))
  const outputs = formats.write(parsed, parsed.stored.get(device.mainSection)!, device)
  assert.deepEqual(Buffer.from(outputs.syx), raw)
  formats.verify(outputs, parsed, formats.mainImage(parsed, device), device)
})

const dispatcher = JSON.parse(readFileSync(new URL('../../mods/machines-dn2/dispatch.sharc.json', import.meta.url), 'utf8'))

/** A kernel artifact at the probe base, as tests/test_sharc_kernels.py's kernel(): its code jumps to its own word 2
 * and its data points at its own byte 4. */
function kernelDoc(...kernels: [string, number, Record<string, unknown>?][]): Record<string, any> {
  return { abi: 'dn2-kernel-v1', code: '063e00bc8402' + '00'.repeat(6), data: '04800920' + '00'.repeat(12), code_pc16: 0xbc8400,
    data_address: 0x20098000, data_align: 8, relocs: [['code', 3, 3, 'code'], ['data', 0, 4, 'data']],
    kernels: kernels.map(([kind, id, extra]) => ({ kind, id, entry: 2, ...(kind === 'sdk' ? { knobs: [] } : {}), ...extra })),
    exports: {} }
}

const TABLE: Record<string, string> = { sdk: 'sdk_alpha_machines', 'mk2-voice': 'mk2_voice_synths', 'mk2-filter': 'mk2_voice_filters' }
const dnTarget = targetOf(DIGITONE_MK2, DIGITONE_MK2.releases[0])
const dnCore = () => new Mod2({ elemod: 2, id: 'core', version: '0.2', target: dnTarget,
  sections: { '.boot': { align: 4, parts: [['hex', '4e75']] } },
  collections: { mk2_voice_synths: { entry: 4 }, mk2_voice_filters: { entry: 4 }, sdk_alpha_machines: { entry: 4 } } }, 'core')
const machinesDn2 = () => new Mod2({ elemod: 2, id: 'machines-dn2', version: '0.3', target: dnTarget, sections: {},
  sharc: dispatcher }, 'machines-dn2')
/** A mod registering descriptors (id first) and carrying the kernels, as tests/test_sharc_kernels.py's mod(). */
function kernelMod(name: string, kernels: [string, number, Record<string, unknown>?][],
  { registers, sharcDoc, g }: { registers?: [string, number][]; sharcDoc?: unknown; g?: [string, number] } = {}): Mod2 {
  registers ??= kernels.map(([kind, id]) => [TABLE[kind], id])
  let text = ''
  const symbols: Record<string, [string, number]> = {}, relocs: unknown[] = [], contribute: unknown[] = []
  registers.forEach(([table, id], n) => {
    symbols[`d${n}`] = ['.run', 32 * n]
    text += id.toString(16).padStart(8, '0') + '00'.repeat(28)
    contribute.push({ to: table, data: '00000000', relocs: [[0, 'abs32', `sym:d${n}`, 0]] })
  })
  if (g) {
    const at = text.length / 2
    symbols.g = ['.run', at]; symbols.label = ['.run', at + 8]
    text += '00000000' + g[1].toString(16).padStart(8, '0') + Buffer.from(g[0], 'latin1').toString('hex') + '00'
    registers.forEach((_, n) => relocs.push(['.run', 32 * n + 16, 'abs32', 'sym:g', 0]))
    relocs.push(['.run', at, 'abs32', 'sym:label', 0])
  }
  text += '00'.repeat((4 - text.length / 2 % 4) % 4)
  const doc: Record<string, unknown> = { elemod: 2, id: name, version: '0.1', target: dnTarget,
    sections: text ? { '.run': { align: 4, parts: [['hex', text]] } } : {}, symbols, relocs, contribute }
  if (sharcDoc !== undefined || kernels.length) doc.sharc = sharcDoc ?? kernelDoc(...kernels)
  return new Mod2(doc, name)
}

test('DN2 kernel artifacts: what they may hold (sharc.kernel_payload)', () => {
  const p = sharc.kernelPayload(kernelDoc(['sdk', 9, { knobs: [[0, 127], [-12, 12]] }], ['mk2-voice', 31],
    ['mk2-filter', 6, { state_bytes: 12 }]), profile)
  assert.deepEqual(p.kernels, [{ kind: 'sdk', id: 9, entry: 2, knobs: [[0, 127], [-12, 12]] },
    { kind: 'mk2-voice', id: 31, entry: 2, state_bytes: 0 }, { kind: 'mk2-filter', id: 6, entry: 2, state_bytes: 12 }])
  assert.deepEqual(sharc.kernelPayload(dispatcher, profile).kernels, [])
  const shape = 'SHARC kernel: a kernel is {kind: sdk, id, entry, knobs} or {kind: mk2-voice|mk2-filter, id, entry[, state_bytes]}'
  for (const [doc, error] of [
    [kernelDoc(['sdk', 4]), 'SHARC kernel: sdk id 4 is outside 5..127'],
    [kernelDoc(['mk2-voice', 32]), 'SHARC kernel: mk2-voice id 32 is outside 5..31'],
    [kernelDoc(['mk2-filter', 5]), 'SHARC kernel: mk2-filter id 5 is outside 6..31'],
    [kernelDoc(['sdk', 7], ['mk2-voice', 7]), 'SHARC kernel: two voice kernels with id 7'],
    [kernelDoc(['sdk', 7, { entry: 6 }]), 'SHARC kernel: sdk 7 entry is outside its code'],
    [kernelDoc(['sdk', 7, { knobs: [[3, 3]] }]), 'SHARC kernel: sdk 7 knobs are up to 15 [min, max] pairs of int32, min below max'],
    [kernelDoc(['sdk', 7, { knobs: [[0, 2 ** 31]] }]), 'SHARC kernel: sdk 7 knobs are up to 15 [min, max] pairs of int32, min below max'],
    [kernelDoc(['sdk', 7, { state_bytes: 4 }]), shape],
    [kernelDoc(['mk2-voice', 7, { knobs: [] }]), shape],
    [kernelDoc(['voice', 7]), shape],
    [kernelDoc(['mk2-voice', 7, { state_bytes: 6 }]), 'SHARC kernel: mk2-voice 7 state_bytes must be whole 32-bit words'],
    [kernelDoc(), 'SHARC kernel: no kernel and no dispatcher'],
    [{ ...kernelDoc(), exports: { 'dn2_voice_hook.': ['code', 0] } },
      'SHARC kernel: a dispatcher defines all of dn2_voice_hook., dn2_filter_hook., dn2_filter_capture., dn2_voice_table., dn2_filter_table.'],
    [{ ...kernelDoc(['sdk', 7]), relocs: [['code', 10, 4, 'code']] }, "SHARC kernel: invalid relocation ['code', 10, 4, 'code']"],
  ] as const) refusal(() => sharc.kernelPayload(doc, profile), error)
})

test('DN2 kernels from several mods share the pool (sharc.place_kernels)', () => {
  const docs: [string, Record<string, any>][] = [
    ['a-synths', kernelDoc(['sdk', 9, { knobs: [[-12, 12], [0, 2 ** 31 - 1]] }], ['mk2-voice', 5, { state_bytes: 12 }])],
    ['machines-dn2', dispatcher],
    ['b-filters', kernelDoc(['mk2-filter', 6], ['mk2-filter', 31, { state_bytes: 40 }])],
    ['c-synth', kernelDoc(['mk2-voice', 31])]]
  const [loads, fills, patches, layout] = sharc.placeKernels(docs.map(([id, doc]) => [id, sharc.kernelPayload(doc, profile)]), profile)
  assert.deepEqual(layout.mods.map(m => m.id), ['machines-dn2', 'a-synths', 'b-filters', 'c-synth'])
  const at = new Map(loads.map(([a, d]) => [a, d]))
  const pc = Object.fromEntries(layout.mods.map(m => [m.id, sharc.pc16(profile, m.code[0])]))
  const tables = new DataView(at.get(layout.mods[0].data[0])!.slice().buffer)
  const entry = (offset: number, id: number) => [0, 4, 8, 12].map(i => tables.getUint32(offset + 16 * id + i, true))
  const sdk = entry(0, 9)
  assert.deepEqual([sdk[0], sdk[2]], [pc['a-synths'] + 2, 256])
  assert.deepEqual([...at.get(sdk[3])!], [...sharc.knobMap([[-12, 12], [0, 2 ** 31 - 1]])])
  assert.deepEqual(entry(0, 31), [pc['c-synth'] + 2, 0, 0, 0])
  assert.deepEqual(entry(2048, 6), [pc['b-filters'] + 2, 0, 0, 0])
  assert.deepEqual(entry(2048, 31).slice(2), [40, 0])
  assert(fills.some(([a, n]) => a === entry(2048, 31)[1] && n === 640))
  assert.deepEqual(patches.map(([site]) => site), [profile.voiceHook![0], profile.filterHook![0], profile.filterCapture![0]])
  assert.throws(() => sharc.placeKernels([['machines-dn2', sharc.kernelPayload(dispatcher, profile)],
    ['big', sharc.kernelPayload(kernelDoc(['mk2-voice', 5, { state_bytes: 4096 }], ['mk2-voice', 6, { state_bytes: 4096 }]), profile)]],
  profile), /DN2 kernels need \d+ bytes; the machine pool has 131072$/)
})

test('DN2 kernels: the linker\'s refusals, refused and passed (tests/test_sharc_kernels.py)', () => {
  const image = new Uint8Array(DIGITONE_MK2.releases[0].mainLen)
  const problems = (...mods: Mod2[]) => check([dnCore(), ...mods], image)
  assert.deepEqual(problems(machinesDn2(), kernelMod('a', [['sdk', 9], ['mk2-voice', 5]]), kernelMod('b', [['mk2-voice', 31]]),
    kernelMod('c', [['mk2-filter', 6], ['mk2-filter', 7]])), [])
  assert(problems(kernelMod('a', [['mk2-voice', 5]])).includes('DN2 machine kernels need one dispatcher (machines-dn2); this build has none'))
  const master = kernelMod('master', [], { sharcDoc: inertArtifact })
  assert(problems(machinesDn2(), master, kernelMod('a', [['mk2-voice', 5]])).includes('DN2 machine kernels do not combine with master 0.1'))
  assert.deepEqual(problems(machinesDn2(), master), [])
  assert(problems(master, kernelMod('m2', [], { sharcDoc: inertArtifact })).includes('a build takes one SHARC mod; this one has master 0.1, m2 0.1'))
  assert(problems(machinesDn2(), kernelMod('a', [['sdk', 9]], { registers: [] }), kernelMod('b', [['mk2-voice', 9]], { registers: [] }))
    .includes('a 0.1 and b 0.1 both have a DN2 voice kernel with id 9'))
  assert.deepEqual(problems(machinesDn2(), kernelMod('a', [], { registers: [['mk2_voice_filters', 7]] })),
    ['a 0.1: its mk2_voice_filters entry 7 needs a SHARC mk2-filter kernel with that id'])
  assert.deepEqual(problems(machinesDn2(), kernelMod('a', [], { registers: [['sdk_alpha_machines', 40]] })), [])
  assert.deepEqual(problems(machinesDn2(), kernelMod('b', [['mk2-voice', 9]], { registers: [['mk2_voice_filters', 9]] })), [
    'b 0.1: its mk2_voice_filters entry 9 needs a SHARC mk2-filter kernel with that id',
    'b 0.1: its SHARC mk2-voice kernel 9 has no registration with that id in its mod'])
  const refusalG = 'g 0.1: DN2 filter G needs a label of 1..5 printable ASCII characters and a default of 0..127'
  for (const g of [['MODE', 0], ['HIGH5', 127], undefined] as const)
    assert.deepEqual(problems(machinesDn2(), kernelMod('g', [['mk2-filter', 8]], { g: g as [string, number] | undefined })), [])
  for (const g of [['MODE', 128], ['TOOWIDE', 1], ['', 1], ['M\tX', 1]] as const)
    assert.deepEqual(problems(machinesDn2(), kernelMod('g', [['mk2-filter', 8]], { g: [...g] })), [refusalG])
})

test('a DN2 kernel build jumps to the dispatcher and leaves the master hook', { skip: !process.env.ELEKLOADER_DN2_SYX }, async () => {
  const [parsed, device] = await formats.load(readFileSync(process.env.ELEKLOADER_DN2_SYX!))
  const image = formats.mainImage(parsed, device), stream = parsed.section(profile.section)
  const mods = [machinesDn2(), kernelMod('a', [['sdk', 9], ['mk2-voice', 5]]), kernelMod('b', [['mk2-filter', 6]])]
  const out = link([dnCore(), ...mods], image, null, stream)
  const reverse = link([...mods].reverse().concat(dnCore()), image, null, stream)
  assert.deepEqual(out.sharcStream, reverse.sharcStream)
  const at = (decoded: Uint8Array, site: number, size = 6) => {
    const r = sharc.parse(decoded, profile).find(r => r.flags === sharc.LOAD && r.target <= site && site + size <= r.target + r.count)!
    return r.raw.subarray(16 + site - r.target, 16 + size + site - r.target)
  }
  const [, , patches] = sharc.placeKernels(mods.map(m => [m.id, m.sharc as sharc.KernelPayload]), profile)
  for (const [site, old, replacement] of patches) {
    assert.deepEqual(at(stream, site, old.length), old)
    assert.deepEqual(at(out.sharcStream!, site, replacement.length), replacement)
  }
  assert.deepEqual(at(out.sharcStream!, profile.hook![0]), at(stream, profile.hook![0]))
  assert.deepEqual(out.layout.sharc!.kernels!.mods.map(m => m.id), ['machines-dn2', 'a', 'b'])
  assert.equal(link([dnCore(), machinesDn2()], image, null, stream).sharcStream, null)
  // Without kernels the dispatcher changes nothing: another SHARC mod links as without machines-dn2.
  const master = () => kernelMod('master', [], { sharcDoc: inertArtifact })
  const alone = link([dnCore(), master()], image, null, stream), beside = link([dnCore(), machinesDn2(), master()], image, null, stream)
  assert.deepEqual([beside.sharcStream, beside.layout.sharc], [alone.sharcStream, alone.layout.sharc])
})

test('DN2 machine IDs can span stored parts without losing the SHARC registration', () => {
  const dn = DIGITONE_MK2, rel = dn.releases[0], target = targetOf(dn, rel)
  const image = new Uint8Array(rel.mainLen), machineId = bytes('00000005')
  image.set(machineId, 0x100)
  const core = new Mod2({ elemod: 2, id: 'core', version: '0.1', target,
    sections: { '.boot': { align: 4, parts: [['hex', '00000000']] } },
    collections: { mk2_voice_synths: { entry: 4 } } }, 'core')
  const mod = (parts: unknown[], relocs: unknown[] = []) => new Mod2({
    elemod: 2, id: 'voice', version: '0.1', target,
    sections: { '.run': { align: 4, parts } }, symbols: { descriptor: ['.run', 0] }, relocs,
    contribute: [{ to: 'mk2_voice_synths', data: '00000000', relocs: [[0, 'abs32', 'sym:descriptor', 0]] }],
    sharc: kernelDoc(['mk2-voice', 5]),
  }, 'voice')
  for (const split of [1, 2, 3]) for (const leftStock of [false, true]) for (const rightStock of [false, true]) {
    const parts = [leftStock ? ['stock', dn.mainLoad + 0x100, split] : ['hex', Buffer.from(machineId.slice(0, split)).toString('hex')],
      rightStock ? ['stock', dn.mainLoad + 0x100 + split, 4 - split] : ['hex', Buffer.from(machineId.slice(split)).toString('hex')],
      ['hex', '00'.repeat(12)]]
    const voice = mod(parts)
    assert.deepEqual(voice.impliedNames(image), ['mk2-synth:5'])
    assert.deepEqual(check([core, machinesDn2(), voice], image), [])
    const relocated = mod(parts, [['.run', 0, 'abs32', 'abs', 5]])
    assert.deepEqual(relocated.impliedNames(image), [])
    assert(check([core, machinesDn2(), relocated], image).includes(
      'voice 0.1: its SHARC mk2-voice kernel 5 has no registration with that id in its mod'))
  }
})

test('DN2 busy companion accepts only its exact data site and requires a SHARC payload', () => {
  const rel = DIGITONE_MK2.releases[0], target = targetOf(DIGITONE_MK2, rel)
  const [address, [old, replacement]] = Object.entries(profile.dataSites!)[0]
  const doc = { elemod: 2, id: 'busy', version: '0.1', target, sections: {},
    sites: [{ addr: Number(address), len: bytes(old).length, kind: 'data', stock_sha256: sha(bytes(old)), new: replacement }] }
  const busy = new Mod2(doc, 'busy')
  const image = new Uint8Array(rel.mainLen)
  assert.ok(check([busy], image).includes('busy 0.1: SHARC data site requires a SHARC mod'))
  const core = new Mod2({ elemod: 2, id: 'core', version: '0.3', target,
    sections: { '.boot': { parts: [['hex', '4e75']] } } }, 'core')
  const app = new Mod2({ elemod: 2, id: 'app', version: '0.1', target, sections: {}, sharc: inertArtifact }, 'app')
  assert.deepEqual(check([core, busy, app], image), [])
  for (const change of [{ new: old }, { kind: 'code' }, { stock_sha256: '0'.repeat(64) },
    { relocs: [[0, 'abs32', 'abs', 0]] }, { len: 4, new: replacement.slice(0, 8) }]) {
    refusal(() => new Mod2({ ...doc, sites: [{ ...doc.sites[0], ...change }] }, 'busy'),
      'busy site 0: unqualified SHARC data site')
  }
})
