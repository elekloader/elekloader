// SPDX-License-Identifier: GPL-3.0-or-later
// A build that keeps the +Drive's storage free, against what elekloader's Python gave (test/drivereserve-vectors.json,
// from tools/drivereserve_vectors.py): store.allocate's lockfiles, notes and refusals with the lockfile's "drive",
// and drivestore.reserve from a .store.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fromHex } from '../src/bytes.ts'
import * as ds from '../src/drivestore.ts'
import * as store from '../src/store.ts'
import { ModError } from '../src/elemod.ts'

const V = JSON.parse(readFileSync(new URL('./drivereserve-vectors.json', import.meta.url), 'utf8'))

test('store.allocate with what the +Drive keeps: store.py\'s lockfiles, notes and refusals', () => {
  V.allocate.forEach((c: any, i: number) => {
    const what = `allocation ${i + 1}`
    let got: [store.Lock, string[]]
    try {
      got = store.allocate(c.lock, c.decls, c.device, null, c.locks, c.ccs, [], [], c.drive)
    } catch (e) {
      assert.ok(e instanceof ModError, what)
      assert.equal((e as Error).message, c.error, what)
      return
    }
    assert.equal(c.error, undefined, what)
    assert.equal(store.dumps(got[0]), c.out, what)
    assert.deepEqual(got[1], c.notes, what)
  })
})

test('drivestore.reserve: drive_store.py\'s, from the same .store', () => {
  for (const r of V.reserve) {
    const st = ds.decode(fromHex(r.store)!)
    assert.ok(st !== null)
    assert.deepEqual(ds.reserve(st!), r.reserve)
  }
})
