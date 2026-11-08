# SPDX-License-Identifier: GPL-2.0-or-later
"""Run an SDK machine's present and render on this computer, with no device or emulator.

    python -m elekloader.sdk.sdk_alpha_host <moddir> [--float] [--note 60] [--knob NAME=V ...]
                                            [--velocity V] [--track T] [--page P] [--retrigger N]
                                            [--blocks N] [--wav out.wav] [--glyphs]

It compiles the mod's C sources with the host compiler (cc, or $CC) and a small host main, prints what
present shows with each knob at its min, default and max, then renders the note and prints its peak and
frequency. --float builds the SHARC's float samples instead of the ColdFire's Q31.

The machine is found without reading the registration: every source is compiled with a prelude that
includes the SDK header and redefines ELEK_REGISTER_MACHINE(m) to export a pointer to m, so whatever
the real macro emits, the host build only sees the machine."""
import argparse
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from pathlib import Path

INCLUDE = Path(__file__).resolve().parent / 'include'
RATE = 48000

PRELUDE = r'''#include "elekloader/sdk_alpha_machine.h"
#undef ELEK_TARGET_SHARC    /* --float: the samples are float, and a mod's C render runs, not its SHARC one */
#undef ELEK_REGISTER_MACHINE
#define ELEK_REGISTER_MACHINE(m) const struct elek_machine *const elek_host_machine = &(m)
'''

# argv: "knobs" prints the declarations; "run pitch blocks velocity track page retrigger v0 v1 ..." prints
# present's views with each knob at its min, default and max (the others at v), then the rendered samples,
# -1.0..1.0, with start set every retrigger blocks (0: on the first only).
MAIN = r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elekloader/sdk_alpha_machine.h"

extern const struct elek_machine *const elek_host_machine;

static int32_t def(const struct elek_knob *k)
{
    return k->def < k->min ? k->min : k->def > k->max ? k->max : k->def;
}

static void present(const struct elek_machine *m, const int32_t *knob, int32_t page, uint32_t track,
                    uint32_t id, int32_t value)
{
    struct elek_view view[ELEK_UI_KNOB_MAX];
    int32_t values[ELEK_UI_KNOB_MAX];
    memcpy(values, knob, sizeof values);
    values[id] = value;
    for (uint32_t i = 0; i < m->knob_count; ++i) {
        memset(&view[i], 0, sizeof view[i]);
        strncpy(view[i].label, m->knobs[i].label, ELEK_UI_LABEL_MAX);
        view[i].glyph = m->knobs[i].glyph;
    }
    struct elek_ui ui = {.knob = values, .view = view, .page = page, .track = track};
    m->present(&ui);
    printf("present %u %d\n", id, value);
    for (uint32_t i = 0; i < m->knob_count; ++i) {
        printf("view %u %u ", i, view[i].hidden != 0);
        if (view[i].glyph)
            for (int r = 0; r < 16; ++r)
                printf("%04x", view[i].glyph->rows[r]);
        else
            printf("-");
        printf(" %.*s\n", ELEK_UI_LABEL_MAX, view[i].label);
    }
}

int main(int argc, char **argv)
{
    const struct elek_machine *m = elek_host_machine;
    if (argc < 2 || m->knob_count > ELEK_UI_KNOB_MAX)
        return 2;
    if (!strcmp(argv[1], "knobs")) {
        int pages = 0;
        while (pages < ELEK_UI_PAGE_MAX && m->pages[pages].label)
            ++pages;
        printf("machine %d %d %d %s\n", m->id, m->render != 0, pages, m->name);
        for (uint32_t i = 0; i < m->knob_count; ++i)
            printf("knob %d %d %d %s\n", m->knobs[i].min, def(&m->knobs[i]), m->knobs[i].max, m->knobs[i].label);
        return 0;
    }
    if (argc != 8 + (int)m->knob_count)
        return 2;
    int32_t knob[ELEK_UI_KNOB_MAX] = {0};
    for (uint32_t i = 0; i < m->knob_count; ++i)
        knob[i] = atoi(argv[8 + i]);
    int32_t page = atoi(argv[6]), retrigger = atoi(argv[7]);
    uint32_t track = (uint32_t)atoi(argv[5]);
    if (m->present)
        for (uint32_t i = 0; i < m->knob_count; ++i) {
            present(m, knob, page, track, i, m->knobs[i].min);
            present(m, knob, page, track, i, def(&m->knobs[i]));
            present(m, knob, page, track, i, m->knobs[i].max);
        }
    if (!m->render)
        return 0;
    static union { unsigned char bytes[ELEK_VOICE_STATE_MAX]; long double align; } state;
    struct elek_voice voice = {.knob = knob, .track = track, .pitch = atoi(argv[2]), .velocity = atoi(argv[4]),
                               .state = &state};
    elek_sample block[ELEK_VOICE_FRAMES];
    for (int b = 0, n = atoi(argv[3]); b < n; ++b) {
        voice.start = b == 0 || (retrigger && b % retrigger == 0);
        m->render(&voice, block);
        for (int i = 0; i < ELEK_VOICE_FRAMES; ++i)
#ifdef ELEK_TARGET_SHARC
            printf("%.9f\n", block[i]);
#else
            printf("%.9f\n", block[i] / 2147483648.0);
#endif
    }
    return 0;
}
'''


class Refused(Exception):
    pass


def compile_mod(moddir, out, sample_float=False):
    """The host program for the mod in moddir, written to out; Refused when it cannot be built."""
    cc = shutil.which(os.environ.get('CC', 'cc'))
    if not cc:
        raise Refused('no host C compiler: install one (Xcode command line tools, build-essential, MSYS2 gcc)'
                      ' or set CC')
    moddir = Path(moddir)
    try:
        sources = [moddir / s for s in json.loads((moddir / 'mod.json').read_text())['sources'] if s.endswith('.c')]
    except (OSError, ValueError, KeyError) as e:
        raise Refused('%s: cannot read its mod.json sources (%s)' % (moddir, e))
    machines = sum(len(re.findall(r'^\s*ELEK_REGISTER_MACHINE\s*\(', s.read_text(), re.M)) for s in sources)
    if machines != 1:
        raise Refused('%s: %d ELEK_REGISTER_MACHINE in its C sources; the host run takes exactly one'
                      % (moddir, machines))
    work = Path(out).parent
    (work / 'prelude.h').write_text(PRELUDE)
    (work / 'main.c').write_text(MAIN)
    flags = ['-std=c11', '-O1', '-I', str(INCLUDE)] + (['-DELEK_TARGET_SHARC'] if sample_float else [])
    objects = []
    for n, source in enumerate(sources):
        objects.append(str(work / ('%d.o' % n)))
        run([cc, *flags, '-include', str(work / 'prelude.h'), '-c', str(source), '-o', objects[-1]], source.name)
    run([cc, *flags, str(work / 'main.c'), *objects, '-o', str(out)], 'the host main')


def run(command, what):
    done = subprocess.run(command, capture_output=True, text=True)
    if done.returncode:
        raise Refused('%s did not compile on the host:\n%s' % (what, done.stderr.strip()))


def frequency(samples, rate=RATE):
    """Hz from the first to the last rising zero crossing, each placed between its two samples; None
    without two."""
    rises = [i + a / (a - b) for i, (a, b) in enumerate(zip(samples, samples[1:])) if a <= 0 < b]
    return rate * (len(rises) - 1) / (rises[-1] - rises[0]) if len(rises) > 1 else None


def show(view):
    hidden, glyph, label = view
    text = ''.join(c if ' ' <= c <= '~' else '?' for c in label)
    return '"%s"%s%s' % (text, ' hidden' if hidden else '', ' glyph' if glyph else '')


def draw(glyph):
    return ['    ' + ''.join('#' if int(glyph[r * 4:r * 4 + 4], 16) >> (15 - x) & 1 else '.' for x in range(16))
            for r in range(16)]


def host(moddir, sample_float=False, note=60, knobs=(), blocks=375, wav=None, glyphs=False,
         velocity=100, track=0, page=0, retrigger=0):
    """Print what the machine in moddir presents and renders; return its samples."""
    if not 0 <= note <= 127:
        raise Refused('--note %g: give a MIDI note 0..127, fractions allowed' % note)
    if not 1 <= velocity <= 127:
        raise Refused('--velocity %d: give 1..127' % velocity)
    if track < 0:
        raise Refused('--track %d: give a track from 0' % track)
    if retrigger < 0:
        raise Refused('--retrigger %d: give blocks between notes, from 1 (0: one held note)' % retrigger)
    with tempfile.TemporaryDirectory() as tmp:
        program = Path(tmp) / 'machine'
        compile_mod(moddir, program, sample_float)
        info = subprocess.run([str(program), 'knobs'], capture_output=True, text=True, check=True).stdout.splitlines()
        _, mid, has_render, pages, name = info[0].split(' ', 4)
        if page and not 0 <= page < int(pages):
            raise Refused('--page %d: %s has %s page(s), from 0' % (page, name, pages))
        decl = [line.split(' ', 4)[1:] for line in info[1:]]
        decl = [(label, int(lo), int(d), int(hi)) for lo, d, hi, label in decl]
        values = [d for _, _, d, _ in decl]
        for setting in knobs:
            label, _, value = setting.partition('=')
            ids = [i for i, k in enumerate(decl) if k[0].upper() == label.upper()]
            if not ids or not re.fullmatch(r'-?\d+', value):
                raise Refused('--knob %s: give NAME=V with NAME one of %s' % (setting, ', '.join(k[0] for k in decl)))
            lo, hi = decl[ids[0]][1], decl[ids[0]][3]
            if not lo <= int(value) <= hi:
                raise Refused('--knob %s: %s takes %d..%d' % (setting, decl[ids[0]][0], lo, hi))
            values[ids[0]] = int(value)
        args = round(note * 65536), blocks, velocity, track, page, retrigger, *values
        out = subprocess.run([str(program), 'run', *map(str, args)],
                             capture_output=True, text=True, errors='replace')
        if out.returncode:
            raise Refused('the machine stopped (exit %d):\n%s' % (out.returncode, out.stderr.strip()))
    print('%s (id %s), %s' % (name, mid, sample_float and 'float (SHARC)' or 'Q31 (ColdFire)'))
    for label, lo, d, hi in decl:
        print('  %-5s %d..%d, default %d' % (label, lo, hi, d))
    lines = out.stdout.splitlines()
    samples = [float(x) for x in lines if not x.startswith(('present ', 'view '))]
    presents = [line for line in lines if line.startswith(('present ', 'view '))]
    if presents:
        print('present on page %d, track %d, the other knobs at %s:' % (page, track, ', '.join('%s=%d' % (k[0], v) for k, v in zip(decl, values))))
    for line in presents:
        if line.startswith('present '):
            kid, value = map(int, line.split()[1:])
            views = []
            print('  %-11s' % ('%s=%d' % (decl[kid][0], value)), end='')
            continue
        _, vid, hidden, glyph, label = (line + ' ').split(' ', 4)
        views.append((int(hidden), glyph != '-' and glyph, label[:-1]))
        if len(views) == len(decl):
            print('  '.join('%s %s' % (decl[i][0], show(v)) for i, v in enumerate(views)))
            if glyphs:
                for i, v in enumerate(views):
                    if v[1]:
                        print('    %s:' % decl[i][0])
                        print('\n'.join(draw(v[1])))
    if has_render == '0':
        print('no render: the device plays its own sound')
        return samples
    hz = frequency(samples)
    print('render: note %g, velocity %d, track %d, %d blocks%s: peak %.3f, %s' % (
        note, velocity, track, blocks, retrigger and ', a new note every %d' % retrigger or '', max(map(abs, samples), default=0), 'about %.1f Hz' % hz if hz else 'no pitch found'))
    if wav:
        with wave.open(str(wav), 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(b''.join(struct.pack('<h', max(-32768, min(32767, round(s * 32768)))) for s in samples))
        print('wrote', wav)
    return samples


def main(argv=None):
    p = argparse.ArgumentParser(prog='python -m elekloader.sdk.sdk_alpha_host', description=__doc__.split('\n')[0])
    p.add_argument('moddir')
    p.add_argument('--float', action='store_true', help="the SHARC's float samples (default: the ColdFire's Q31)")
    p.add_argument('--note', type=float, default=60, help='MIDI note to render, fractions allowed (default 60)')
    p.add_argument('--knob', action='append', default=[], metavar='NAME=V',
                   help='a knob value other than its default, for present and render')
    p.add_argument('--velocity', type=int, default=100, help='the note velocity, 1..127 (default 100)')
    p.add_argument('--track', type=int, default=0, help='the track, from 0, for present and render (default 0)')
    p.add_argument('--page', type=int, default=0, help="the page present is shown, from 0 (default 0)")
    p.add_argument('--retrigger', type=int, default=0, metavar='N',
                   help='start a new note every N blocks (default: one held note)')
    p.add_argument('--blocks', type=int, default=375, help='render blocks of 32 samples (default 375: 0.25 s)')
    p.add_argument('--wav', help='write the render as a 48 kHz mono WAV')
    p.add_argument('--glyphs', action='store_true', help='draw each glyph present shows')
    a = p.parse_args(argv)
    try:
        host(a.moddir, a.float, a.note, a.knob, a.blocks, a.wav, a.glyphs, a.velocity, a.track, a.page,
             a.retrigger)
    except Refused as e:
        sys.exit('refused: %s' % e)


if __name__ == '__main__':
    main()
