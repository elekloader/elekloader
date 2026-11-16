// SPDX-License-Identifier: GPL-3.0-or-later
// +Drive storage against what elekloader's Python gave (test/drivestore-vectors.json, from tools/drivestore_vectors.py):
// the same placements, /mods/.store bytes, notes and refusals, run by run.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { fromHex, toHex } from '../src/bytes.ts'
import * as ds from '../src/drivestore.ts'
import { ModError } from '../src/elemod.ts'

const V = JSON.parse(readFileSync(new URL('./drivestore-vectors.json', import.meta.url), 'utf8'))

const base = (j: any) => new ds.Base(j.device, j.scopes, j.flashed_id, j.ancestors, j.codes, j.ccs, j.inits, j.room, 0,
  j.live_dst ?? false)

test('the allocator: drive_store.py\'s placements, bytes and notes, step by step', () => {
  for (const run of V.runs) {
    let st: ds.State | null = null
    run.steps.forEach(([bj, [op, arg]]: [any, [string, any]], i: number) => {
      const want = run.results[i], what = `${run.name}, step ${i + 1}`
      const b = base(bj)
      if (op === 'fit') {
        assert.deepEqual({ fit: ds.fit(b, new ds.Want(arg.map((k: string) => V.mods[k]))) }, want, what)
      } else if (op === 'forget') {
        const [s, changed] = ds.forget(st!, arg)
        st = s
        assert.deepEqual({ store: toHex(ds.encode(st)), changed }, want, what)
      } else {
        let p: ds.Placed
        try {
          p = ds.allocate(b, st, new ds.Want(arg.map((k: string) => V.mods[k])))
        } catch (e) {
          assert.ok(e instanceof ModError, what)
          assert.deepEqual({ error: (e as Error).message }, want, what)
          return
        }
        assert.deepEqual(p.summary(), want, what)
        st = ds.decode(p.bytes)
        assert.ok(st !== null, what)
        assert.equal(toHex(ds.encode(st!)), want.store, what)
      }
    })
  }
})

test('a bad .store reads as none', () => {
  const good = fromHex(V.runs[1].results[0].store)!
  assert.ok(ds.decode(good) !== null)
  const flip = new Uint8Array(good); flip[0x20] ^= 1
  for (const bad of [null, new Uint8Array(0), good.subarray(0, good.length - 1), new Uint8Array([...good, 0]), flip])
    assert.equal(ds.decode(bad), null)
})
