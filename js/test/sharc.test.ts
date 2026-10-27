// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { test } from 'node:test'
import { sha } from '../src/bytes.ts'
import { DIGITONE_MK2, targetOf } from '../src/devices.ts'
import { ModError } from '../src/elemod.ts'
import { Mod2, check } from '../src/link.ts'
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
  refusal(() => sharc.insert(decoded, profile, [], [[0x20098000, 4, 0]]), 'SHARC new load or fill is outside the profile-owned ranges')
  const patched = sharc.insert(decoded, profile, [], [], [[0x2838489a, bytes('8715ffff78ff'), bytes('3e06160006eb')]])
  const changed = [...decoded.keys()].filter(i => decoded[i] !== patched[i])
  assert.equal(changed.length, 6)
  assert.equal(Buffer.from(patched.subarray(changed[0], changed[0] + 6)).toString('hex'), '3e06160006eb')
  const outputs = formats.write(parsed, parsed.stored.get(device.mainSection)!, device)
  assert.deepEqual(Buffer.from(outputs.syx), raw)
  formats.verify(outputs, parsed, formats.mainImage(parsed, device), device)
})

function machineArtifact(kind: 'voice' | 'filter', id = 6) {
  const prefix = JSON.parse(readFileSync(new URL(`../../elekloader/sdk/include/digitone-mk2/${kind}-bridge.json`, import.meta.url), 'utf8'))
  return { abi: profile[`${kind}Abi`], machine_id: id, stack_bytes: profile[`${kind}StackMax`]!,
    entries: { render_offset_bytes: 2048 } as { render_offset_bytes: number } | null,
    segments: [prefix, { offset_bytes: 2048, kind: 'code', hex: '0100'.repeat(3072) },
      { offset_bytes: 8192, kind: 'data', hex: '00000000' }],
    zero: [{ offset_bytes: 8196, size_bytes: 7164 }, { offset_bytes: 15360, size_bytes: 1024 }] }
}

for (const kind of ['voice', 'filter'] as const) test(`DN2 ${kind} artifacts pin only the adapter and bound the author kernel`, () => {
  const artifact = machineArtifact(kind)
  const payload = sharc.modulePayload(artifact, profile)
  assert.equal(payload.machineId, 6)
  assert.equal(payload.machineKind, kind)
  assert.deepEqual(payload.owner, profile[`${kind}Range`])
  const first = kind === 'voice' ? 5 : 6
  for (const [change, error] of [
    [(d: any) => { d.machine_id = first - 1 }, `SHARC ${kind} machine_id must be ${first}..31`],
    [(d: any) => { d.machine_id = 32 }, `SHARC ${kind} machine_id must be ${first}..31`],
    [(d: any) => { d.machine_id = true }, `SHARC ${kind} machine_id must be ${first}..31`],
    [(d: any) => { d.stack_bytes += 1 }, `SHARC ${kind} stack exceeds the profile limit`],
    [(d: any) => { d.entries = null }, `SHARC ${kind} payload needs a render entry`],
    [(d: any) => { d.entries.render_offset_bytes = 0 }, `SHARC ${kind} mod overlaps the adapter reservation`],
    [(d: any) => { d.entries.render_offset_bytes = 8128 }, 'SHARC entry is in the instruction read-ahead guard'],
    [(d: any) => { d.entries.render_offset_bytes = 8192 }, 'SHARC entry is not in owned code'],
    [(d: any) => { d.segments[0].hex = '0000' + d.segments[0].hex.slice(4) }, `SHARC ${kind} adapter does not match the qualified prefix`],
    [(d: any) => { d.segments[1].hex = d.segments[1].hex.slice(0, -4) + '0000' }, 'SHARC mod code needs its profiled NOP read-ahead guard'],
    [(d: any) => { d.zero[1].size_bytes += 4 }, 'SHARC zero range is unaligned or exceeds its owner'],
    [(d: any) => { d.segments[2].offset_bytes -= 4 }, 'SHARC payload has a gap or overlap in its owner'],
    [(d: any) => { d.zero = [{ offset_bytes: 8196, size_bytes: 8188 }] }, `SHARC ${kind} mod overlaps the adapter reservation`],
  ] as const) {
    const bad = structuredClone(artifact)
    change(bad)
    refusal(() => sharc.modulePayload(bad, profile), error)
  }
  artifact.segments[1].hex = '0000' + artifact.segments[1].hex.slice(4)
  sharc.modulePayload(artifact, profile)
  // A stateless filter may have no data segment; its complete owner is still initialized.
  artifact.segments.pop()
  artifact.zero[0] = { offset_bytes: 8192, size_bytes: 7168 }
  sharc.modulePayload(artifact, profile)
})

test('DN2 filter G: a valid one links and an invalid one is refused', () => {
  const rel = DIGITONE_MK2.releases[0], target = targetOf(DIGITONE_MK2, rel)
  const image = new Uint8Array(rel.mainLen)
  const core = new Mod2({ elemod: 2, id: 'core', version: '0.1', target,
    sections: { '.boot': { align: 4, parts: [['hex', '00000000']] } },
    collections: { mk2_voice_filters: { entry: 4 } } }, 'core')
  const ascii = (text: string) => [...text].map(c => c.charCodeAt(0).toString(16).padStart(2, '0')).join('')
  // The descriptor {8, name, label, curve, g} at 0, G {label, default} at 20 and its label at 28.
  const mod = (label: string | null, def: number, g = true) => new Mod2({ elemod: 2, id: 'g', version: '0.1', target,
    sections: { '.run': { align: 4, parts: [['hex', '00000008' + '00'.repeat(20) + def.toString(16).padStart(8, '0')
      + ascii(label ?? '') + '00']] } },
    symbols: { descriptor: ['.run', 0], g: ['.run', 20], label: ['.run', 28] },
    relocs: [...(g ? [['.run', 16, 'abs32', 'sym:g', 0]] : []), ...(label === null ? [] : [['.run', 20, 'abs32', 'sym:label', 0]])],
    contribute: [{ to: 'mk2_voice_filters', data: '00000000', relocs: [[0, 'abs32', 'sym:descriptor', 0]] }],
    sharc: machineArtifact('filter', 8) }, 'g')
  const refusal = 'g 0.1: DN2 filter G needs a label of 1..5 printable ASCII characters and a default of 0..127'
  for (const [label, def] of [['MODE', 0], ['HIGH5', 127]] as const) assert.deepEqual(check([core, mod(label, def)], image), [])
  assert.deepEqual(check([core, mod(null, 0, false)], image), [])
  for (const [label, def] of [['MODE', 128], ['TOOWIDE', 1], ['', 1], ['M\tX', 1], [null, 1]] as const)
    assert.deepEqual(check([core, mod(label, def)], image), [refusal], `${label} ${def}`)
})

test('DN2 combines one synth and one filter with matching independent identities', () => {
  const rel = DIGITONE_MK2.releases[0], target = targetOf(DIGITONE_MK2, rel)
  const image = new Uint8Array(rel.mainLen)
  const core = new Mod2({ elemod: 2, id: 'core', version: '0.1', target,
    sections: { '.boot': { align: 4, parts: [['hex', '00000000']] } },
    collections: { mk2_voice_synths: { entry: 4 }, mk2_voice_filters: { entry: 4 } } }, 'core')
  const doc = (kind: 'voice' | 'filter', id = 6, name: string = kind): Record<string, any> => ({
    elemod: 2, id: name, version: '0.1', target,
    sections: { '.run': { align: 4, parts: [['hex', id.toString(16).padStart(8, '0') + '00'.repeat(16)]] } },
    symbols: { descriptor: ['.run', 0] },
    contribute: [{ to: profile[`${kind}Table`], data: '00000000', relocs: [[0, 'abs32', 'sym:descriptor', 0]] }],
    sharc: machineArtifact(kind, id),
  })
  const voice = () => new Mod2(doc('voice'), 'voice'), filter = () => new Mod2(doc('filter'), 'filter')
  assert.deepEqual(check([core, voice(), filter()], image), [])
  assert.deepEqual(check([core, filter(), voice()], image), [])
  assert.deepEqual(check([core, voice()], image), [])
  assert.deepEqual(check([core, filter()], image), [])
  const collision = 'SHARC mods may combine only one DN2 voice and one DN2 filter'
  for (const kind of ['voice', 'filter'] as const) {
    const a = new Mod2(doc(kind), kind), b = new Mod2(doc(kind, 7, 'second'), 'second')
    assert(check([core, a, b], image).includes(collision))
    const absent = doc(kind); delete absent.sharc
    assert(check([core, new Mod2(absent, kind)], image).includes(
      `${kind} 0.1: DN2 ${kind} registration requires a matching SHARC ${kind} payload`))
    const mismatch = doc(kind); mismatch.sharc.machine_id = 7
    assert(check([core, new Mod2(mismatch, kind)], image).includes(
      `${kind} 0.1: SHARC ${kind} machine_id must match its single native ${kind} registration`))
    // The old registration tables are gone: the generic check refuses an entry there.
    const legacy = doc(kind), table = kind === 'voice' ? 'mk2_synths' : 'mk2_filters'
    legacy.contribute.push({ ...legacy.contribute[0], to: table })
    assert(check([core, new Mod2(legacy, kind)], image).includes(
      `${kind} 0.1 adds to table ${table}, which no given mod declares`))
    const master = new Mod2({ elemod: 2, id: 'master', version: '0.1', target,
      sections: {}, sharc: inertArtifact }, 'master')
    assert(check([core, a, master], image).includes(collision))
  }
  assert(check([core, voice(), filter(), new Mod2(doc('voice', 7, 'second'), 'second')], image).includes(collision))
  const wrongRole = doc('filter'); wrongRole.sharc = machineArtifact('voice')
  assert(check([core, new Mod2(wrongRole, 'filter')], image).includes(
    'filter 0.1: DN2 filter registration requires a matching SHARC filter payload'))
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
    sharc: machineArtifact('voice', 5),
  }, 'voice')
  for (const split of [1, 2, 3]) for (const leftStock of [false, true]) for (const rightStock of [false, true]) {
    const parts = [leftStock ? ['stock', dn.mainLoad + 0x100, split] : ['hex', Buffer.from(machineId.slice(0, split)).toString('hex')],
      rightStock ? ['stock', dn.mainLoad + 0x100 + split, 4 - split] : ['hex', Buffer.from(machineId.slice(split)).toString('hex')],
      ['hex', '00'.repeat(12)]]
    const voice = mod(parts)
    assert.deepEqual(voice.impliedNames(image), ['mk2-synth:5'])
    assert.deepEqual(check([core, voice], image), [])
    const relocated = mod(parts, [['.run', 0, 'abs32', 'abs', 5]])
    assert.deepEqual(relocated.impliedNames(image), [])
    assert(check([core, relocated], image).includes(
      'voice 0.1: SHARC voice machine_id must match its single native voice registration'))
  }
})
