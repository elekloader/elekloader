# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild the DSP peek (peek.s) with the qualified Selache checkout and print the bytes the
device profiles pin as peek_code, in loader order."""
import argparse
import hashlib
from pathlib import Path
import struct
import subprocess
import tempfile

START = 0x2e0000          # L1 block 1, after the machine stacks; PC16 0x170000


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('selache', type=Path)
    tools = parser.parse_args().selache / 'target/release'
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'layout.ldf').write_text('''ARCHITECTURE(ADSP-21569)
$OBJECTS=$COMMAND_LINE_OBJECTS;
MEMORY {code {TYPE(BW RAM) START(0x%x) END(0x%x) WIDTH(8)}}
PROCESSOR p0 {OUTPUT($COMMAND_LINE_OUTPUT_FILE) SECTIONS {
 peek SW {INPUT_SECTIONS($OBJECTS(seg_swco))} > code}}
''' % (START, START + 0x5ff))
        subprocess.run([tools / 'selas', '-proc', 'ADSP-21569', '-o', tmp / 'peek.doj',
                        Path(__file__).resolve().parent / 'peek.s'], check=True)
        subprocess.run([tools / 'seld', '-proc', 'ADSP-21569', '-T', tmp / 'layout.ldf',
                        '-entry', 'dn2_peek_entry', '-o', tmp / 'peek.dxe', tmp / 'peek.doj'], check=True)
        raw = (tmp / 'peek.dxe').read_bytes()
        shoff, = struct.unpack_from('<I', raw, 32)
        count, = struct.unpack_from('<H', raw, 48)
        sections = [struct.unpack_from('<10I', raw, shoff + 40 * i) for i in range(count)]
        code = [raw[s[4]:s[4] + s[5]] for s in sections if s[2] & 4]
        assert len(code) == 1 and len(code[0]) <= 0x600 and len(code[0]) % 2 == 0
        code = b''.join(code[0][i:i + 2][::-1] for i in range(0, len(code[0]), 2))
        code += b'\x01\x00' * (len(code) // 2 % 2)        # to whole words
        print('peek: %d bytes; SHA-256 %s' % (len(code), hashlib.sha256(code).hexdigest()))
        print(code.hex())
        symtab = next(s for s in sections if s[1] == 2)
        names = sections[symtab[6]]
        for at in range(symtab[4], symtab[4] + symtab[5], 16):
            name, value = struct.unpack_from('<II', raw, at)
            name = raw[names[4] + name:raw.index(b'\0', names[4] + name)].decode()
            if name.startswith('dn2_') and name.endswith('_entry'):
                print('%s: PC16 %#x' % (name, value))       # symbols are PC16


if __name__ == '__main__':
    main()
