# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild the inspected assembly sine with the existing keyed C entry wrapper."""
import argparse
import json
import os
from pathlib import Path
import re
import struct
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(HERE.parent / 'sharc-sine-probe'))
from qualify import FLAGS, PIN, command, digest, require, section_rows, validate_data
from execute import check_dag_store

CODE_SHA256 = '15f6dbd8897a2986003b139876398824db3a0eb73aa08b72a835ea35fe7b2871'
ARTIFACT_CODE_SHA256 = 'd8c160e0fb4284e779c1eefdf7a0e1c1fc7a945fe69c3354667af126845fce9e'
DATA_SHA256 = '32040df54c1330b3325255cf5ad18101caf2dabd697a7a10040a99aa530c40d3'


def check_arithmetic(encoded, mnemonic):
    """PRM rev 1.5, 18-3/4 and 18-10: this kernel's new compute fields."""
    value = int(encoded, 16)
    match = re.fullmatch(r'r([0-7])=r([0-7])(\+| and | xor )r([0-7])', mnemonic)
    if match:
        dest, left, op, right = match.groups()
        dest, left, right = int(dest), int(left), int(right)
        if dest == left:
            want = 0xc000 | {'+': 0, ' and ': 12, ' xor ': 14}[op] << 8 | dest << 4 | right
            require(len(encoded) == 4 and value == want, 'wrong short-compute bit fields')
        else:
            require(op == ' and ' and len(encoded) == 8 and
                    value == 0x01840000 | dest << 8 | left << 4 | right,
                    'wrong full AND compute bit fields')
        return True
    match = re.fullmatch(r'r([0-7])=r([0-7])-1', mnemonic)
    if match:
        dest, source = map(int, match.groups())
        require(len(encoded) == 4 and value == 0xc600 | dest << 4 | source,
                'wrong short decrement bit fields')
        return True
    match = re.fullmatch(r'r([0-7])=lshift r([0-7]) by r([0-7])', mnemonic)
    if match:
        dest, source, shift = map(int, match.groups())
        require(len(encoded) == 8 and value == 0x01a00000 | dest << 8 | source << 4 | shift,
                'wrong register-shift bit fields')
        return True
    return False


def build(selache, output=None):
    selache = Path(selache).expanduser().resolve()
    output = Path(output or HERE / 'sine.sharc.json').resolve()
    require(command(['git', 'rev-parse', 'HEAD'], cwd=selache, text_output=True).strip() == PIN,
            'Selache checkout is not at the pinned commit')
    require(not command(['git', 'status', '--porcelain', '--untracked-files=normal'],
                        cwd=selache, text_output=True).strip(), 'Selache checkout is not clean')
    tools = selache / 'target/release'
    work = Path(tempfile.mkdtemp(prefix='dn2-asm-sine-'))
    command([tools / 'selas', '-proc', 'ADSP-21569', '-o', work / 'sine.doj', HERE / 'sine.s'])
    command([tools / 'selcc', *FLAGS, '-no-std-inc', '-I', selache / 'libsel/include',
             '-c', '-o', work / 'entry.doj', HERE.parent / 'sharc-sine/entry.c'])
    exe = work / 'sine.dxe'
    command([tools / 'seld', '-proc', 'ADSP-21569', '-T', HERE.parent / 'sharc-sine/keyed.ldf',
             '-entry', 'sharc_init.', '-Map', work / 'sine.map', '-o', exe,
             work / 'sine.doj', work / 'entry.doj'])
    raw = exe.read_bytes()
    dump = command([tools / 'seldump', '-fh', '-sh', '-n', '.symtab', exe], text_output=True)
    listing = command([tools / 'seldump', '-ns', 'probe_code', exe], text_output=True)
    (work / 'symbols.txt').write_text(dump)
    (work / 'instructions.txt').write_text(listing)
    rows = section_rows(dump, raw)
    require({name for name, row in rows.items() if row['flags'] & 2} ==
            {'probe_code', 'probe_data'}, 'unexpected loadable section')
    require(not any(row['kind'] in ('rel', 'rela') for row in rows.values()) and
            not re.search(r"(?m)^[0-9a-f]{4}\s+UNDEF.*'[^']+'$", dump), 'unresolved symbol/relocation')
    for name, expected in {'probe_code': (0xbc8000, 520, 7, CODE_SHA256),
                           'probe_data': (0x20094000, 204, 3, DATA_SHA256)}.items():
        row = rows[name]
        require((row['address'], row['size'], row['flags'], digest(row['bytes'])) == expected,
                f'{name} differs from the inspected assembly mod')
    for name, address in (('sharc_init.', 0xbc8058), ('sharc_render.', 0xbc8077),
                           ('steps12.', 0x20094098)):
        require(re.search(r"(?m)^[0-9a-f]{4}\s+[0-9a-f]{8}\s+%08x\s+.*'%s'$"
                          % (address, re.escape(name)), dump), f'{name} moved')
    require(struct.unpack_from('<I', raw, 24)[0] == 0xbc8058, 'wrong executable entry')
    validate_data(rows['probe_data']['bytes'][:152])
    require(rows['probe_data']['bytes'][200:] == bytes(4), 'gain does not start at zero')
    instructions = re.findall(r'(?m)^  ([0-9a-f]{8})\s+([0-9a-f]+)\s+(.+)$', listing)
    require(b''.join(bytes.fromhex(code) for _, code, _ in instructions) == rows['probe_code']['bytes'],
            'incomplete instruction listing')
    for address, encoded, mnemonic in instructions:
        check_dag_store(mnemonic)
        if int(address, 16) < 0xbc8058:
            check_arithmetic(encoded, mnemonic)
    native_code = rows['probe_code']['bytes']
    code = b''.join(native_code[i:i + 2][::-1] for i in range(0, len(native_code), 2))
    code += bytes.fromhex('01000100') * ((16384 - len(code)) // 4)
    require(len(code) == 16384 and digest(code) == ARTIFACT_CODE_SHA256, 'incorrect code padding/byte order')
    app = {'abi': 'dn2-master-mono32-l2-v1',
           'segments': [{'offset_bytes': 0, 'kind': 'code', 'hex': code.hex()},
                        {'offset_bytes': 16384, 'kind': 'data', 'hex': rows['probe_data']['bytes'].hex()}],
           'zero': [{'offset_bytes': 16588, 'size_bytes': 16180}],
           'entries': {'init_offset_bytes': 176, 'render_offset_bytes': 238},
           'stack_bytes': 128}
    sys.path.insert(0, str(ROOT))
    from elekloader import sharc
    from elekloader.devices.digitone_mk2 import DEVICE
    sharc.module_payload(app, DEVICE.releases['1.12'].sharc)
    staged = output.with_name(output.name + '.tmp')
    staged.write_text(json.dumps(app, indent=1) + '\n')
    os.replace(staged, output)
    sources = [HERE / 'sine.s', HERE / 'rebuild.py', HERE.parent / 'sharc-sine/entry.c',
               HERE.parent / 'sharc-sine/keyed.ldf']
    report = {'selache_commit': PIN, 'sources_sha256': {
                  str(path.relative_to(ROOT)): digest(path.read_bytes()) for path in sources},
              'tools_sha256': {name: digest((tools / name).read_bytes())
                               for name in ('selcc', 'selas', 'seld', 'seldump')},
              'executable_sha256': digest(raw), 'artifact_sha256': digest(output.read_bytes()),
              'code_bytes': 520, 'data_bytes': 204, 'owner_bytes': 32768,
              'entries': app['entries'], 'declared_stack_bytes': 128,
              'target_executed': False}
    (work / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: exact assembly mod, shared entry wrapper, data layout and 32 KiB owner')
    print(output)
    print('Author executable:', exe)
    return app


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('selache', type=Path)
    parser.add_argument('--out', type=Path, default=HERE / 'sine.sharc.json')
    args = parser.parse_args()
    try:
        build(args.selache, args.out)
    except (OSError, ValueError) as error:
        sys.exit(f'FAIL: {error}')
