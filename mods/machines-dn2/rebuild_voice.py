# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild the pinned machine adapters with the qualified Selache checkout."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('selache', type=Path)
    args = parser.parse_args()
    here = Path(__file__).resolve().parent
    tools = args.selache / 'target/release'
    for kind, start in (('voice', 0x1790000), ('filter', 0x1794000)):
        rebuild(here, tools, kind, start)


def rebuild(here, tools, kind, start):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'layout.ldf').write_text('''ARCHITECTURE(ADSP-21569)
$OBJECTS=$COMMAND_LINE_OBJECTS;
MEMORY {code {TYPE(BW RAM) START(0x%x) END(0x%x) WIDTH(8)}}
PROCESSOR p0 {OUTPUT($COMMAND_LINE_OUTPUT_FILE) SECTIONS {
 bridge SW {INPUT_SECTIONS($OBJECTS(seg_swco))} > code}}
''' % (start, start + 2047))
        subprocess.run([tools / 'selas', '-proc', 'ADSP-21569', '-o', tmp / 'bridge.doj',
                        here / (kind + '-bridge.s')], check=True)
        subprocess.run([tools / 'seld', '-proc', 'ADSP-21569', '-T', tmp / 'layout.ldf',
                        '-entry', 'dn2_' + kind + '_entry', '-o', tmp / 'bridge.dxe', tmp / 'bridge.doj'], check=True)
        raw = (tmp / 'bridge.dxe').read_bytes()
        shoff, = struct.unpack_from('<I', raw, 32)
        count, = struct.unpack_from('<H', raw, 48)
        sections = [struct.unpack_from('<10I', raw, shoff + 40 * i) for i in range(count)]
        code = [raw[s[4]:s[4] + s[5]] for s in sections if s[2] & 4]
        assert len(code) == 1 and len(code[0]) <= 1984 and len(code[0]) % 2 == 0
        size = len(code[0])
        code = b''.join(code[0][i:i + 2][::-1] for i in range(0, size, 2))
        code += b'\x01\x00' * ((2048 - size) // 2)
        target = here.parents[1] / ('elekloader/sdk/include/digitone-mk2/' + kind + '-bridge.json')
        target.write_text(json.dumps({'offset_bytes': 0, 'kind': 'code', 'hex': code.hex()}, indent=1) + '\n')
        print(kind, 'adapter:', size, 'bytes; padded SHA-256:', hashlib.sha256(code).hexdigest())


if __name__ == '__main__':
    main()
