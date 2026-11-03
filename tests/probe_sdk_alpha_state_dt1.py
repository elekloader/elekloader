# SPDX-License-Identifier: GPL-2.0-or-later
"""SDK state checks used by probe_sdk_alpha_lfo_dt1.py (DT1 1.54 only).

Calls the linked firmware's pattern-lock and kit converters, not project/card I/O.
The caller supplies the existing bounded native-call harness and Alpha LFO fixture.
"""
import struct


def sdk_alpha_probe_state(m, linked, native, put, label, kit, sound, slots):
    locks, packed_locks, restored_locks, resolved = 0x44000100, 0x44100100, 0x44200100, 0x44300100
    for address in (locks, packed_locks, restored_locks, resolved):
        m.ensure(address)
    m.load(bytes(111440), locks)
    m.load(bytes(111440), restored_locks)
    m.uc.mem_write(packed_locks, b'\xff' * 10400)
    assert native(0x4007ABB2, [locks, packed_locks]) & 255 == 1
    expected_locks = []
    for track in (0, 7):
        for logical, slot in enumerate(slots):
            value = 513 + logical * 1811 + track * 13
            for step in (0, 63):
                m.uc.mem_write(locks + track * 6965 + step * 108 + slot * 2, struct.pack('>h', value + step))
            m.uc.mem_write(locks + track * 6965 + 6912 + slot, b'\x01')
            expected_locks.append((track, logical, slot, value))
    assert native(0x4007ADB2, [packed_locks, locks]) & 255 == 1
    # Clear all runtime lock data before reading the saved record.
    m.uc.mem_write(locks, b'\x55' * 111440)
    assert native(0x4007ABB2, [restored_locks, packed_locks]) & 255 == 1
    for track, logical, slot, value in expected_locks:
        for step in (0, 63):
            assert struct.unpack('>h', m.uc.mem_read(restored_locks + track * 6965 + step * 108 + slot * 2, 2))[0] == value + step
    for track in (0, 7):
        native(0x40084EF6, [sound, 0, 126])
        m.uc.mem_write(sound + 56, struct.pack('>h', 32512))  # Hide logical zero.
        base = bytes(m.uc.mem_read(sound + 20, 106))
        # Resolver takes the track's lock base, independently of the engine track.
        native(0x4006F46E, [resolved, restored_locks + track * 6965, 0, 63])
        assert struct.unpack('>I', m.uc.mem_read(resolved + 8, 4))[0] == 15
        # Apply to engine zero, the same native path used by the existing backend probe.
        native(0x40077282, [sound, 0])
        native(0x40074B0A, [0, resolved, 0x800014F0])
        for t, logical, slot, value in expected_locks:
            if t == track:
                assert struct.unpack('>h', m.uc.mem_read(0x800014F0 + 18 + slot * 2, 2))[0] == value + 63
        native(0x40074A40, [0, 0x800014F0, sound])
        assert bytes(m.uc.mem_read(0x800014F0 + 18, 106)) == base

    # Erase live sound values before loading; presentation must be rebuilt from saved inputs.
    saved_kit, loaded_kit = 0x44400100, 0x44500100
    for address in (saved_kit, loaded_kit):
        m.ensure(address)

    def processed(s):
        m.uc.mem_write(0x421F4E10, bytes(640))
        native(0x40077282, [s, 0])
        native(0x4006F46E, [resolved, restored_locks, 0, 63])
        native(0x40074B0A, [0, resolved, 0x800014F0])
        put(0x800019B4, 0)
        m.uc.emu_start(0x40074732, 0x40074738, count=1)
        output = native(0x40074B9E, [0x800014F0])
        m.uc.emu_start(0x4006A868, 0x4006A86E, count=1)
        native(0x400ED766, [output, 14400, 255])
        return bytes(m.uc.mem_read(output + 18, 106))

    snapshots = []
    for controller in (0, 256, 32512, 0):
        for track in range(8):
            s = kit + 32 + 162 * track
            native(0x40084EF6, [s, 0, 126 if track in (0, 7) else 3])
            if track in (0, 7):
                for logical, slot in enumerate(slots):
                    m.uc.mem_write(s + 20 + 2 * slot, struct.pack('>h', 255 + logical * 1703 + track))
                m.uc.mem_write(s + 56, struct.pack('>h', controller if track == 0 else 0))
                for offset, slot in ((28, 17), (44, 52)):
                    m.uc.mem_write(s + offset, struct.pack('>h', slot << 8))
                for lfo in (0, 1):
                    for slot, value in ((1, 0x5000), (5, 2 << 8), (6, 0x4000),
                                        (7, 1 << 8), (8, 0x4800)):
                        m.uc.mem_write(s + 20 + 2 * (slot + 8 * lfo), struct.pack('>h', value))
        before = [bytes(m.uc.mem_read(kit + 32 + 162 * t + 20, 106)) for t in range(8)]
        output_before = processed(sound)
        for logical in (0, 14):
            slot = slots[logical]
            assert struct.unpack_from('>h', output_before, 2 * slot)[0] != 576 + logical * 1811
        assert native(0x4007A6A6, [saved_kit, kit, 0]) & 255 == 1
        m.uc.mem_write(kit, b'\x55' * 2334)
        m.uc.mem_write(loaded_kit, b'\xcc' * 2334)
        assert native(0x4007A4CE, [loaded_kit, saved_kit]) & 255 == 1
        put(linked.map['fw_kit'], loaded_kit)
        for t in (0, 7):
            assert bytes(m.uc.mem_read(loaded_kit + 32 + 162 * t + 20, 106)) == before[t]
        assert processed(loaded_kit + 32) == output_before
        put(linked.map['fw_active_track'], 0)
        assert label(132, 1) == ('QWE' if controller else 'XYZ')
        machine = native(linked.map['sdk_alpha_machine'], [126])
        assert native(linked.map['sdk_alpha_visible'], [machine, 0]) == int(controller != 32512)
        put(linked.map['fw_active_track'], 7)
        assert label(132, 1) == 'XYZ'
        put(linked.map['fw_active_track'], 0)
        snapshots.append({'controller': controller, 'label': label(132, 1), 'visible': controller != 32512})
        put(linked.map['fw_kit'], kit)
    return {'all_15_locks_roundtrip_on_first_last_tracks_and_steps': True,
            'all_15_locks_apply_and_restore_with_hidden_parameter': True,
            'kit_restores_parameter_inputs_and_both_lfo_destinations': True,
            'combined_locks_and_both_lfos_match_before_after_load': True,
            'presentation_after_load': snapshots}
