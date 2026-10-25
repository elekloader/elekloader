# SPDX-License-Identifier: GPL-2.0-or-later
"""Package prelinked DN2 machine kernels; compilation belongs to the mod."""
import json
from pathlib import Path

from ..devices.digitone_mk2 import DEVICE
from ..sharc import module_payload, pc16


def machine_layout(kind):
    """Linker bounds for a voice or filter; no compiler-specific linker syntax."""
    if kind not in ('voice', 'filter'):
        raise ValueError('DN2 machine kind must be voice or filter')
    profile = DEVICE.releases['1.12'].sharc
    lo, hi = profile[kind + '_range']
    middle = (hi - lo) // 2
    return {'code_pc16': pc16(profile, lo + profile[kind + '_adapter_bytes']),
            'code_bytes': middle - profile[kind + '_adapter_bytes'],
            'data_address': lo + middle,
            'data_bytes': middle - profile[kind + '_state_bytes']}


def voice_artifact(code, data, render_offset_bytes, machine_id, stack_bytes):
    """Kernel PC16 0xbc8400, data 0x20092000; entry offset excludes the adapter."""
    return _machine_artifact('voice', code, data, render_offset_bytes, machine_id, stack_bytes)


def filter_artifact(code, data, render_offset_bytes, machine_id, stack_bytes):
    """Kernel PC16 0xbca400, data 0x20096000; entry offset excludes the adapter."""
    return _machine_artifact('filter', code, data, render_offset_bytes, machine_id, stack_bytes)


def _machine_artifact(kind, code, data, render_offset_bytes, machine_id, stack_bytes):
    profile = DEVICE.releases['1.12'].sharc
    layout = machine_layout(kind)
    prefix = profile[kind + '_adapter_bytes']
    data_offset = prefix + layout['code_bytes']
    state = data_offset + layout['data_bytes']
    if not isinstance(code, bytes) or not code or len(code) % 2 or len(code) > layout['code_bytes'] - 64:
        raise ValueError('DN2 %s code must fit 6 KiB with its 64-byte guard' % kind)
    if type(render_offset_bytes) is not int or render_offset_bytes % 2 or not 0 <= render_offset_bytes < len(code):
        raise ValueError('DN2 %s entry must be even and inside the supplied code' % kind)
    if not isinstance(data, bytes) or len(data) % 4 or len(data) > layout['data_bytes']:
        raise ValueError('DN2 %s data must fit 7 KiB and be four-byte aligned' % kind)
    adapter = json.loads((Path(__file__).parent / ('include/digitone-mk2/' + kind + '-bridge.json')).read_text())
    code += b'\x01\x00' * ((layout['code_bytes'] - len(code)) // 2)
    end = data_offset + len(data)
    doc = {'abi': profile[kind + '_abi'], 'machine_id': machine_id,
           'segments': [adapter, {'offset_bytes': prefix, 'kind': 'code', 'hex': code.hex()},
                        *([{'offset_bytes': data_offset, 'kind': 'data', 'hex': data.hex()}] if data else [])],
           'zero': ([{'offset_bytes': end, 'size_bytes': state - end}] if end < state else [])
                   + [{'offset_bytes': state, 'size_bytes': profile[kind + '_state_bytes']}],
           'entries': {'render_offset_bytes': prefix + render_offset_bytes},
           'stack_bytes': stack_bytes}
    module_payload(doc, profile)
    return doc
