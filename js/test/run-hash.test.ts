// SPDX-License-Identifier: GPL-3.0-or-later
// __run_hash, the hash a core takes of its copy of the RAM image: link.py's run_hash, value for value.
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { runHash } from '../src/link.ts'
import { lcg } from './helpers.ts'

test('__run_hash: a rolling x33 over the RAM image words from 5381, as link.run_hash gives it', () => {
  // the values come from the Python (elekloader/link.py, run_hash), tests/test_octatrack_boot.py's own copy agrees
  const words = (...w: number[]) => Uint8Array.from(w.flatMap(x => [x >>> 24, (x >>> 16) & 255, (x >>> 8) & 255, x & 255]))
  assert.equal(runHash(new Uint8Array(0)), 0x1505)
  assert.equal(runHash(words(0)), 0x2b5a5)                      // a zero word still counts
  assert.equal(runHash(words(1)), 0x2b5a6)
  assert.equal(runHash(words(0xffffffff)), 0x2b5a4)             // 5381 * 33 + 0xffffffff, mod 2^32
  assert.equal(runHash(words(1, 2)), 0x596a68)
  assert.equal(runHash(words(2, 1)), 0x596a88)                  // the order counts
  assert.equal(runHash(lcg(7, 4096)), 0x38cf51b6)
  assert.equal(runHash(lcg(99, 1816)), 0x340c6088)
  const odd = lcg(3, 4099)                                      // a partial word at the end is not hashed
  assert.equal(runHash(odd), runHash(odd.slice(0, 4096)))
})
