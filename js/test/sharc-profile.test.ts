// SPDX-License-Identifier: GPL-3.0-or-later
import assert from 'node:assert/strict'
import { test } from 'node:test'
import { DIGITONE_MK2, devices } from '../src/devices.ts'

const profile = DIGITONE_MK2.releases[0].sharc!

test('DN2 SHARC profile grants the H1 core and mod reservation', () => {
  assert.equal(profile.processor, 'ADSP-21569')
  assert.equal(profile.isa, 'sharc+-visa')
  assert.equal(profile.siliconRevision, null)
  assert.deepEqual(profile.ownedRanges, [[0x282dd600, 0x282e0800], [0x20090000, 0x200b0000]])
  assert.deepEqual(profile.patchRanges, [[0x2838489a, 0x283848a0], [0x20004a36, 0x20004a3c],
    [0x28392b0e, 0x28392b14], [0x8052dba4, 0x8052dba8], [0x28393cbe, 0x28393cc4]])
  assert.deepEqual(profile.dataSites, { 0x20011576: ['3e07ff00f0ff', '3e0617004501'] })
  assert.equal(profile.abi, 'dn2-master-mono32-v1')
  assert.equal(profile.l2Abi, 'dn2-master-mono32-l2-v1')
  assert.deepEqual(profile.pitchRange, [0x282dda00, 0x282dde00])
  assert.deepEqual(DIGITONE_MK2.dspPayloads, {})
  assert.deepEqual(devices().flatMap(d => d.releases.filter(r => r.sharc).map(r => [d.key, r.version])),
    [['digitone-mk2', '1.12']])
})

test('DN2 address units and capacity endpoints remain distinct', () => {
  for (const [core, loader] of [[0x268438, 0x28268438], [0x2dd600, 0x282dd600],
    [0x300000, 0x28300000], [0x38489a, 0x2838489a]]) {
    const matches = profile.l1Aliases.filter(([lo, hi]) => lo <= core && core < hi)
    assert.equal(matches.length, 1)
    const [lo, hi, base] = matches[0]
    assert.equal(base + core - lo, loader)
    assert.equal((base + hi - lo) % 4, 0)
  }
  assert.equal(2 * 0x1c244d, 0x38489a)
  assert.equal(2 * 0x1c12e2 + 0x28000000, 0x283825c4)
  const [lo, hi, base] = profile.l2Pc16
  assert.deepEqual([base, base + 2 * (hi - lo)], profile.l2Bytes)
  assert.equal(base + 2 * (0xb89c53 - lo), 0x200138a6)
  assert.ok(profile.protected.some(([a, b]) => a <= 0x200fe000 && 0x200fe000 < b))
  assert.equal(profile.protected.some(([a, b]) => a <= 0x282dd600 && 0x282dd600 < b), false)
  assert.equal(profile.ownedRanges.some(([a, b]) => a <= 0x282dd600 && 0x282dd600 < b), true)
})

test('DN2 transport, flash, stored and decoded DSP have independent budgets', () => {
  assert.deepEqual([profile.storedMax, profile.decodedMax], [0x100000, 0xfffff])
  assert.equal(profile.decodedMax + 1, 0x100000)
  assert.equal(DIGITONE_MK2.updateMax, 0x200000)
  assert.equal(DIGITONE_MK2.flashLimit - DIGITONE_MK2.flashAt, 0x300000)
  assert.equal(profile.storedMax - profile.storedLen, 446500)
  assert.equal(profile.decodedMax - profile.decodedLen, 211619)
})


test('DN2 mods require the 32 KiB ABI and preserve pointer units', async () => {
  const { modulePayload, pc16, byteSpan } = await import('../src/sharc.ts')
  const app = { abi: profile.l2Abi, segments: [{ offset_bytes: 0, kind: 'code', hex: '0100'.repeat(34) }],
    zero: [{ offset_bytes: 68, size_bytes: 32768 - 68 }],
    entries: { init_offset_bytes: 0, render_offset_bytes: 2 }, stack_bytes: 128 }
  assert.deepEqual(modulePayload(app, profile).owner, [0x20090000, 0x20098000])
  assert.throws(() => modulePayload(app, profile, true), /ABI does not match/)
  const overrun = structuredClone(app)
  overrun.zero[0].size_bytes += 4
  assert.throws(() => modulePayload(overrun, profile), /exceeds its owner/)
  assert.throws(() => modulePayload({ ...app, abi: profile.abi }, profile), /ABI does not match/)
  for (const [loader, expected] of [[0x282dda00, 0x16ed00], [0x20090000, 0xbc8000], [0x20097ffe, 0xbcbfff]]) {
    assert.equal(pc16(profile, loader), expected)
    assert.equal(byteSpan(profile, expected, 2, 'pc16')[0], loader)
  }
  for (const address of [0x20090001, 0x80000000])
    assert.throws(() => pc16(profile, address), /no aligned PC16 address/)
})
