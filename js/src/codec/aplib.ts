// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from elekloader's codec/aplib.py (GPL-2.0-or-later; vendored there from digikit, dt2/aplib.py, by Em D,
// with the match-finder and the compressed output by irpina). It writes the same bytes as the Python for any
// input: the same cost-aware parse with one byte of look-ahead, ties broken the same way, the same terminator.
// See aplib.py for the format and how each rule was confirmed against the device's own depacker.

const BIAS = 767            // offset = ((g << 8) + byte) - 767
const FAR = 3328            // past this, the decoder adds 1 to the length
const REUSE = 2             // a gamma of 2 reuses the last offset
const MIN_MATCH = 3         // the hash key: candidates share their first three bytes
const MAX_OFFSET = (1 << 20) - 1   // Elektron's own streams reach back up to just under 1 MiB, on every device
const MAX_COUNT = 1 << 11   // Elektron's own streams never match more than 2048 bytes (issue #59)
const CANDIDATES = 8        // positions tried per hash chain
const LITERAL_BITS = 9
const TERMINATOR_GAMMA = (1 << 24) + 2     // with a trailing 0xFF byte: (2**24+2)*256 + 255 == 0x2FF mod 2**32
const TERMINATOR_TRAILER = 0xff

/** The (tag bit, payload) event stream, written as the depacker reads it: a tag byte per 8 events, MSB first,
 * that group's payload bytes right after it (aplib.py _assemble). */
class Events {
  out: Uint8Array
  n = 0
  private tagAt = 0
  private count = 0
  constructor(size: number) { this.out = new Uint8Array(Math.max(64, size)) }
  private push(b: number) {
    if (this.n === this.out.length) {
      const more = new Uint8Array(this.out.length * 2)
      more.set(this.out)
      this.out = more
    }
    this.out[this.n++] = b
  }
  event(bit: number, payload = -1) {
    const slot = this.count & 7
    if (slot === 0) { this.tagAt = this.n; this.push(0) }
    if (bit & 1) this.out[this.tagAt] |= 0x80 >> slot
    this.count++
    if (payload >= 0) this.push(payload)
  }
  /** The depacker's gamma2 code for `value` (>= 2): each bit after the leading 1, then a stop bit, 1 on the
   * last. `trailer` rides on that last stop bit (a match's offset byte, the terminator's 0xFF). */
  gamma(value: number, trailer = -1) {
    if (value < 2) throw new Error('gamma2 cannot encode values below 2, got ' + value)
    for (let b = 30 - Math.clz32(value); b >= 0; b--) {
      this.event((value >>> b) & 1)
      this.event(b === 0 ? 1 : 0, b === 0 ? trailer : -1)
    }
  }
  bytes(): Uint8Array { return this.out.slice(0, this.n) }
}

function match(ev: Events, offset: number, count: number) {
  if (!(offset >= 1 && offset <= MAX_OFFSET)) throw new Error(`offset ${offset} out of range`)
  let length = count - 1
  if (offset > FAR) length -= 1           // the decoder will add it back
  if (length < 1) throw new Error(`count ${count} too small for offset ${offset}`)
  const raw = offset + BIAS
  const g = raw >>> 8
  if (g === REUSE) throw new Error(`offset ${offset} would encode as the reuse code`)
  ev.event(0)                             // a match
  ev.gamma(g, raw & 0xff)                 // the offset's gamma, its raw byte riding the last bit
  if (length >= 1 && length <= 3) {
    ev.event((length >> 1) & 1)
    ev.event(length & 1)
  } else {
    ev.event(0)                           // 00: a gamma follows
    ev.event(0)
    ev.gamma(length - 2)
  }
}

/** aplib.py _reuse_events: a match that reuses the last offset, the reuse code in place of the offset. */
function reuse(ev: Events, offset: number, count: number) {
  const length = count - 1 - (offset > FAR ? 1 : 0)
  if (length < 1) throw new Error(`count ${count} too small for offset ${offset}`)
  ev.event(0)
  ev.gamma(REUSE)
  if (length <= 3) {
    ev.event((length >> 1) & 1)
    ev.event(length & 1)
  } else {
    ev.event(0)
    ev.event(0)
    ev.gamma(length - 2)
  }
}

const gammaCost = (v: number) => 2 * (31 - Math.clz32(v))

/** aplib.py _match_cost: the bits a match costs, or -1 when the decoder cannot express it. */
function matchCost(offset: number, count: number, reuse: boolean): number {
  const length = count - 1 - (offset > FAR ? 1 : 0)
  if (length < 1) return -1
  const lc = length <= 3 ? 2 : 2 + gammaCost(length - 2)
  return 1 + (reuse ? gammaCost(REUSE) : gammaCost((offset + BIAS) >>> 8) + 8) + lc
}

type Step = [number, number, number]   // [kind 0 literal | 1 match | 2 reuse, byte or offset, count]

/** aplib.py _parse: the bits-saved choice at each position (the last offset, then the newest CANDIDATES
 * positions sharing the next three bytes), one byte of look-ahead, reuse only once a match set the offset. */
function parse(data: Uint8Array): Step[] {
  const n = data.length
  const head = new Map<number, number>()
  const prev = new Int32Array(n).fill(-1)
  let hashed = 0
  const hashTo = (p: number) => {
    while (hashed <= p) {
      const q = hashed
      if (q + MIN_MATCH <= n) {
        const k = (data[q] << 16) | (data[q + 1] << 8) | data[q + 2]
        const h = head.get(k)
        prev[q] = h === undefined ? -1 : h
        head.set(k, q)
      }
      hashed = q + 1
    }
  }
  const run = (i: number, j: number, limit: number) => {
    let k = 0
    while (k < limit && data[i + k] === data[j + k]) k++
    return k
  }
  const best = (i: number, last: number): [number, number, number, boolean] => {
    hashTo(i)
    const limit = Math.min(MAX_COUNT, n - i)
    let out: [number, number, number, boolean] = [0, 0, 0, false]
    if (last && last <= i) {
      const k = run(i, i - last, limit)
      const c = k >= 2 ? matchCost(last, k, true) : -1
      if (c >= 0 && LITERAL_BITS * k - c > out[0]) out = [LITERAL_BITS * k - c, last, k, true]
    }
    let j = i + MIN_MATCH <= n ? prev[i] : -1
    let tried = 0
    while (j >= 0 && tried < CANDIDATES) {
      const off = i - j
      if (off > MAX_OFFSET) break
      const k = run(i, j, limit)
      const c = matchCost(off, k, off === last)
      if (c >= 0 && LITERAL_BITS * k - c > out[0]) out = [LITERAL_BITS * k - c, off, k, off === last]
      j = prev[j]
      tried++
    }
    return out
  }
  const out: Step[] = []
  let i = 0, last = 0
  while (i < n) {
    let [gain, off, count, isReuse] = best(i, last)
    if (gain > 0 && i + 1 < n && best(i + 1, last)[0] > gain + LITERAL_BITS) gain = 0
    if (gain > 0) {
      out.push([isReuse ? 2 : 1, off, count])
      last = off
      i += count
    } else {
      out.push([0, data[i], 1])
      i += 1
    }
  }
  return out
}

function terminate(ev: Events) {
  ev.event(0)
  ev.gamma(TERMINATOR_GAMMA, TERMINATOR_TRAILER)
}

/** A valid stream for the device's depacker, without the section header. `compress` false stores every byte as
 * a literal (always a little larger than the input), as aplib.py's pack(compress=False). */
export function pack(data: Uint8Array, compress = true): Uint8Array {
  const ev = new Events(Math.ceil(data.length * 1.15) + 64)
  if (compress) {
    for (const [kind, v, count] of parse(data)) {
      if (kind === 0) ev.event(1, v)
      else if (kind === 1) match(ev, v, count)
      else reuse(ev, v, count)
    }
  } else {
    for (let i = 0; i < data.length; i++) ev.event(1, data[i])
  }
  terminate(ev)
  return ev.bytes()
}

/** pack(data) with the section's 8-byte header: big-endian [u32 stream length][u32 byte sum of the stream]. */
export function packSection(data: Uint8Array, compress = true): Uint8Array {
  const body = pack(data, compress)
  const out = new Uint8Array(8 + body.length)
  let sum = 0
  for (let i = 0; i < body.length; i++) sum += body[i]
  const v = new DataView(out.buffer)
  v.setUint32(0, body.length)
  v.setUint32(4, sum >>> 0)
  out.set(body, 8)
  return out
}
