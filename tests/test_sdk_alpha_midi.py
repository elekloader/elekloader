# SPDX-License-Identifier: GPL-2.0-or-later
"""MIDI CCs reach a machine's render in v->cc: a CC on the track's channel, once, at the voice's
next render, and one before the machine's first note at that note; the stock still applies it, so a
knob's CC still sets what the render plays, and .present shows it. Another track's CC does not reach
it, nor does one from before the track had the machine (test_sdk_alpha_render's adapter checks).

Run as tests/sdk_alpha_harness.py says.
"""
import unittest

from sdk_alpha_harness import sdk_machine, sdk_tests

FREE = 9                        # a CC no stock parameter uses, on either device

# CCS counts the CC 9s its render saw and keeps the last value, and LEVEL as it played, by track (the
# low three bits); LEVEL's label is its value. On the Digitone II its render is SHARC code (CCS_DN2).
CCS_DN2 = """
.SECTION/DM seg_dmda;
.GLOBAL cc_test_count.;
.GLOBAL cc_test_value.;
.GLOBAL cc_test_renders.;
.GLOBAL cc_test_level.;
.VAR cc_test_count. = 0;
.VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0;
.VAR cc_test_value. = 0;
.VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0;
.VAR cc_test_renders. = 0;
.VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0;
.VAR cc_test_level. = 0;
.VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0; .VAR = 0;

.SECTION/SW seg_swco;
.GLOBAL cc_test_render.;
cc_test_render.:
I0=R4;
R1=DM(1,I0);                    // track
R2=7;
R1=R1 AND R2;
R1=LSHIFT R1 BY 2;              // its word in each array
R2=cc_test_renders.;
R2=R2+R1;
I1=R2;
R0=DM(0,I1);
R0=R0+1;
DM(0,I1)=R0;
R0=DM(0,I0);                    // knob
I1=R0;
R0=DM(0,I1);                    // LEVEL
R2=cc_test_level.;
R2=R2+R1;
I1=R2;
DM(0,I1)=R0;
// cc[@FREE@]: the value, or -1.
R0=DM(7,I0);
I1=R0;
R0=DM(@FREE@,I1);
R0=PASS R0;
IF LT JUMP played;
R2=cc_test_value.;
R2=R2+R1;
I1=R2;
DM(0,I1)=R0;
R2=cc_test_count.;
R2=R2+R1;
I1=R2;
R0=DM(0,I1);
R0=R0+1;
DM(0,I1)=R0;
played:
I1=R8;
R0=0;
R1=32;
silence:
DM(I1,M6)=R0;
R1=R1-1;
IF NE JUMP silence;
I12=DM(M7,I6);
JUMP(M14,I12)(DB);
RFRAME;
NOP;
""".replace('@FREE@', str(FREE))

CCS = sdk_machine("Ccs", """
    #include "elekloader/sdk_alpha_machine.h"

    static const struct elek_knob knobs[] = { { "LEVEL", .max = 127 } };

    static void present(const struct elek_ui *ui)
    {
        ui->view[0].label = elek_num(ui, "L", ui->knob[0]);
    }

    #ifdef ELEK_TARGET_SHARC
    void cc_test_render(const struct elek_voice *v, elek_sample *out);
    #define RENDER cc_test_render
    #else
    volatile int32_t cc_test_count[8], cc_test_value[8], cc_test_renders[8], cc_test_level[8];

    static void render(const struct elek_voice *v, elek_sample *out)
    {
        int32_t value = v->cc[@FREE@];
        uint32_t t = v->track & 7;
        ++cc_test_renders[t];
        cc_test_level[t] = v->knob[0];
        if (value >= 0) {
            ++cc_test_count[t];
            cc_test_value[t] = value;
        }
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = 0;
    }
    #define RENDER render
    #endif

    static const struct elek_machine ccs = {
        .id = @ID@, .name = "Ccs", .short_name = "CCS", ELEK_KNOBS(knobs),
        .pages = { { "CCS", { &knobs[0] } } }, .present = present, .render = RENDER,
    };
    ELEK_REGISTER_MACHINE(ccs);
""", sharc={'ccs-dn2.s': CCS_DN2}, free=str(FREE))

# WIDE has fifteen knobs, so ids 8-14 are on its second page; its render keeps K8 and K14 as it played them.
WIDE = sdk_machine("Wide", """
    #include "elekloader/sdk_alpha_machine.h"

    #define K(n) { "K" #n, .max = 127 }
    static const struct elek_knob knobs[] = {
        K(0), K(1), K(2), K(3), K(4), K(5), K(6), K(7), K(8), K(9), K(10), K(11), K(12), K(13), K(14),
    };

    static void present(const struct elek_ui *ui)
    {
        ui->view[14].label = elek_num(ui, "L", ui->knob[14]);
    }

    volatile int32_t wide_test_k8[8], wide_test_k14[8];

    static void render(const struct elek_voice *v, elek_sample *out)
    {
        wide_test_k8[v->track & 7] = v->knob[8];
        wide_test_k14[v->track & 7] = v->knob[14];
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
            out[i] = 0;
    }

    static const struct elek_machine wide = {
        .id = @ID@, .name = "Wide", .short_name = "WIDE", ELEK_KNOBS(knobs),
        .pages = { { "ONE", { &knobs[0], &knobs[1], &knobs[2], &knobs[3],
                              &knobs[4], &knobs[5], &knobs[6], &knobs[7] } },
                   { "TWO", { &knobs[8], &knobs[9], &knobs[10], &knobs[11],
                              &knobs[12], &knobs[13], &knobs[14] } } },
        .present = present, .render = render,
    };
    ELEK_REGISTER_MACHINE(wide);
""")


@sdk_tests
class MidiCc:
    audio = True                    # the render runs from the audio
    requires = ('render', 'midi_cc')

    def hold(self, channel=1):
        """Hold a note on the track, played over MIDI, so its voice renders."""
        self.panel.note(channel, 60, 100, after_ms=100)
        self.addCleanup(self.panel.note, channel, 60, 0)

    def seen(self, track=0):
        return self.read('cc_test_count', track), self.read('cc_test_value', track)

    def test_a_cc_on_the_tracks_channel_reaches_its_voice_once(self):
        self.track.use(CCS)
        self.hold()
        self.assertGreater(self.read('cc_test_renders'), 0, 'the held note did not render')
        self.assertEqual(self.seen(), (0, 0))
        self.panel.cc(1, FREE, 64)
        self.assertEqual(self.seen(), (1, 64))
        self.panel.cc(1, FREE, 65)
        self.wait(100)
        self.assertEqual(self.seen(), (2, 65), 'one CC, one render that sees it')

    def test_a_cc_before_the_machines_first_note_reaches_that_note(self):
        self.track.use(CCS)
        self.panel.cc(1, FREE, 100)     # the track's voice plays the machine from its next note
        self.assertEqual(self.read('cc_test_renders'), 0)
        self.hold()
        self.assertEqual(self.seen(), (1, 100))

    def test_another_tracks_cc_does_not_reach_it(self):
        self.track.use(CCS)
        self.hold()
        self.panel.cc(2, FREE, 64)
        self.wait(100)
        self.assertEqual(self.seen(), (0, 0))

    def test_a_knobs_cc_still_sets_the_knob_and_present_shows_it(self):
        level, = self.track.use(CCS).knobs("LEVEL")
        self.hold()
        self.assertEqual(self.read('cc_test_level'), 0)
        self.panel.cc(1, self.panel.knob_cc(0), 100, after_ms=200)
        self.assertEqual(self.read('cc_test_level'), 100, 'the render does not play the CC')
        self.assertEqual(level.value, 100)
        self.assertEqual(level.label, 'L100')


@sdk_tests
class KnobCcsPastH:
    """Knob ids 8-14 take CC 24-30 on the Digitakt mk1 (machine-pages 1.10)."""
    audio = True
    requires = ('render', 'midi_cc', 'knob_ccs_past_h')

    def hold(self, channel=1):
        self.panel.note(channel, 60, 100, after_ms=100)
        self.addCleanup(self.panel.note, channel, 60, 0)

    def test_cc_24_to_30_set_knob_ids_8_to_14(self):
        knobs = self.track.use(WIDE).all_knobs()
        self.hold()
        for k in range(8, 15):
            self.assertEqual(self.panel.knob_cc(k), 16 + k)
            self.panel.cc(1, 16 + k, 10 + k, after_ms=200)
        self.assertEqual([knob.value for knob in knobs[8:]], [10 + k for k in range(8, 15)])
        self.assertEqual((self.read('wide_test_k8'), self.read('wide_test_k14')), (18, 24),
                         'the render does not play the CC')
        self.assertEqual(knobs[14].label, 'L24')

    def test_cc_16_to_23_still_set_knobs_a_to_h(self):
        knobs = self.track.use(WIDE).all_knobs()
        for k in range(8):
            self.panel.cc(1, 16 + k, 30 + k, after_ms=100)
        self.assertEqual([knob.value for knob in knobs], [30 + k for k in range(8)] + [0] * 7)

    def test_another_tracks_cc_does_not_set_it(self):
        k8 = self.track.use(WIDE).all_knobs()[8]
        self.panel.cc(2, 24, 99, after_ms=200)
        self.assertEqual(k8.value, 0)

    def test_a_machine_with_one_knob_answers_no_cc_past_23(self):
        level, = self.track.use(CCS).knobs("LEVEL")
        for cc in range(24, 31):
            self.panel.cc(1, cc, 99, after_ms=100)
        self.assertEqual(level.value, 0)
        self.assertEqual([self.panel.raw_value(k) for k in range(8, 15)], [0] * 7,
                         'a CC set an extra value the machine does not declare')


if __name__ == '__main__':
    unittest.main()
