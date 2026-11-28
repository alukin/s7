#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Reproduce SP06's explicit three-tap FIR diagram (standard Python only)."""

import json
from generate_foundations import diagram_path


def make():
    elements, nodes, connections, wires = [], [], [], []

    def element(ident, kind, params, x, y, title):
        elements.append(dict(id=ident, type=kind, params=params))
        nodes.append(dict(id=ident, position=[x, y], title=title))

    def wire(source, target, port=0, observer=False, bends=()):
        if not observer:
            connections.append(dict(from_element=source, from_port=0,
                                    to_element=target, to_port=port))
        wires.append(dict(id=f'w{len(wires)}',
                          **{'from': {'port': [source, 'out', 0]},
                             'to': {'port': [target, 'in', port]}}, bends=list(bends)))

    element('dc', 'Constant', dict(value=.25), 3, 17, 'DC: 0.25 V')
    element('tone_low', 'AnalogOscillator', dict(amplitude=1, frequency_hz=10, phase=.2),
            3, 29, 'Retained: 10 Hz, 1 Vpk')
    element('tone_zero', 'AnalogOscillator', dict(amplitude=.5, frequency_hz=100, phase=.7),
            3, 41, 'Rejected: 100 Hz, 0.5 Vpk')
    element('source', 'Adder', dict(weights=[1, 1, 1]), 16, 29, 'x[n]: DC + two tones')
    element('delay1', 'Delay', dict(initial_value=0), 29, 41, 'x[n-1], initially zero')
    element('delay2', 'Delay', dict(initial_value=0), 42, 53, 'x[n-2], initially zero')
    element('tap0', 'Gain', dict(factor=1/3), 42, 17, 'h[0] = 1/3')
    element('tap1', 'Gain', dict(factor=1/3), 55, 29, 'h[1] = 1/3')
    element('tap2', 'Gain', dict(factor=1/3), 55, 53, 'h[2] = 1/3')
    element('filtered', 'Adder', dict(weights=[1, 1, 1]), 68, 29, 'y[n]: three-tap average')
    for i, ident in enumerate(('dc', 'tone_low', 'tone_zero')):
        wire(ident, 'source', i)
    wire('source', 'tap0', bends=([27, 31], [27, 19]))
    wire('source', 'delay1')
    wire('delay1', 'delay2')
    wire('delay1', 'tap1', bends=([40, 43], [40, 31]))
    wire('delay2', 'tap2')
    for i in range(3):
        wire(f'tap{i}', 'filtered', i)
    nodes.append(dict(id='scope', position=[81, 29], title='Input and filtered output',
                      observer=True, type='Scope', params=dict(channel_count=2,
                      channel_names=['input', 'filtered'], channel_units=['V', 'V'])))
    wire('source', 'scope', observer=True, bends=([27, 31], [27, 16], [78, 16], [78, 31]))
    wire('filtered', 'scope', 1, observer=True)
    notes = ('**SP06: three-tap FIR / moving average**  \n'
             '**Predict → Run → Inspect → Break → Explain.** Run 4 s at 300 Hz; double-click Scope.  \n'
             '`y[n]=(x[n]+x[n-1]+x[n-2])/3`, `x[n<0]=0`. Step k samples n=k−1 at time k/300 s.  \n'
             'Predict: DC stays 0.25 V; 10 Hz gain ≈ 0.98543, phase −12°; 100 Hz cancels.  \n'
             'Try: change tap1 factor to 0 and rerun. What happens to the mean and 100 Hz tone?  \n'
             '[Open student lab](qrc:/s7studio/help/education/labs/SP06-three-tap-fir/README.md)'
             ' · [MIT convolution lesson](https://sigproc.mit.edu/_static/fall26/lectures/Unit-Sample_Response_and_Convolution-handout.pdf)'
             ' · [MIT block diagrams](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_chap3/)'
             ' · Original S7 exercise, not an official MIT resource.')
    return dict(id='three_tap_fir', dt=1/300, document_kind='s7.diagram',
                sheet=dict(orientation='landscape', width_mm=500, height_mm=360),
                studio_run=dict(runtime_seconds=4, clear_scope_traces=True),
                schema=dict(name='three_tap_fir', dt=1/300, elements=elements, connections=connections),
                probes=[dict(name=name, element=ident, port=0, unit='V')
                        for name, ident in [('input', 'source'), ('filtered', 'filtered')]],
                diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667),
                             nodes=nodes, wires=wires, text_frames=[dict(id='notes', position=[2, 1],
                             params=dict(text=notes, text_format='markdown', width=1770, height=280, font_size=18))]))


if __name__ == '__main__':
    path = diagram_path('sp06_three_tap_fir.s7diagram')
    path.write_text(json.dumps(make(), indent=2) + '\n')
