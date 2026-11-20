#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original SP01/SP03 graphs; reuse drawing helpers, never existing diagrams."""
import json
import math
from pathlib import Path
from generate_foundations import Drawing

LABS = {
    'SP01': ('sp01_fourier_synthesis', 'SP01-fourier-synthesis', 'Build a square wave from smooth tones', 128),
    'SP03': ('sp03_aliasing', 'SP03-aliasing', 'Three tones, one sampled sequence', 64),
}


def make(lab):
    ident, slug, title, rate = LABS[lab]
    d = Drawing()
    if lab == 'SP01':
        for m, y in zip(range(1, 10, 2), range(18, 59, 10)):
            d.element(f'tone{m}', 'AnalogOscillator', dict(amplitude=1, frequency_hz=2*m, phase=0),
                      3, y, f'Harmonic {m}: {2*m} Hz, unit sine')
            d.element(f'weight{m}', 'Gain', dict(factor=4/(math.pi*m)), 16, y, f'Coefficient: 4 / ({m}π)')
            d.wire(f'tone{m}', f'weight{m}')
        d.element('three', 'Adder', dict(weights=[1, 1, 1]), 32, 28, 'First three odd harmonics')
        d.element('five', 'Adder', dict(weights=[1]*5), 48, 43, 'Full sum: all five branches')
        for port,m in enumerate(range(1, 10, 2)):
            d.wire(f'weight{m}', 'five', port)
            if port < 3: d.wire(f'weight{m}', 'three', port)
        d.element('target', 'PulseGenerator', dict(low=-1, high=1, frequency_hz=2, phase=0, duty=.5),
                  32, 60, 'Square target: right-hand value at jumps')
        channels = [('one term','weight1'), ('three terms','three'), ('full sum','five'), ('square target','target')]
        question = 'Do more harmonics remove the ringing near a jump?'
        prediction = 'At state time 0.125 s: one 1.27324 V; three 1.10347 V; five 1.06305 V. At jumps every partial sum is 0.'
        edit = 'Change weight9 factor to 0. Which 18 Hz component disappears? Scope: inspect 0–0.5 s; see guide for Spectrum settings.'
        sheet, position, lecture = (440, 370), [66, 28], '15'
    else:
        for ident_, f, phase, y, title_ in [
            ('low',10,.375,18,'10 Hz: reference tone'),
            ('shifted',74,.375,32,'Shifted tone: edit frequency_hz'),
            ('reflected',54,math.pi-.375,46,'54 Hz: 64 − 10, reflected phase')]:
            d.element(ident_, 'AnalogOscillator', dict(amplitude=1, frequency_hz=f, phase=phase), 6,y,title_)
        d.element('residual', 'Adder', dict(weights=[-1,1]), 29,25,'Difference: shifted minus reference')
        d.wire('low','residual'); d.wire('shifted','residual',1)
        channels=[('reference','low'),('shifted','shifted'),('reflected','reflected'),('difference','residual')]
        question='Can a 10 Hz spectral peak identify the original tone?'
        prediction='At 64 Hz all three source sequences match; difference is zero. Overlay is intentional: hide channels to inspect each.'
        edit='Change shifted frequency_hz 74 → 70: predict a 6 Hz alias. Restore 74, then run at 256 Hz to distinguish the tones.'
        sheet, position, lecture=(355,310), [49,25], '21'
    d.nodes.append(dict(id='scope', position=position, observer=True, type='Scope', title='Compare the named signals',
                        params=dict(channel_count=len(channels), channel_names=[c[0] for c in channels], channel_units=['V']*len(channels))))
    for port,(_, source) in enumerate(channels): d.wire(source,'scope',port,observer=True)
    notes=(f'**{lab} — {title}**  \n**{question}** Run **2 s at {rate} Hz**; open Scope.  \n'
           f'{prediction}  \n{edit}  \n'
           f'[Open {lab} student lab](qrc:/s7studio/help/education/labs/{slug}/README.md)'
           f' · [MIT teaching materials](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec{lecture}/)'
           ' · [S7 signal sources](qrc:/s7studio/help/docs/SIGNAL_SOURCES.md)')
    return dict(id=ident, dt=1/rate, document_kind='s7.diagram', diagram_version=2,
                studio_run=dict(runtime_seconds=2,clear_scope_traces=True),
                sheet=dict(orientation='landscape',width_mm=sheet[0],height_mm=sheet[1]),
                schema=dict(name=f'{lab} — {title}',dt=1/rate,elements=d.elements,connections=d.connections),probes=[],
                diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
                             text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',
                                          width=int(sheet[0]*96/25.4)-100,height=260 if lab == 'SP01' else 320,font_size=18))]))


if __name__ == '__main__':
    for lab,(ident,*_) in LABS.items():
        Path(__file__).with_name(ident+'.s7diagram').write_text(json.dumps(make(lab),indent=2)+'\n')
