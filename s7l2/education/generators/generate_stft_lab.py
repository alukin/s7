#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original SP11 burst/chirp graph; analysis remains read-only."""
import json
import math
from generate_foundations import diagram_path, Drawing

MIT = 'https://sigproc.mit.edu/_static/fall25/lectures/Short-Time_Fourier_Transform.pdf'


def make():
    d = Drawing()
    d.element('tone', 'AnalogOscillator', dict(amplitude=1, frequency_hz=32, phase=math.pi/2),
              4, 28, 'Burst carrier: 1 V peak, 32 Hz')
    d.element('start', 'StepGenerator', dict(initial_value=0, final_value=1, start_time=1),
              4, 43, 'Open gate at source time 1 s')
    d.element('stop', 'StepGenerator', dict(initial_value=0, final_value=1, start_time=2),
              4, 58, 'Close gate: edit start_time 2 → 1.5 s')
    d.element('gate', 'Adder', dict(weights=[1, -1]), 21, 43, 'Gate = start minus stop')
    d.wire('start', 'gate');d.wire('stop', 'gate', 1)
    d.element('burst', 'Multiply', dict(factors=[1, 1]), 38, 28, 'Carrier while gate is open')
    d.wire('tone', 'burst');d.wire('gate', 'burst', 1)
    d.element('chirp', 'ChirpGenerator', dict(start_hz=8, end_hz=40, sweep_time=4,
              amplitude=.5, offset=0, phase=math.pi/2, sweep='linear', end_behavior='hold'),
              38, 58, 'Chirp: 0.5 V peak, 8 → 40 Hz')
    d.element('mixture', 'Adder', dict(weights=[1, 1]), 55, 43, 'Burst plus chirp')
    d.wire('burst', 'mixture');d.wire('chirp', 'mixture', 1)
    d.nodes.append(dict(id='scope', position=[72, 28], observer=True, type='Scope',
                        title='Time samples: compare burst, chirp and mixture', params=dict(
                        channel_count=3, channel_names=['burst', 'chirp', 'mixture'], channel_units=['V']*3)))
    for port, source in enumerate(['burst', 'chirp', 'mixture']): d.wire(source, 'scope', port, observer=True)
    for ident, source, x, y, title in [
        ('burst_map', 'burst', 55, 28, 'Burst STFT: inspect the window edges'),
        ('map', 'mixture', 72, 56, 'Mixture STFT: horizontal burst plus rising chirp')]:
        d.nodes.append(dict(id=ident, position=[x, y], observer=True, type='TimeFrequency',
                            title=title, params=dict(channel_units=['V'])))
        d.wire(source, ident, observer=True)
    notes = ('**SP11 — When did that frequency happen?**  \n'
             '**Predict → Run → Inspect → Change duration. Run 4 s at 128 Hz.** '
             'The 32 Hz burst occupies n=128…255; a quieter chirp rises from 8 toward 40 Hz.  \n'
             'Open a map. Select STFT, FFT size 64, Hop 32 samples, Magnitude. '
             'Expect 15 columns, Δf=2 Hz and 0.25 s between centers; the first center is 0.25390625 s.  \n'
             'Compare N=128 / Hop 64, then N=64 / Hop 16. Smaller hop adds columns, not sharper frequency resolution. '
             'Only full windows are analyzed; edge overlap spreads the apparent burst.  \n'
             'Select stop, F4: start_time 2 → 1.5 s. Predict the shorter burst before rerunning. '
             'Scope observes source sample n at model time (n+1)/128 s.  \n'
             '[Open SP11 student lab](qrc:/s7studio/help/education/labs/SP11-short-time-fourier/README.md)'
             f' · [MIT STFT lecture]({MIT})'
             ' · [S7 time–frequency guide](qrc:/s7studio/help/examples/time_frequency/README.md)')
    return dict(id='sp11_short_time_fourier', document_kind='s7.diagram',
                dt=1/128, studio_run=dict(runtime_seconds=4, clear_scope_traces=True),
                sheet=dict(orientation='landscape', width_mm=470, height_mm=390),
                schema=dict(name='SP11 — When did that frequency happen?', dt=1/128,
                            elements=d.elements, connections=d.connections), probes=[],
                diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667), nodes=d.nodes, wires=d.wires,
                             text_frames=[dict(id='notes', position=[2, 1], params=dict(
                                 text=notes, text_format='markdown', width=1670, height=500, font_size=20))]))


if __name__ == '__main__':
    diagram_path('sp11_short_time_fourier.s7diagram').write_text(json.dumps(make(), indent=2)+'\n')
