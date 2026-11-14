"""Assemble tone.asm with the SDK's DSP assembler and write tone_dsp.h: the words, the entry and the
words that hold an address in the code, for ot_dsp_load (octatrack/dspload.h).
usage: ELEKLOADER_DSP_ASM=... python3 gen.py"""
import os, sys, tempfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..'))
from elekloader.sdk import build
words, labels, rels = build.dsp_assemble(os.path.join(HERE, 'tone.asm'), tempfile.mkdtemp())
for i, off in rels:
    words[i] = off                      # the address as an offset: ot_dsp_load adds the home
out = ['/* SPDX-License-Identifier: GPL-2.0-or-later */',
       '/* tone.asm, assembled by gen.py (elekloader.sdk.build.dsp_assemble): do not edit. */',
       'static const uint32_t tone_words[] = {%s};' % ', '.join('0x%06x' % w for w in words),
       'static const uint16_t tone_rel[] = {%s};' % (', '.join(str(i) for i, _ in rels) or '0'),
       '#define TONE_N %d' % len(words), '#define TONE_ENTRY %d' % labels['tone'],
       '#define TONE_NREL %d' % len(rels), '']
open(os.path.join(HERE, 'tone_dsp.h'), 'w', newline='\n').write('\n'.join(out))
print(len(words), 'words,', len(rels), 'relocations, entry', labels['tone'])
