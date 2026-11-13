// SPDX-License-Identifier: GPL-3.0-or-later
import { test } from 'node:test'
import assert from 'node:assert/strict'
import { packSection } from '../src/codec/aplib.ts'
import { contentChecksum, encodeSyx } from '../src/codec/transport.ts'
import { fromHex } from '../src/bytes.ts'
import { devices, DIGITAKT_MK1 } from '../src/devices.ts'
import { Syx, SyxError, verify } from '../src/syx.ts'

test('the update limit counts the preamble and is optional', () => {
  const body = new TextEncoder().encode('main os image '.repeat(300))
  const packed = packSection(body)
  const cont = new Uint8Array(0x80 + Math.ceil(packed.length / 16) * 16)
  cont.set(new TextEncoder().encode('ELE3'))
  cont.set(new TextEncoder().encode('1.00'), 0x14)
  const v = new DataView(cont.buffer)
  for (const [at, word] of [[0x1c, 1], [0x20, 3], [0x24, 0x80], [0x28, packed.length], [0x2c, DIGITAKT_MK1.mainLoad]])
    v.setUint32(at, word)
  cont.set(packed, 0x80)
  const stream = new Uint8Array(8 + cont.length)
  const header = new DataView(stream.buffer)
  header.setUint32(0, cont.length)
  header.setUint32(4, contentChecksum(cont))
  stream.set(cont, 8)
  const raw = encodeSyx(stream, 0x0a, fromHex('f000203c0a007f0105000172000000f7')!,
    fromHex('f000203c0a007f0205000172000000f7')!)
  const stock = new Syx(raw)
  for (const updateMax of [null, stream.length, stream.length + 1])
    verify(raw, stock, body, { ...DIGITAKT_MK1, updateMax })
  for (const updateMax of [cont.length, stream.length - 1])
    assert.throws(() => verify(raw, stock, body, { ...DIGITAKT_MK1, updateMax }), new SyxError(
      `decoded update stream ${stream.length} bytes exceeds the measured USB update limit of ${updateMax} bytes`))
  assert.deepEqual(devices().filter(d => d.updateMax !== null).map(d => [d.key, d.updateMax]), [
    ['digitone-mk2', 0x200000],
  ])
})
