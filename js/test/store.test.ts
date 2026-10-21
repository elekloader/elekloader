// SPDX-License-Identifier: GPL-3.0-or-later
// Mod storage's lockfile and allocator against what elekloader's store.py gave (test/store-vectors.json, from
// tools/store_vectors.py): the same lockfile, notes, symbols and IDs for a run of builds, and the same refusals.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { allocate, dumps, fromImage, layoutId, parseDecls, symbols, trailer, type Decl, type Lock } from '../src/store.ts'

const V = JSON.parse(readFileSync(new URL('./store-vectors.json', import.meta.url), 'utf8'))
const DEV = 'analog-rytm-mk2'
const hex = (b: Uint8Array) => [...b].map(x => x.toString(16).padStart(2, '0')).join('')

test('a run of builds: the same lockfiles, notes, symbols and IDs', () => {
  for (const [name, run] of Object.entries<any>(V.runs)) {
    let lock: Lock | null = run.start ?? null
    const core: string | null = run.core ?? null
    run.steps.forEach((decls: Decl[], i: number) => {
      const want = run.results[i]
      if (want.error) {
        assert.throws(() => allocate(lock, decls, DEV, core), (e: Error) => e.message === want.error, `${name} step ${i}`)
        return
      }
      const [l, notes] = allocate(lock, decls, DEV, core)
      lock = l
      assert.deepEqual(l, want.lock, `${name} step ${i}: lockfile`)
      assert.deepEqual(notes, want.notes, `${name} step ${i}: notes`)
      const [syms, tab] = symbols(l)
      assert.deepEqual(Object.fromEntries(syms), want.symbols, `${name} step ${i}: symbols`)
      assert.deepEqual(tab.map(hex), want.ids, `${name} step ${i}: IDs`)
      assert.equal(layoutId(l), want.layout_id, `${name} step ${i}: layout ID`)
      assert.equal(dumps(l), want.json, `${name} step ${i}: JSON`)
    })
  }
})

test('the lockfiles it refuses, with the same messages, and the ones it keeps as they are', () => {
  for (const [name, c] of Object.entries<any>(V.locks)) {
    if (c.error === null) assert.deepEqual(allocate(c.lock, [], DEV)[0], c.result, name)
    else assert.throws(() => allocate(c.lock, [], DEV), (e: Error) => e.message === c.error, `${name}: ${c.error}`)
  }
})

test('a mod\'s "storage": what it reads, and what it refuses', () => {
  for (const [name, c] of Object.entries<any>(V.decls)) {
    if (c.error) assert.throws(() => parseDecls(c.doc), (e: Error) => e.message === c.error, `${name}: ${c.error}`)
    else assert.deepEqual(parseDecls(c.doc), c.ok, name)
  }
})

test('the build carries the lockfile, as store.py writes it', () => {
  const [lock] = allocate(null, [['a', 'x', 'track', 4]], DEV)
  const t = trailer(lock)
  assert.equal(new TextDecoder().decode(t.subarray(12)), dumps(lock))
  assert.deepEqual([...t.subarray(0, 12)], [...new TextEncoder().encode('ELKSTORE'), 0, 0, (t.length - 12) >> 8, (t.length - 12) & 255])
  const image = new Uint8Array(1000 + t.length)
  image.set(t, 1000)
  assert.deepEqual(fromImage(image), lock)
  assert.equal(fromImage(new Uint8Array(1000)), null)
})
