// SPDX-License-Identifier: GPL-3.0-or-later
// Mod storage's lockfile and allocator against what elekloader's store.py gave (test/store-vectors.json, from
// tools/store_vectors.py): the same lockfile, notes, symbols and IDs for a run of builds, and the same refusals.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { allocate, ccEntries, checkFlags, checkLock, fresh, knobs, codeEntries, dumps, fromImage, handleEntries, initEntries, layoutId, parseCcs, parseCvs, parseDecls, parseHandles, parseInits, parseLfos, parseLocks, parseRough, parseRoutes, symbols, trailer, type Decl, type Lock } from '../src/store.ts'

const V = JSON.parse(readFileSync(new URL('./store-vectors.json', import.meta.url), 'utf8'))
const DEV = 'analog-rytm-mk2'
const hex = (b: Uint8Array) => [...b].map(x => x.toString(16).padStart(2, '0')).join('')

test('a run of builds: the same lockfiles, notes, symbols and IDs', () => {
  for (const [name, run] of Object.entries<any>(V.runs)) {
    let lock: Lock | null = run.start ?? null
    const core: string | null = run.core ?? null
    const dev: string = run.device ?? DEV
    run.steps.forEach((decls: Decl[], i: number) => {
      const want = run.results[i]
      if (want.error) {
        assert.throws(() => allocate(lock, decls, dev, core), (e: Error) => e.message === want.error, `${name} step ${i}`)
        return
      }
      const [l, notes] = allocate(lock, decls, dev, core)
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

test('lockable fields: the same lock codes, notes and table entries', () => {
  for (const [name, run] of Object.entries<any>(V.lock_runs)) {
    let lock: Lock | null = null
    run.steps.forEach(([decls, locks]: [Decl[], [string, string][]], i: number) => {
      const want = run.results[i]
      if (want.error) {
        assert.throws(() => allocate(lock, decls, DEV, null, locks), (e: Error) => e.message === want.error, `${name} step ${i}`)
        return
      }
      const [l, notes] = allocate(lock, decls, DEV, null, locks)
      lock = l
      assert.deepEqual(l, want.lock, `${name} step ${i}: lockfile`)
      assert.deepEqual(notes, want.notes, `${name} step ${i}: notes`)
      assert.deepEqual(codeEntries(l).map(hex), want.codes, `${name} step ${i}: codes`)
      assert.equal(layoutId(l), want.layout_id, `${name} step ${i}: layout ID`)
      assert.equal(dumps(l), want.json, `${name} step ${i}: JSON`)
    })
  }
})

test('CCs: the same CCs, notes and table entries', () => {
  for (const [name, run] of Object.entries<any>(V.cc_runs)) {
    let lock: Lock | null = null
    run.steps.forEach(([decls, locks, ccs]: [Decl[], [string, string][], [string, string][]], i: number) => {
      const want = run.results[i]
      if (want.error) {
        assert.throws(() => allocate(lock, decls, DEV, null, locks, ccs), (e: Error) => e.message === want.error, `${name} step ${i}`)
        return
      }
      const [l, notes] = allocate(lock, decls, DEV, null, locks, ccs)
      lock = l
      assert.deepEqual(l, want.lock, `${name} step ${i}: lockfile`)
      assert.deepEqual(notes, want.notes, `${name} step ${i}: notes`)
      assert.deepEqual(codeEntries(l).map(hex), want.codes, `${name} step ${i}: codes`)
      assert.deepEqual(codeEntries(l, ccs).map(hex), want.lfo_codes, `${name} step ${i}: codes with LFO`)
      assert.deepEqual(codeEntries(l, ccs.slice(0, 1), ccs.slice(1)).map(hex), want.rough_codes, `${name} step ${i}: codes with LFO and SMOOTH_OFF`)
      assert.deepEqual(codeEntries(l, [], ccs.slice(1), ccs.slice(0, 2)).map(hex), want.route_codes, `${name} step ${i}: codes with SMOOTH_OFF and ROUTE_FLAG`)
      assert.deepEqual(ccEntries(l).map(hex), want.ccs, `${name} step ${i}: CCs`)
      assert.equal(layoutId(l), want.layout_id, `${name} step ${i}: layout ID`)
      assert.equal(dumps(l), want.json, `${name} step ${i}: JSON`)
    })
  }
})

test('codes per track kind (core-a4 0.5): the same lock codes, notes and table entries', () => {
  const A4 = 'analog-four-mk2'
  for (const [name, run] of Object.entries<any>(V.kind_runs)) {
    let lock: Lock | null = null
    run.steps.forEach(([decls, locks, cvs, ccs = []]: [Decl[], [string, string][], [string, string][], [string, string][]?], i: number) => {
      const want = run.results[i]
      if (want.error) {
        assert.throws(() => allocate(lock, decls, A4, null, locks, ccs, [], cvs), (e: Error) => e.message === want.error, `${name} step ${i}`)
        return
      }
      const [l, notes] = allocate(lock, decls, A4, null, locks, ccs, [], cvs)
      lock = l
      assert.deepEqual(l, want.lock, `${name} step ${i}: lockfile`)
      assert.deepEqual(notes, want.notes, `${name} step ${i}: notes`)
      assert.deepEqual(codeEntries(l).map(hex), want.codes, `${name} step ${i}: codes`)
      assert.deepEqual(codeEntries(l, [], [], locks).map(hex), want.route_codes, `${name} step ${i}: codes with ROUTE_FLAG`)
      assert.deepEqual(ccEntries(l).map(hex), want.ccs, `${name} step ${i}: CCs`)
      assert.equal(layoutId(l), want.layout_id, `${name} step ${i}: layout ID`)
      assert.equal(dumps(l), want.json, `${name} step ${i}: JSON`)
    })
  }
  for (const [name, c] of Object.entries<any>(V.kind_locks)) {
    if (c.error === null) assert.deepEqual(allocate(c.lock, [], A4)[0], c.result, name)
    else assert.throws(() => allocate(c.lock, [], A4), (e: Error) => e.message === c.error, `${name}: ${c.error}`)
  }
})

test("the device checks of the lockable fields' flags, with the same messages", () => {
  for (const [name, c] of Object.entries<any>(V.flags)) {
    const run = () => checkFlags(c.device, new Map(Object.entries(c.scopes)), c.lfos, c.routes, c.cvs, c.ccs)
    if (c.error === null) run()
    else assert.throws(run, (e: Error) => e.message === c.error, `${name}: ${c.error}`)
  }
})

test('inits: the same lockfiles, descriptors, table entries and notes', () => {
  for (const [name, run] of Object.entries<any>(V.init_runs)) {
    let lock: Lock | null = null
    run.steps.forEach(([decls, locks, inits]: [Decl[], [string, string][], [string, string, number][]], i: number) => {
      const want = run.results[i]
      if (want.error) {
        assert.throws(() => { const [l] = allocate(lock, decls, DEV, null, locks, [], inits); symbols(l) },
          (e: Error) => e.message === want.error, `${name} step ${i}`)
        return
      }
      const [l, notes] = allocate(lock, decls, DEV, null, locks, [], inits)
      lock = l
      assert.deepEqual(l, want.lock, `${name} step ${i}: lockfile`)
      assert.deepEqual(notes, want.notes, `${name} step ${i}: notes`)
      assert.deepEqual(Object.fromEntries(symbols(l)[0]), want.symbols, `${name} step ${i}: symbols`)
      assert.deepEqual(codeEntries(l).map(hex), want.codes, `${name} step ${i}: codes`)
      assert.deepEqual(initEntries(l).map(hex), want.inits, `${name} step ${i}: inits`)
      assert.equal(layoutId(l), want.layout_id, `${name} step ${i}: layout ID`)
      assert.equal(dumps(l), want.json, `${name} step ${i}: JSON`)
    })
  }
})

test('handles (core-ar2 0.11): core_handles as store.py lists it, with the lockfile and layout of the same fields', () => {
  for (const [name, c] of Object.entries<any>(V.handle_runs)) {
    const [l] = allocate(null, c.decls, DEV)
    assert.deepEqual(l, c.lock, `${name}: lockfile`)
    assert.deepEqual(handleEntries(l, c.handles).map(hex), c.handles_table, `${name}: core_handles`)
    assert.equal(layoutId(l), c.layout_id, `${name}: layout ID`)
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
    else {
      assert.deepEqual(parseDecls(c.doc), c.ok, name)
      assert.deepEqual(parseLocks(c.doc), c.locks, name)
      assert.deepEqual(parseCcs(c.doc), c.ccs, name)
      assert.deepEqual(parseLfos(c.doc), c.lfos, name)
      assert.deepEqual(parseRough(c.doc), c.rough, name)
      assert.deepEqual(parseRoutes(c.doc), c.routes, name)
      assert.deepEqual(parseCvs(c.doc), c.cvs, name)
      assert.deepEqual(Object.fromEntries(parseInits(c.doc)), c.inits, name)
      assert.deepEqual(parseHandles(c.doc), c.handles, name)
    }
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

test('knob ids: the same runs, notes and refusals as store.knobs', () => {
  for (const [name, run] of Object.entries<any>(V.knob_runs)) {
    const lock: Record<string, any> = run.start === 'fresh' ? fresh(DEV) : {}
    run.steps.forEach(([want, first, fixed, top]: [Record<string, number>, number, [number, number][], number], i: number) => {
      const res = run.results[i]
      const w = new Map(Object.entries(want))
      if (res.error) {
        assert.throws(() => knobs(lock, w, first, fixed, top), (e: Error) => e.message === res.error, `${name} step ${i}`)
        return
      }
      const [base, notes] = knobs(lock, w, first, fixed, top)
      assert.deepEqual(Object.fromEntries(base), res.base, `${name} step ${i}: ids`)
      assert.deepEqual(notes, res.notes, `${name} step ${i}: notes`)
      assert.deepEqual(lock, res.lock, `${name} step ${i}: lockfile`)
      if (res.layout_id !== null) assert.equal(layoutId(lock as Lock), res.layout_id, `${name} step ${i}: layout ID`)
    })
  }
  for (const [name, c] of Object.entries<any>(V.knob_locks)) {
    if (c.error) assert.throws(() => checkLock(c.lock, DEV), (e: Error) => e.message === c.error, name)
    else checkLock(c.lock, DEV)
  }
})
