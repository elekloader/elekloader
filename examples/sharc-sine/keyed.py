# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild the bounded DN2 keyed-sine core and mod with pinned Selache."""
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.path.insert(0, str(HERE.parent / 'sharc-sine-probe'))
from qualify import FLAGS, PIN, TOOLS, command, digest, require, section_rows, validate_data  # noqa: E402
from h1_spills import verify_full_width_spills  # noqa: E402


def _loader_code(data):
    return b''.join(data[i:i + 2][::-1] for i in range(0, len(data), 2))


def build(selache, source_dir=HERE, app_out=None, core_out=None):
    selache = Path(selache).resolve()
    source_dir = Path(source_dir).resolve()
    app_out = Path(app_out or HERE / 'sine.sharc.json').resolve()
    core_out = Path(core_out or ROOT / 'mods/core-dn2/bridge.sharc.json').resolve()
    require(command(['git', 'rev-parse', 'HEAD'], cwd=selache, text_output=True).strip() == PIN,
            'Selache checkout is not at the pinned commit')
    require(not command(['git', 'status', '--porcelain', '--untracked-files=normal'],
                        cwd=selache, text_output=True).strip(), 'Selache checkout is not clean')
    tools = {name: selache / 'target/release' / name for name in TOOLS}
    require(all(tool.is_file() for tool in tools.values()), 'build the four pinned Selache tools')
    flags = FLAGS + ['-no-std-inc', '-I', str(selache / 'libsel/include')]
    out = Path(tempfile.mkdtemp(prefix='dn2-keyed-'))

    def run(args, text_output=False):
        return command(args, text_output=text_output)

    for name, path in (
        ('sine', source_dir / 'sine.c'),
        ('entry', source_dir / 'entry.c'),
    ):
        run([tools['selcc'], *flags, '-c', '-o', out / f'{name}.doj', path])
    bridge_source = ROOT / 'mods/core-dn2/sharc-bridge.s'
    run([tools['selas'], '-proc', 'ADSP-21569', '-o', out / 'bridge.doj', bridge_source])
    exe = out / 'keyed.dxe'
    run([tools['seld'], '-proc', 'ADSP-21569', '-T', HERE / 'keyed.ldf',
         '-entry', 'bridge', '-Map', out / 'keyed.map', '-o', exe,
         out / 'sine.doj', out / 'entry.doj', out / 'bridge.doj'])
    raw = exe.read_bytes()
    dump = run([tools['seldump'], '-fh', '-sh', '-n', '.symtab', exe], True)
    rows = section_rows(dump, raw)
    require({name for name, row in rows.items() if row['flags'] & 2} ==
            {'bridge_code', 'probe_code', 'probe_data'}, 'unexpected loadable section')
    require(not any(row['kind'] in ('rel', 'rela') for row in rows.values()),
            'remaining relocation')
    require(not re.search(r"(?m)^[0-9a-f]{4}\s+UNDEF.*'[^']+'$", dump),
            'named unresolved symbol')
    expected = {
        'bridge_code': (0x16eb06, 948, 7, 'aeba817a60de4c292865136c6835a1604aa2fe8600f86cdfd489d912d2974b8c'),
        'probe_code': (0xbc8000, 664, 7, 'd4b28fac03b78af8e1b7227a0628d1f7199871805fb07af29d930fdb951ec7f2'),
        'probe_data': (0x20094000, 204, 3, '32040df54c1330b3325255cf5ad18101caf2dabd697a7a10040a99aa530c40d3'),
    }
    for name, wanted in expected.items():
        row = rows[name]
        require((row['address'], row['size'], row['flags'], digest(row['bytes'])) == wanted,
                f'{name} differs from the qualified keyed-sine section')
    require(struct.unpack_from('<I', raw, 24)[0] == 0x16eb06, 'wrong ELF entry')
    for symbol, address in (('bridge_restored', 0x16ecd6), ('sharc_init.', 0xbc80a0),
                            ('sharc_render.', 0xbc80bf), ('steps12.', 0x20094098)):
        require(re.search(r"(?m)^[0-9a-f]{4}\s+[0-9a-f]{8}\s+%08x\s+.*'%s'$"
                          % (address, re.escape(symbol)), dump), f'{symbol} moved')
    data = rows['probe_data']['bytes']
    validate_data(data[:152])
    require(data[0x90:0x98] == bytes(8) and data[0xc8:0xcc] == bytes(4),
            'phase, increment or gain does not cold-start at zero')
    expected_steps = tuple(round((440 * 2 ** ((note - 69) / 12)) * (1 << 32) / 48000)
                           for note in range(36, 48))
    require(struct.unpack_from('<12I', data, 0x98) == expected_steps,
            'equal-tempered phase table differs')
    listing = run([tools['seldump'], '-ns', 'bridge_code', exe], True)
    verify_full_width_spills(rows['bridge_code']['bytes'], 0x16eb06, listing)

    sys.path.insert(0, str(ROOT))
    from elekloader import sharc
    from elekloader.devices.digitone_mk2 import DEVICE
    profile = DEVICE.releases['1.12'].sharc
    nop = bytes.fromhex('01000100')
    core_code = _loader_code(rows['bridge_code']['bytes'])
    require(len(core_code) <= 996, 'bridge overlaps its state')
    core_code += (nop * ((996 - len(core_code)) // 4))
    require(len(core_code) == 996, 'bridge padding is not a full NOP word')
    app_code = _loader_code(rows['probe_code']['bytes'])
    require(len(app_code) <= 16320, 'mod code overlaps its data')
    app_code += nop * ((16384 - len(app_code)) // 4)
    require(len(app_code) == 16384, 'mod code padding is not a full NOP word')
    core = {'abi': profile['abi'],
            'segments': [{'offset_bytes': 12, 'kind': 'code', 'hex': core_code.hex()}],
            'zero': [{'offset_bytes': 1008, 'size_bytes': 16}],
            'entries': {'hook_offset_bytes': 12}, 'stack_bytes': 116}
    app = {'abi': profile['l2_abi'],
           'segments': [{'offset_bytes': 0, 'kind': 'code', 'hex': app_code.hex()},
                        {'offset_bytes': 16384, 'kind': 'data', 'hex': data.hex()}],
           'zero': [{'offset_bytes': 16588, 'size_bytes': 16180}],
           'entries': {'init_offset_bytes': 320, 'render_offset_bytes': 382},
           'stack_bytes': 128}
    sharc.module_payload(core, profile, core=True)
    sharc.module_payload(app, profile)
    document, qualified = core, profile['active_keyed_core']
    require([(part['offset_bytes'], part['kind'], digest(bytes.fromhex(part['hex'])))
             for part in document['segments']] == list(qualified['segments'])
            and [(part['offset_bytes'], part['size_bytes']) for part in document['zero']]
            == list(qualified['zero'])
            and document['entries'] == qualified['entries']
            and document['stack_bytes'] == qualified['stack_bytes'],
            'generated artifact is not accepted by the device profile')
    for target, document in ((core_out, core), (app_out, app)):
        staged = target.with_name(target.name + '.tmp')
        staged.write_text(json.dumps(document, indent=1) + '\n')
        os.replace(staged, target)
    print('PASS: pinned keyed-sine sections, table, layout and full-width spills')
    print('Core:', core_out)
    print('Mod:', app_out)
    return core, app
