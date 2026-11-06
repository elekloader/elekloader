# SPDX-License-Identifier: GPL-2.0-or-later
"""Package DN2 machine kernels: link a mod's SHARC objects with the pinned Selache into a relocatable
kernel artifact (dn2-kernel-v1) that the linker places anywhere in the machine pool; the build never
names an address.

`kernel_artifact` assembles sources and names each kernel's entry label (SDK machines, machines-dn2's
dispatcher); `machine_artifact` links an mk2 synth's or filter's objects, whose entry is
sharc_voice_render / sharc_filter_render (digitone-mk2/voice.h, filter.h)."""
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile

from .. import sharc as linker
from ..devices.digitone_mk2 import DEVICE
from ..elemod import ModError
from . import sharc_reloc

PROFILE = DEVICE.releases['1.12'].sharc
CODE_SECTION, DATA_SECTION = 'machine_code', 'machine_data'   # the sections KERNEL_LDF places
SELACHE_PIN = '2b26d3b75c53063575bc5c820fa0d38879335187'
KERNEL_CODE, KERNEL_DATA = 0xbc8400, 0x20098000      # where the probes link; the linker rebases
KERNEL_WINDOW = 0x40000                                # bytes either section may take in a probe link
KERNEL_LDF = """ARCHITECTURE(ADSP-21569)
$OBJECTS = $COMMAND_LINE_OBJECTS;
MEMORY {
 code { TYPE(BW RAM) START(0x%x) END(0x%x) WIDTH(8) }
 data { TYPE(BW RAM) START(0x%x) END(0x%x) WIDTH(8) }
}
PROCESSOR p0 { OUTPUT($COMMAND_LINE_OUTPUT_FILE) SECTIONS {
 %s SW { INPUT_SECTION_ALIGN(2) INPUT_SECTIONS($OBJECTS(seg_swco)) } > code
 %s BW { INPUT_SECTION_ALIGN(4) INPUT_SECTIONS($OBJECTS(seg_dmda)) } > data
} }
"""


def selache_tools():
    """The pinned Selache's target/release, from $SELACHE (a clean checkout). Raises ValueError."""
    root = os.environ.get('SELACHE')
    if not root:
        raise ValueError('SHARC sources need Selache: set SELACHE to a checkout at %s, built with '
                         'cargo build --release' % SELACHE_PIN[:7])
    try:
        head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
        dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=root, text=True).strip()
    except (OSError, subprocess.CalledProcessError) as e:
        raise ValueError('SELACHE=%s is not a git checkout: %s' % (root, e)) from e
    if head != SELACHE_PIN or dirty:
        raise ValueError('SELACHE must be a clean checkout at %s (it is at %s%s)'
                         % (SELACHE_PIN[:7], head[:7], ', modified' if dirty else ''))
    tools = Path(root) / 'target' / 'release'
    if not all((tools / t).is_file() for t in ('selas', 'seld')):
        raise ValueError('SELACHE has no target/release/selas and seld: run cargo build --release')
    return tools


def _executable(raw):
    """(code PC16, code, data address, data, {symbol: address}) of a linked SHARC ELF32."""
    if len(raw) < 52 or raw[:7] != b'\x7fELF\x01\x01\x01' or struct.unpack_from('<HH', raw, 16) != (2, 0x85):
        raise ValueError('SHARC kernel: expected a linked little-endian SHARC ELF32 executable')
    shoff, = struct.unpack_from('<I', raw, 32)
    _size, shnum, shstrndx = struct.unpack_from('<HHH', raw, 46)
    headers = [struct.unpack_from('<10I', raw, shoff + 40 * i) for i in range(shnum)]

    def text(table, at):
        start = headers[table][4] + at
        return raw[start:raw.index(b'\0', start)].decode('latin-1')
    sections = {}
    for name, kind, flags, address, offset, size, *_ in headers:
        label = text(shstrndx, name)
        if kind in (4, 9):                                     # SHT_RELA, SHT_REL
            raise ValueError('SHARC kernel: unresolved relocations in %s' % label)
        if flags & 2:                                          # SHF_ALLOC
            if kind != 1 or label not in (CODE_SECTION, DATA_SECTION):
                raise ValueError('SHARC kernel: only seg_swco code and seg_dmda data may be loaded, not %s' % label)
            sections[label] = (address, raw[offset:offset + size])
    symbols, undefined = {}, []
    for _n, kind, _f, _a, offset, size, strings, *_ in headers:
        if kind != 2:                                          # SHT_SYMTAB
            continue
        for at in range(offset + 16, offset + size, 16):
            name, value, _s, _i, _o, index = struct.unpack_from('<IIIBBH', raw, at)
            label = text(strings, name)
            if label and index == 0:
                undefined.append(label)
            elif label:
                symbols[label] = value
    if undefined:
        raise ValueError('SHARC kernel: unresolved symbols: ' + ', '.join(sorted(undefined)))
    if CODE_SECTION not in sections:
        raise ValueError('SHARC kernel: no code in seg_swco')
    code_pc, code = sections[CODE_SECTION]
    data_address, data = sections.get(DATA_SECTION, (None, b''))
    return code_pc, code, data_address, data, symbols


def _relocatable(tools, objects, work):
    """sharc_reloc.relocatable over seld links of `objects`."""
    work = Path(work)

    def link(objects, code_pc16, data_address):
        ldf, exe = work / 'kernel.ldf', work / 'kernel.dxe'
        ldf.write_text(KERNEL_LDF % (code_pc16 * 2, code_pc16 * 2 + KERNEL_WINDOW - 1,
                                     data_address, data_address + KERNEL_WINDOW - 1, CODE_SECTION, DATA_SECTION))
        _run([tools / 'seld', '-proc', 'ADSP-21569', '-T', ldf, '-o', exe, *objects])
        code_pc, code, at, data, symbols = _executable(exe.read_bytes())
        if code_pc != code_pc16 or (data and at != data_address):
            raise ValueError('SHARC kernel: sections moved from their base')
        return code, data, symbols
    return sharc_reloc.relocatable(link, list(objects), KERNEL_CODE, KERNEL_DATA)


def kernel_artifact(sources, work, kernels=()):
    """Assemble `sources` (.s files) with the pinned Selache: the dn2-kernel-v1 artifact, with one kernel
    per (kind, id, entry label, knobs) of `kernels` (an SDK machine's knobs are its (min, max) pairs) and
    the dispatcher's labels as exports when the sources define them. Raises ValueError."""
    tools = selache_tools()
    objects = []
    for i, src in enumerate(sources):
        obj = Path(work) / ('k%d.doj' % i)
        _run([tools / 'selas', '-proc', 'ADSP-21569', '-o', obj, src])
        objects.append(obj)
    art = _relocatable(tools, objects, work)
    entries = []
    for kind, mid, label, knobs in kernels:
        entries.append({'kind': kind, 'id': mid, 'entry': _code_symbol(art, label),
                        **({'knobs': [list(r) for r in knobs]} if kind == 'sdk' else {})})
    return kernel_doc(art, entries)


def machine_artifact(kind, objects, machine_id):
    """Link an mk2 synth's (kind 'voice') or filter's ('filter') Selache objects (.doj, from C or
    assembly): the dn2-kernel-v1 artifact of one kernel whose entry is sharc_<kind>_render and whose
    state, 16 slots, is sharc_<kind>_state_bytes's value (DN2_VOICE_STATE / DN2_FILTER_STATE), if
    defined, in whole words. Raises ValueError."""
    if kind not in ('voice', 'filter'):
        raise ValueError('DN2 machine kind must be voice or filter')
    tools = selache_tools()
    with tempfile.TemporaryDirectory() as work:
        art = _relocatable(tools, [Path(o) for o in objects], work)
    k = {'kind': 'mk2-' + kind, 'id': machine_id, 'entry': _code_symbol(art, 'sharc_%s_render.' % kind)}
    state = art['symbols'].get('sharc_%s_state_bytes.' % kind)
    if state:
        if state[0] != 'data' or state[1] + 4 > len(art['data']):
            raise ValueError('SHARC kernel: sharc_%s_state_bytes is not a word of data' % kind)
        size, = struct.unpack_from('<I', art['data'], state[1])
        k['state_bytes'] = size + -size % 4
    return kernel_doc(art, [k])


def _code_symbol(art, label):
    where = art['symbols'].get(label)
    if where is None:
        raise ValueError('SHARC kernel: the sources define no global label %s' % label)
    if where[0] != 'code':
        raise ValueError('SHARC kernel: %s is not a code label' % label)
    return where[1]


def kernel_doc(artifact, kernels):
    """The mod file's "sharc" for a relocatable kernel (sharc_reloc.relocatable's result), checked as
    the linker checks it. Raises ValueError."""
    doc = {'abi': linker.KERNEL_ABI, 'code': artifact['code'].hex(), 'data': artifact['data'].hex(),
           'code_pc16': artifact['code_pc16'], 'data_address': artifact['data_address'],
           'data_align': artifact['data_align'], 'relocs': [list(r) for r in artifact['relocs']],
           'kernels': kernels,
           'exports': {n: list(artifact['symbols'][n]) for n in linker.DISPATCH if n in artifact['symbols']}}
    try:
        linker.kernel_payload(doc, PROFILE)
    except ModError as e:
        raise ValueError(str(e)) from e
    return doc


def _run(cmd):
    p = subprocess.run([str(c) for c in cmd], capture_output=True, text=True)
    # Selache may print an error and still exit 0 (the assembler, like its C compiler).
    if p.returncode or re.search(r'(?i)\berror\b', p.stdout + p.stderr):
        raise ValueError('SHARC kernel: %s failed:\n%s' % (Path(str(cmd[0])).name, (p.stdout + p.stderr).strip()))
