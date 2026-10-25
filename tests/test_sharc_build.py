# SPDX-License-Identifier: GPL-2.0-or-later
"""The artifact boundary must reject malformed output before firmware builds."""
import importlib.util
import os
import shutil
import struct
import sys
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch
import unittest

SPEC = importlib.util.spec_from_file_location(
    'sharc_example_build', Path(__file__).resolve().parents[1] / 'examples/sharc-sine/build.py')
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)
artifact, check_toolchain = build.artifact, build.check_toolchain


def executable(code=bytes.fromhex('0001000100010001')):
    raw = bytearray(52)
    raw[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HHII', raw, 16, 2, 0x85, 1, 0xbc8000)
    raw += code + bytes(204)
    dump = (f"0001 progbits 00000007 00bc8000 00000034 {len(code):08x}:B 0 0 4 1 'probe_code'\n"
            f"0002 progbits 00000003 20094000 {52+len(code):08x} 000000cc:B 0 0 4 0 'probe_data'\n"
            "0003 00000001 00bc8000 00000000 function global 00000000 'sharc_init.'\n"
            "0004 00000001 00bc8002 00000000 function global 00000000 'sharc_render.'\n"
            "0005 00000002 20094098 00000000 object global 00000000 'steps12.'\n")
    return bytes(raw), dump


class ArtifactTests(unittest.TestCase):
    def test_converts_code_and_reads_callback_symbols(self):
        app = artifact(*executable())
        self.assertEqual(app['entries'], {'init_offset_bytes': 0, 'render_offset_bytes': 4})
        self.assertEqual(bytes.fromhex(app['segments'][0]['hex']), bytes.fromhex('0100') * 8192)
        self.assertEqual(app['zero'], [{'offset_bytes': 16588, 'size_bytes': 16180}])
        changed = artifact(*executable(bytes.fromhex('c001c00100010001')))
        self.assertNotEqual(changed['segments'][0], app['segments'][0])

    def test_accepts_growing_data_with_a_moved_pitch_table(self):
        raw, dump = executable()
        grown = artifact(raw + bytes(8), dump.replace('000000cc:B', '000000d4:B')
                         .replace('20094098', '200940a0'))
        self.assertEqual(grown['entries'], artifact(raw, dump)['entries'])
        self.assertEqual(len(bytes.fromhex(grown['segments'][1]['hex'])), 212)
        self.assertEqual(grown['zero'], [{'offset_bytes': 16596, 'size_bytes': 16172}])

    def test_layout_and_guard_bound(self):
        app = artifact(*executable())
        self.assertEqual(app['abi'], 'dn2-master-mono32-l2-v1')
        self.assertEqual(app['segments'][1]['offset_bytes'], 16384)
        self.assertEqual(bytes.fromhex(app['segments'][0]['hex'])[-64:], b'\x01\x00' * 32)
        for count, accepted in ((16320, True), (16322, False)):
            raw, dump = executable(bytes(count))
            if accepted:
                self.assertEqual(len(bytes.fromhex(artifact(raw, dump)['segments'][0]['hex'])), 16384)
            else:
                with self.assertRaisesRegex(ValueError, '64-byte NOP read-ahead guard'):
                    artifact(raw, dump)

    @unittest.skipUnless(shutil.which('git'), 'needs git')
    def test_toolchain_pin_and_clean_source(self):
        with tempfile.TemporaryDirectory() as work:
            repo = Path(work)
            subprocess.run(['git', 'init', '-q', work], check=True)
            source = repo / 'source.c'
            source.write_text('initial\n')
            subprocess.run(['git', 'add', 'source.c'], cwd=repo, check=True)
            subprocess.run(['git', '-c', 'commit.gpgsign=false', '-c', 'user.name=Build Test', '-c', 'user.email=test@example.invalid',
                            'commit', '-qm', 'Initialize compiler fixture'], cwd=repo, check=True)
            revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=repo, text=True).strip()
            with self.assertRaisesRegex(ValueError, 'Selache must be at'):
                check_toolchain(repo)
            with patch.object(build, 'SELACHE_PIN', revision):
                self.assertEqual(check_toolchain(repo), revision)
                source.write_text('modified\n')
                with self.assertRaisesRegex(ValueError, 'checkout must be clean'):
                    check_toolchain(repo)

    def test_rejects_invalid_executables_and_abi_layout(self):
        raw, dump = executable()
        cases = [
            (b'', dump, 'linked little-endian SHARC'),
            (raw[:-1], dump, 'truncated section'),
            (raw, dump.replace('progbits 00000007', 'progbits 00000003'), 'section attributes'),
            (raw, dump.replace('progbits', 'note', 1), 'allocated section must be progbits'),
            (raw, dump + "0006 seg info 00000003 20094000 00000034 00000004:B 0 0 4 0 'extra'\n",
             'allocated section must be progbits'),
            (raw, dump.replace('00bc8002', '00bc8008'), 'callback outside linked code'),
            (raw, dump.replace('20094000', '20098000'), 'data must fit its area'),
            (raw, dump.replace('00bc8000 00000034', '00bc8100 00000034'), '16384-byte area'),
            (raw, dump.replace('00bc8000 00000034', '0016ed00 00000034'), '16384-byte area'),
            (raw, dump.replace('progbits', 'rela', 1), 'unresolved relocation'),
            (raw, dump + "0006 UNDEF 00000000 00000000 function global 00000000 'missing'\n", 'unresolved symbol'),
            (raw, dump + "0006 progbits 00000003 20094000 00000034 00000004:B 0 0 4 0 'extra'\n", 'only probe_code'),
            (*executable(bytes(16388)), '16384-byte area'),
            (*executable(bytes(16322)), '64-byte NOP read-ahead guard'),
        ]
        for data, inspector, message in cases:
            with self.subTest(message=message), self.assertRaisesRegex(ValueError, message):
                artifact(data, inspector)

    def test_failed_compilation_removes_previous_firmware(self):
        with tempfile.TemporaryDirectory() as work:
            root = Path(work)
            stock = root / 'stock.syx'
            stock.touch()
            tools = root / 'target/release'
            tools.mkdir(parents=True)
            for name in ('selcc', 'selas', 'seld', 'seldump'):
                (tools / name).touch()
            output = root / 'out/c'
            output.mkdir(parents=True)
            firmware = output / 'firmware.syx'
            firmware.write_bytes(b'stale')
            args = ['build.py', 'c', '--stock', str(stock), '--selache', str(root)]
            with patch.object(build, 'HERE', root), patch.object(sys, 'argv', args), \
                    patch.dict(os.environ, {'DN2_DIGIHEALTH': ''}), \
                    patch.object(build, 'check_toolchain', return_value=build.SELACHE_PIN), \
                    patch.object(build, 'run', side_effect=subprocess.CalledProcessError(1, 'compiler')):
                with self.assertRaises(subprocess.CalledProcessError):
                    build.main()
            self.assertFalse(firmware.exists())


if __name__ == '__main__':
    unittest.main()
