# SPDX-License-Identifier: GPL-2.0-or-later
"""DN2 section-7 evidence; optional owner stock never leaves memory.

Run with Python, or set ELEKLOADER_DN2_SYX for the stock assertions too.
"""
import functools
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader import devices, formats  # noqa: E402
from elekloader.devices.digitone_mk2 import DEVICE  # noqa: E402

RELEASE = DEVICE.releases['1.12']
PROFILE = RELEASE.sharc
STOCK = os.environ.get('ELEKLOADER_DN2_SYX')


@functools.lru_cache(maxsize=1)
def stock():
    raw = Path(STOCK).read_bytes()
    parsed, device, release = formats.load(raw)
    assert device is DEVICE and release is RELEASE
    return raw, parsed, formats.main_image(parsed, device)


class ProfileTests(unittest.TestCase):
    def test_profile_grants_the_h1_core_and_app_reservation(self):
        self.assertEqual(PROFILE['processor'], 'ADSP-21569')
        self.assertEqual(PROFILE['isa'], 'sharc+-visa')
        self.assertIsNone(PROFILE['silicon_revision'])
        self.assertEqual(PROFILE['owned_ranges'], ((0x282dd600, 0x282e0800), (0x20090000, 0x200b0000)))
        self.assertEqual(PROFILE['patch_ranges'], ((0x2838489a, 0x283848a0), (0x20004a36, 0x20004a3c),
                                                  (0x28392b0e, 0x28392b14), (0x8052dba4, 0x8052dba8),
                                                  (0x28393cbe, 0x28393cc4)))
        self.assertEqual(PROFILE['data_sites'], {0x20011576: ('3e07ff00f0ff', '3e0617004501')})
        self.assertEqual(PROFILE['abi'], 'dn2-master-mono32-v1')
        self.assertEqual(PROFILE['l2_abi'], 'dn2-master-mono32-l2-v1')
        self.assertEqual(PROFILE['pitch_range'], (0x282dda00, 0x282dde00))
        self.assertEqual(DEVICE.dsp_payloads, {})  # OT/DSP56300 support remains separate.
        self.assertEqual([(d.key, r.version) for d in devices.devices()
                          for r in d.releases.values() if r.sharc], [('digitone-mk2', '1.12')])

    def test_alias_units_and_capacity_endpoints(self):
        # Datasheet L1 byte aliases and independent stock data/PC landmarks.
        for core, loader in ((0x268438, 0x28268438), (0x2dd600, 0x282dd600),
                             (0x300000, 0x28300000), (0x38489a, 0x2838489a)):
            matches = [(lo, hi, base) for lo, hi, base in PROFILE['l1_aliases'] if lo <= core < hi]
            self.assertEqual(len(matches), 1)
            lo, hi, base = matches[0]
            self.assertEqual(base + core - lo, loader)
            self.assertEqual((base + hi - lo) & 3, 0)
        self.assertEqual(2 * 0x1c244d, 0x38489a)  # PC16 is not a byte pointer.
        self.assertEqual(2 * 0x1c12e2 + 0x28000000, 0x283825c4)
        lo, hi, base = PROFILE['l2_pc16']
        self.assertEqual((base, base + 2 * (hi - lo)), PROFILE['l2_bytes'])
        self.assertEqual(base + 2 * (0xb89c53 - lo), 0x200138a6)
        self.assertTrue(any(a <= 0x200fe000 < b for a, b, _ in PROFILE['protected']))
        self.assertFalse(any(a <= 0x282dd600 < b for a, b, _ in PROFILE['protected']))
        self.assertTrue(any(a <= 0x282dd600 < b for a, b in PROFILE['owned_ranges']))

    def test_independent_transfer_and_dsp_budgets(self):
        self.assertEqual((PROFILE['stored_max'], PROFILE['decoded_max']), (0x100000, 0xfffff))
        self.assertEqual(PROFILE['decoded_max'] + 1, 0x100000)  # command byte shares staging RAM
        self.assertEqual(DEVICE.update_max, 0x200000)
        self.assertEqual(DEVICE.flash_limit - DEVICE.flash_at, 0x300000)
        self.assertLess(DEVICE.update_max, DEVICE.flash_limit - DEVICE.flash_at)
        self.assertEqual(PROFILE['stored_max'] - PROFILE['stored_len'], 446500)
        self.assertEqual(PROFILE['decoded_max'] - PROFILE['decoded_len'], 211619)

    @unittest.skipUnless(STOCK, 'missing ELEKLOADER_DN2_SYX')
    def test_owner_stock_identity_inventory_and_exact_noop(self):
        raw, parsed, image = stock()
        sha = lambda data: hashlib.sha256(data).hexdigest()
        stored = parsed.stored[PROFILE['section']]
        decoded = parsed.section(PROFILE['section'])
        self.assertEqual((sha(raw), len(raw)), (RELEASE.syx_sha256, 2390304))
        self.assertEqual((sha(stored), len(stored)), (PROFILE['stored_sha256'], PROFILE['stored_len']))
        self.assertEqual((sha(decoded), len(decoded)), (PROFILE['decoded_sha256'], PROFILE['decoded_len']))
        self.assertEqual((sha(image), len(image)), (RELEASE.main_sha256, RELEASE.main_len))
        self.assertEqual(len(parsed.container), 1886016)
        self.assertEqual(DEVICE.update_max - 8 - len(parsed.container), 211128)

        # Independent assertion walker for this hash-pinned stock, not a public parser.
        records, chunks, offset = [], [], 0
        while offset < len(decoded):
            code, target, count, argument = struct.unpack_from('<4I', decoded, offset)
            end = offset + 16 + (0 if code & 0x100 else count)
            self.assertLessEqual(end, len(decoded))
            self.assertEqual(functools.reduce(int.__xor__, decoded[offset:offset + 16]), 0)
            records.append((offset, code & 0xffff, target, count, argument))
            chunks.append(struct.pack('<4I', code, target, count, argument) + decoded[offset + 16:end])
            offset = end
        self.assertEqual(offset, len(decoded))
        self.assertEqual(b''.join(chunks), decoded)
        self.assertEqual(len(records), PROFILE['record_count'])
        self.assertEqual(sum(bool(flags & 0x100) for _, flags, _, _, _ in records), 37)
        self.assertEqual([(off, flags, target, count, arg) for off, flags, target, count, arg in records
                          if flags & 0xc800],
                         [(0, 0x5001, 0x120230, 0, 0x2964),
                          (0x2964, 0x0801, 0x120230, 0, 0),
                          (0x2974, 0x5001, 0x1c12e2, 0, 0xc9bd8),
                          (0xcc54c, 0x8001, 0x1c12e2, 0, 0)])
        self.assertTrue(all(target % 4 == count % 4 == 0 for _, flags, target, count, _ in records if count))
        self.assertEqual(max(target + count for _, _, target, count, _ in records
                             if 0x20000000 <= target < 0x20100000), 0x2008823c)
        rebuilt = formats.write(parsed, parsed.stored[DEVICE.main_section], DEVICE)
        self.assertEqual(rebuilt['syx'], raw)
        formats.verify(rebuilt, parsed, image, DEVICE)
        print(json.dumps({'device': DEVICE.key, 'os': RELEASE.version, 'records': len(records),
                          'stored_bytes': len(stored), 'decoded_bytes': len(decoded),
                          'update_headroom': DEVICE.update_max - 8 - len(parsed.container),
                          'stock_sha256': sha(raw), 'stored_sha256': sha(stored),
                          'decoded_sha256': sha(decoded), 'firmware_written': False}, sort_keys=True))

    @unittest.skipUnless(STOCK, 'missing ELEKLOADER_DN2_SYX')
    def test_sender_evidence_still_matches_stock(self):
        _, _, image = stock()
        start = 0x400cf4b0 - DEVICE.main_load
        sender = image[start:start + 828]
        self.assertEqual(hashlib.sha256(sender).hexdigest(),
                         'fca04573f758de9f75fb20b3a56ebf4fc97d20ee8a21fa1a6f990219328cc082')


if __name__ == '__main__':
    unittest.main()
