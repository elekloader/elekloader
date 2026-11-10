# SPDX-License-Identifier: GPL-2.0-or-later
"""machines-dn2's dispatcher (dispatch.s), placed as the linker places it, run hook call by hook call in
DigiKit's SHARC runner with the frame OS 1.12 fills: each call ABI's fields, the state zeroed on a
machine change, the stack guard's mute, and several synths and filters side by side. The kernels are
tests/fixtures/sharc-kernel/probe.s, which record what they were given."""
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader import sharc   # noqa: E402
from elekloader.devices.digitone_mk2 import DEVICE   # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
PROBE = ROOT / 'tests' / 'fixtures' / 'sharc-kernel' / 'probe.s'
DIGIKIT = os.environ.get('ELEKLOADER_DIGIKIT')
PROFILE = DEVICE.releases['1.12'].sharc
# OS 1.12's DSP, as dispatch.s reads it: the frame copy and its halfwords, the slot-to-track map, the voice
# buffers, the hooks' continuations and the capture arrays.
FRAME, MAP, BUFFERS = 0x25c48c, 0x257898, 0x804acf90
# core-dn2's posts, as the DSP receives the frames: tail words 4..13 of its two receive buffers.
POSTS = (0x2c29d0 + 0xa80 + 16, 0x2c39d0 + 0xa80 + 16)
TYPE = {'voice': 74, 'filter': 90}
PITCH, VELOCITY, PARAMS, PARAM_STEP, NOTE_ON = 1, 26, 0x6d, 0x49, 0x11
EXIT = {'voice': 0xb8251e, 'filter': 0x1c958a}
CAP_WORD = 1152
STOCK_STACK = 0x2d0000
KNOBS = [(0, 127), (-12, 12), (-2147483648, 2147483647), (20, 20000), (-100, -99), (0, 32512),
         (0, 32513), (5, 6), (-1, 1000000)]


def sdk_alpha_map(lo, hi, raw):
    """mods/sdk-alpha/machine.c's sdk_alpha_map, in uint32 arithmetic."""
    span = (hi - lo) % 2 ** 32
    raw = max(0, min(raw, 32512))
    offset = (span // 32512 * raw + ((span % 32512) * raw + 16256) // 32512) % 2 ** 32
    value = (lo + offset) % 2 ** 32
    return value - 2 ** 32 if value >= 2 ** 31 else value


def divide(n):
    """dispatch.s's n / 32512 for n below 2 ** 30, step for step: t = n >> 8, then t / 127 by shifts and adds."""
    t, q = n >> 8, 0
    for _ in range(4):
        q, t = q + (t >> 7), (t & 127) + (t >> 7)
    return q + (t >= 127)


def dispatched(knob_map, raw):
    """dispatch.s's knob value for one knob's map words (min, q, r) and frame halfword."""
    mn, q, r = knob_map
    c = min(raw, 32512)
    value = (mn + q * c + divide(r * c + 16256)) % 2 ** 32
    return value - 2 ** 32 if value >= 2 ** 31 else value


class KnobMapTests(unittest.TestCase):
    """The dispatcher's knob arithmetic, from the linker's knob map, is sdk_alpha_map for every raw value."""

    def test_the_division_is_exact_for_every_numerator(self):
        # n / 32512 = (n >> 8) / 127 for whole numbers; every t = n >> 8 the dispatcher can see
        # (r below 32512, raw at most 32512) is below 2 ** 22.
        self.assertLess((32511 * 32512 + 16256) >> 8, 1 << 22)
        for t in range(1 << 22):
            if divide(t << 8) != t // 127:
                self.fail(t)

    def test_every_raw_value_of_every_knob(self):
        for lo, hi in KNOBS:
            count, *words = struct.unpack('<4I', sharc.knob_map([(lo, hi)]))
            self.assertEqual(count, 1)
            for raw in range(0, 65536):
                if dispatched(words, raw) != sdk_alpha_map(lo, hi, raw):
                    self.fail((lo, hi, raw))


def _runner():
    sys.path.insert(0, DIGIKIT)
    from tools.sharc_dev import sharc_run, sharcldr
    return sharc_run, sharcldr


class Machine:
    """The dispatcher and the kernels, placed in the pool, and the memory the runner sees."""

    def __init__(self, payloads):
        self.run, self.ldr = _runner()
        loads, fills, _patches, self.layout = sharc.place_kernels(payloads, PROFILE)
        lo, hi = PROFILE['pool']
        pool = bytearray(hi - lo)
        stacks = bytearray(PROFILE['filter_stack'][1] - PROFILE['voice_stack'][0])
        self.regions = {lo: pool, PROFILE['voice_stack'][0]: stacks}
        for at, data in loads:
            b, off = self._at(at)
            b[off:off + len(data)] = data
        self.code = [(sharc.pc16(PROFILE, e['code'][0]), bytes(pool[e['code'][0] - lo:e['code'][0] - lo + e['code'][1]]))
                     for e in self.layout['mods']]
        self.placed = {e['id']: e for e in self.layout['mods']}
        dispatch = self.placed['machines-dn2']
        exports = dict(payloads)['machines-dn2']['exports']
        self.hook = {k: sharc.pc16(PROFILE, dispatch['code'][0]) + exports['dn2_%s_hook.' % k][1]
                     for k in ('voice', 'filter')}
        self.capture = dispatch['data'][0] + 4 * CAP_WORD
        self.regions.update({FRAME & ~0xff: bytearray(0xa00), MAP & ~0xff: bytearray(0x100),
                             POSTS[0] & ~0xff: bytearray(0x1100),
                             BUFFERS: bytearray(16 * 128), STOCK_STACK: bytearray(0x1000)})

    def _at(self, address):
        for base, b in self.regions.items():
            if base <= address < base + len(b):
                return b, address - base
        raise KeyError(hex(address))

    def word(self, address, value=None, fmt='<I'):
        b, off = self._at(address)
        if value is None:
            return struct.unpack_from(fmt, b, off)[0]
        struct.pack_into(fmt, b, off, value)

    def halfword(self, h, value, base=FRAME):
        address = base + 2 * (h & ~1)
        old = self.word(address)
        self.word(address, (old & 0xffff) | value << 16 if h & 1 else (old & 0xffff0000) | value)

    def probe(self, mod, at=0, count=1):
        """Words of mod's probe_out. (data word 2)."""
        return [self.word(self.placed[mod]['data'][0] + 4 * (2 + at + i)) for i in range(count)]

    def block(self, kind, slot, note_on=0):
        """One hook call for `slot`; note_on: the frame's note-on mask. -> the slot's 32 samples."""
        self.halfword(NOTE_ON, note_on)
        out = BUFFERS + 128 * slot
        for i in range(32):
            self.word(out + 4 * i, 0x3e800000)               # 0.25: a call or silence changes it
        regs = {'R8': out, 'R12': out, 'R15': 16 - slot} if kind == 'voice' else {'I5': out, 'R2': 0, 'R15': 16 - slot}
        blocks, image = [], bytearray()
        for address, data in [(self.ldr.sw_to_byte(pc), code) for pc, code in self.code] + list(self.regions.items()):
            blocks.append({'target_address': address, 'byte_count': len(data), 'fill': False,
                           'payload_offset': len(image), 'payload_len': len(data)})
            image.extend(data)
        uregs = {'MODE1': 0x1000, 'I6': STOCK_STACK + 0x900, 'I7': STOCK_STACK + 0x800, 'B7': STOCK_STACK,
                 **{'L%d' % i: 0 for i in range(16)}, **regs}
        runner = self.run.Runner(self.ldr.LoadedMemory(bytes(image), blocks), self.hook[kind], regs=uregs,
                                 breakpoints=[EXIT[kind]])
        runner.state.stack_model = True
        while True:
            try:
                runner.step()
            except self.run.Halt as halt:
                if halt.reason != 'breakpoint':
                    raise AssertionError('the SHARC run stopped: %s' % halt)
                break
            if runner.instructions > 200000:
                raise AssertionError('a block ran past 200,000 instructions')
        for address, value in runner.state.overlay.items():
            b, off = self._at(address)
            b[off] = getattr(value, 'value', value)
        return [self.word(out + 4 * i, fmt='<f') for i in range(32)]


@unittest.skipUnless(DIGIKIT and os.environ.get('SELACHE'), 'needs DigiKit (ELEKLOADER_DIGIKIT) and Selache (SELACHE)')
class DispatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from elekloader.sdk.sharc import kernel_artifact, machine_artifact
        from elekloader.sdk.sharc import selache_tools
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        work = Path(cls.tmp.name)
        obj = work / 'probe.doj'
        import subprocess
        subprocess.run([str(selache_tools() / 'selas'), '-proc', 'ADSP-21569', '-o', str(obj), str(PROBE)], check=True)
        cls.dispatcher = json.loads((ROOT / 'mods' / 'machines-dn2' / 'dispatch.sharc.json').read_text())
        cls.docs = {
            'sdk-9': kernel_artifact([PROBE], work, [('sdk', 9, 'probe_sdk.', KNOBS)]),
            'sdk-40': kernel_artifact([PROBE], work, [('sdk', 40, 'probe_sdk.', [])]),
            'sdk-15': kernel_artifact([PROBE], work, [('sdk', 15, 'probe_sdk.', KNOBS + [(-50, 50), (0, 1), (1000, 1001),
                                                                                  (-2147483648, -2147483647),
                                                                                  (0, 2147483647), (7, 100)])]),
            'voice-5': machine_artifact('voice', [obj], 5),
            'voice-31': machine_artifact('voice', [obj], 31),
            'filter-6': machine_artifact('filter', [obj], 6),
            'filter-7': machine_artifact('filter', [obj], 7),
            'over-voice': kernel_artifact([PROBE], work, [('mk2-voice', 30, 'probe_overrun_voice.', ())]),
            'over-filter': kernel_artifact([PROBE], work, [('mk2-filter', 30, 'probe_overrun_filter.', ())]),
        }

    def machine(self, *names):
        payloads = [('machines-dn2', sharc.kernel_payload(self.dispatcher, PROFILE))]
        payloads += [(n, sharc.kernel_payload(self.docs[n], PROFILE)) for n in names]
        return Machine(payloads)

    def input_at(self, m):
        return m.placed['machines-dn2']['data'][0] + 4 * 1200

    def params_at(self, m):
        return m.placed['machines-dn2']['data'][0] + 4 * 1208

    def state(self, m, mod, slot, each):
        mid = self.docs[mod]['kernels'][0]['id']
        table = 'filter' if self.docs[mod]['kernels'][0]['kind'] == 'mk2-filter' else 'voice'
        entry = m.placed['machines-dn2']['data'][0] + 4 * (0 if table == 'voice' else 512) + 16 * mid
        base, size = m.word(entry + 4), m.word(entry + 8)
        self.assertEqual(size, each)
        return base + slot * each

    def test_an_sdk_machine_gets_struct_elek_voice(self):
        m = self.machine('sdk-9', 'sdk-40')
        slot, raws = 3, [0, 32512, 16256, 65535, 1, 32511, 12345, 32500, 777]
        m.halfword(TYPE['voice'] + slot, 9)
        m.halfword(PITCH + slot, 0x3c80)
        m.halfword(VELOCITY + slot, 100 << 8)
        m.halfword(slot, 5, MAP)                    # the slot plays track 6
        for k, raw in enumerate(raws):
            m.halfword(PARAMS + PARAM_STEP * slot + k, raw)
        state = self.state(m, 'sdk-9', slot, 256)
        self.assertEqual(m.block('voice', slot)[:2], [1.0, 0.25])
        self.assertEqual(m.probe('sdk-9', 0, 6), [self.params_at(m), 5, 0, 0x3c8000, 100, state])
        self.assertEqual(m.probe('sdk-9', 7, 3), [BUFFERS + 128 * slot, 0, 1])   # R8, the zeroed state, one call
        self.assertEqual([v - 2 ** 32 if v >= 2 ** 31 else v for v in m.probe('sdk-9', 16, 9)],
                         [sdk_alpha_map(lo, hi, raw) for (lo, hi), raw in zip(KNOBS, raws)])
        # A note-on's block is silent; the next one starts, with the state kept.
        self.assertEqual(m.block('voice', slot, 1 << slot), [0.0] * 32)
        m.block('voice', slot, 1 << slot)            # held: not an edge
        self.assertEqual(m.probe('sdk-9', 2), [1])
        self.assertEqual(m.probe('sdk-9', 8), [0x5a5a5a5a])
        m.block('voice', slot)
        self.assertEqual(m.probe('sdk-9', 2), [0])
        # The velocity is 1..127.
        for word, velocity in ((0, 1), (0x80, 1), (0x7f80, 127), (0xffff, 127)):
            m.halfword(VELOCITY + slot, word)
            m.block('voice', slot)
            self.assertEqual(m.probe('sdk-9', 4), [velocity])
        # Another machine on the slot, then back: the state is zeroed.
        m.halfword(TYPE['voice'] + slot, 40)
        m.block('voice', slot)
        self.assertEqual(m.probe('sdk-40', 0, 6)[5], self.state(m, 'sdk-40', slot, 256))
        m.halfword(TYPE['voice'] + slot, 9)
        m.block('voice', slot)
        self.assertEqual(m.probe('sdk-9', 8), [0])
        self.assertEqual(m.word(state), 0x5a5a5a5a)

    def post(self, m, buffer, i, number, machine, track, cc, value, check=None):
        """Post i of a receive buffer's tail: its number and its word, the number's low bits in bits 25-31."""
        m.word(POSTS[buffer] + 8 * i, number)
        word = machine << 18 | track << 14 | cc << 7 | value
        m.word(POSTS[buffer] + 8 * i + 4, word | ((number if check is None else check) & 127) << 25)

    def cc9(self, m, slot):
        """cc[9] as the slot's SDK machine saw it in its last render, after slot 0's call took the posts
        (once a block)."""
        if slot:
            m.block('voice', 0)
        m.block('voice', slot)
        return [v - 2 ** 32 if v >= 2 ** 31 else v for v in m.probe('sdk-9', 59)][0]

    def test_an_sdk_machine_reads_the_ccs_posted_for_its_track(self):
        m = self.machine('sdk-9', 'sdk-40')
        slot = 3
        m.halfword(TYPE['voice'] + slot, 9)
        m.halfword(slot, 5, MAP)                    # the slot plays track 6 (5 from 0)
        self.assertEqual(self.cc9(m, slot), -1)
        self.post(m, 0, 0, 1, 9, 5, 9, 64)
        self.assertEqual(self.cc9(m, slot), 64)
        self.assertEqual(self.cc9(m, slot), -1)      # once
        # In both buffers, any order: oldest first, so the last value is the newest's.
        self.post(m, 1, 2, 3, 9, 5, 9, 71)
        self.post(m, 0, 4, 2, 9, 5, 9, 70)
        self.assertEqual(self.cc9(m, slot), 71)
        self.assertEqual(m.word(m.placed['machines-dn2']['data'][0] + 4 * 1318), 3)    # cc_last
        # A post whose check does not match its number is still arriving: taken once it does.
        self.post(m, 0, 1, 4, 9, 5, 9, 99, check=0)
        self.assertEqual(self.cc9(m, slot), -1)
        self.post(m, 0, 1, 4, 9, 5, 9, 99)
        self.assertEqual(self.cc9(m, slot), 99)
        # Another track's CC, or an old number, does not reach it.
        self.post(m, 0, 2, 5, 9, 6, 9, 1)
        self.post(m, 1, 0, 2, 9, 5, 9, 2)
        self.assertEqual(self.cc9(m, slot), -1)
        # A CC for the track's machine before the slot plays it reaches its first render there; one for
        # the machine before does not.
        m.halfword(TYPE['voice'] + slot, 40)
        m.block('voice', slot)
        self.post(m, 0, 3, 6, 9, 5, 9, 33)           # the track has 9 again, the slot still plays 40
        m.halfword(TYPE['voice'] + slot, 9)
        self.assertEqual(self.cc9(m, slot), 33)
        m.halfword(TYPE['voice'] + slot, 40)
        m.block('voice', slot)
        self.post(m, 1, 1, 7, 40, 5, 9, 44)           # for machine 40
        m.halfword(TYPE['voice'] + slot, 9)
        self.assertEqual(self.cc9(m, slot), -1)

    def test_the_knobs_run_as_the_model_says(self):
        import random
        rng = random.Random(7)
        knobs = KNOBS + [(-50, 50), (0, 1), (1000, 1001), (-2147483648, -2147483647), (0, 2147483647), (100, 7)]
        knobs[-1] = (7, 100)
        m = self.machine('sdk-15')
        m.halfword(TYPE['voice'] + 0, 15)
        for block in range(12):
            raws = [rng.choice((0, 1, 16255, 16256, 32511, 32512, 32513, 65535)) if block < 4 else rng.randrange(65536)
                    for _ in knobs]
            for k, raw in enumerate(raws):
                m.halfword(PARAMS + k, raw)
            m.block('voice', 0)
            self.assertEqual([v - 2 ** 32 if v >= 2 ** 31 else v for v in m.probe('sdk-15', 16, 15)],
                             [sdk_alpha_map(lo, hi, raw) for (lo, hi), raw in zip(knobs, raws)])

    def test_the_sdk_sine_example_plays_through_the_dispatcher(self):
        from elekloader.sdk.sharc import kernel_artifact
        with tempfile.TemporaryDirectory() as work:
            doc = kernel_artifact([ROOT / 'examples/sdk-sine/sine-dn2.s'], work, [('sdk', 123, 'sine_render.', [(-50, 50)])])
        m = Machine([('machines-dn2', sharc.kernel_payload(self.dispatcher, PROFILE)),
                     ('sdk-sine', sharc.kernel_payload(doc, PROFILE))])
        m.halfword(TYPE['voice'] + 2, 123)
        m.halfword(PITCH + 2, 69 << 8)
        m.halfword(VELOCITY + 2, 100 << 8)
        m.halfword(PARAMS + PARAM_STEP * 2, 16256)            # FINE 0
        self.assertEqual(m.block('voice', 2, 1 << 2), [0.0] * 32)
        out = [x for _ in range(12) for x in m.block('voice', 2)]
        self.assertTrue(all(abs(x) <= 1 for x in out) and max(out) > 0.1)
        rising = [i for i in range(1, len(out)) if out[i - 1] < 0 <= out[i]]
        self.assertAlmostEqual((rising[-1] - rising[0]) / (len(rising) - 1), 48000 / 440, delta=1)
        # CC 9 on its track (0) restarts the wave at zero, as a note does.
        self.assertNotEqual(m.block('voice', 2)[0], 0.0)
        self.post(m, 0, 0, 1, 123, 0, 9, 5)
        m.block('voice', 0)                                   # slot 0's call takes the post
        self.assertEqual(m.block('voice', 2)[0], 0.0)
        self.assertNotEqual(m.block('voice', 2)[0], 0.0)

    def test_an_mk2_synth_gets_dn2_voice_input(self):
        m = self.machine('voice-5', 'voice-31')
        slot = 7
        m.halfword(TYPE['voice'] + slot, 5)
        m.halfword(PITCH + slot, 0x4500)
        for k in range(41):
            m.halfword(PARAMS + PARAM_STEP * slot + k, 1000 + k)
        state = self.state(m, 'voice-5', slot, 12)
        m.block('voice', slot)
        self.assertEqual(m.probe('voice-5', 0, 6), [BUFFERS + 128 * slot, slot, 0x4500, 0, self.params_at(m), state])
        self.assertEqual(m.probe('voice-5', 7, 3), [BUFFERS + 128 * slot, 0, 1])
        self.assertEqual(m.probe('voice-5', 16, 40), [1000 + k for k in range(40)])
        self.assertEqual(m.block('voice', slot, 1 << slot), [0.0] * 32)   # the trigger's block
        m.block('voice', slot)
        self.assertEqual(m.probe('voice-5', 3), [1])                   # DN2_VOICE_START
        m.block('voice', slot)
        self.assertEqual(m.probe('voice-5', 3), [0])
        # The other synth on the next slot: its own state and its own calls.
        m.halfword(TYPE['voice'] + slot + 1, 31)
        m.block('voice', slot + 1)
        self.assertEqual(m.probe('voice-31', 5), [self.state(m, 'voice-31', slot + 1, 12)])
        self.assertEqual(m.probe('voice-31', 9), [1])
        self.assertEqual(m.probe('voice-5', 9), [3])
        # A stock machine is not called.
        m.halfword(TYPE['voice'] + slot, 2)
        self.assertEqual(m.block('voice', slot), [0.25] * 32)
        self.assertEqual(m.probe('voice-5', 9), [3])

    def test_an_mk2_filter_gets_dn2_filter_input(self):
        m = self.machine('filter-6', 'filter-7', 'voice-5')
        for slot, mid in ((2, 6), (3, 7)):
            m.halfword(TYPE['filter'] + slot, mid)
            for at, value in ((0, 0.5), (64, 0.25), (128, 0.75)):
                m.word(m.capture + at + 4 * slot, value + slot, '<f')
        m.block('filter', 2)
        flags, cutoff, resonance, g = m.probe('filter-6', 2, 4)
        self.assertEqual(m.probe('filter-6', 0, 2) + [flags], [BUFFERS + 256, 2, 1])   # RESET on entering
        self.assertEqual([struct.unpack('<f', struct.pack('<I', w))[0] for w in (cutoff, resonance, g)],
                         [2.5, 2.25, 2.75])
        self.assertEqual(m.probe('filter-6', 6, 3), [self.state(m, 'filter-6', 2, 8), BUFFERS + 256, 0])
        m.block('filter', 2)
        self.assertEqual(m.probe('filter-6', 2), [0])
        m.block('filter', 2, 1 << 2)
        self.assertEqual(m.probe('filter-6', 2), [1])                  # RESET on a trigger edge
        m.block('filter', 3)
        self.assertEqual(m.probe('filter-7', 0, 7)[6], self.state(m, 'filter-7', 3, 8))
        # The stock filter's internal type is not zero: the stock path.
        before = m.probe('filter-6', 9)
        m.halfword(TYPE['filter'] + 2, 2)
        self.assertEqual(m.block('filter', 2), [0.25] * 32)
        self.assertEqual(m.probe('filter-6', 9), before)

    def test_an_overrun_mutes_only_that_machine(self):
        for kind, over, other, mid in (('voice', 'over-voice', 'voice-5', 5), ('filter', 'over-filter', 'filter-6', 6)):
            with self.subTest(kind=kind):
                m = self.machine(over, other)
                guard = PROFILE[kind + '_stack'][0]
                m.halfword(TYPE[kind] + 0, 30)
                m.halfword(TYPE[kind] + 1, mid)
                self.assertEqual(m.block(kind, 0), [0.0] * 32)          # the overrunning call is silenced
                self.assertEqual(m.word(guard), PROFILE['stack_guard'])  # and the guard rewritten
                self.assertEqual(m.block(kind, 0), [0.0] * 32)          # muted: not called again
                self.assertEqual(m.block(kind, 1)[0], 1.0)              # the kind's other machine runs
                self.assertEqual(m.probe(other, 9), [1])


if __name__ == '__main__':
    unittest.main()
