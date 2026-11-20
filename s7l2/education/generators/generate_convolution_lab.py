#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Construct the original SP10 scalar graph; no frame ports or solver."""
import json
from generate_foundations import diagram_path, Drawing

MIT = 'https://ocw.mit.edu/courses/res-6-008-digital-signal-processing-spring-2011/resources/lecture-10-circular-convolution/'


def make():
    d = Drawing()
    d.element('last_tap', 'Constant', dict(value=-.25), 69, 28,
              'Shared h[2]: edit value −0.25 → 0.5')
    for prefix, y, period in [('linear', 32, None), ('cyclic4', 56, 4), ('padded8', 80, 8)]:
        if period is None:
            d.element(prefix+'_pulse', 'ImpulseGenerator', dict(area=.125, start_time=1),
                      3, y, 'One unit sample at n=8')
        else:
            d.element(prefix+'_pulse', 'PulseGenerator',
                      dict(low=0, high=1, frequency_hz=8/period, duty=1/period, phase=0),
                      3, y, f'Unit samples every {period} ticks')
        for k in range(1, 4):
            d.element(f'{prefix}_x{k}', 'Delay', dict(initial_value=0), 3+12*k, y+9,
                      f'Word builder: delay {k}')
            d.wire(prefix+'_pulse' if k == 1 else f'{prefix}_x{k-1}', f'{prefix}_x{k}')
        d.element(prefix+'_input', 'Adder', dict(weights=[1, 2, -1, 1]), 43, y,
                  'Input word: 1, 2, −1, 1')
        d.wire(prefix+'_pulse', prefix+'_input')
        for k in range(1, 4):
            d.wire(f'{prefix}_x{k}', prefix+'_input', k)
        for k in (1, 2):
            d.element(f'{prefix}_d{k}', 'Delay', dict(initial_value=0), 57+12*(k-1), y+9,
                      f'FIR history: {k} sample'+('s' if k == 2 else ''))
            d.wire(prefix+'_input' if k == 1 else prefix+'_d1', f'{prefix}_d{k}')
        d.element(prefix+'_weighted', 'Multiply', dict(factors=[1, 1]), 83, y+9, 'Oldest input × shared h[2]')
        d.wire(prefix+'_d2', prefix+'_weighted');d.wire('last_tap', prefix+'_weighted', 1)
        d.element(prefix, 'Adder', dict(weights=[1, .5, 1]), 97, y,
                  {'linear': 'LINEAR: isolated word', 'cyclic4': 'CIRCULAR: period 4',
                   'padded8': 'PADDED: period 8'}[prefix])
        d.wire(prefix+'_input', prefix)
        d.wire(prefix+'_d1', prefix, 1)
        d.wire(prefix+'_weighted', prefix, 2)
    d.element('difference', 'Adder', dict(weights=[1, -1]), 111, 80,
              'Padded minus linear: read n=8…15')
    d.wire('padded8', 'difference');d.wire('linear', 'difference', 1)
    for ident, channels, x, y, title in [
        ('inputs', [('isolated input', 'linear_input'), ('period 4 input', 'cyclic4_input'),
                    ('period 8 input', 'padded8_input')], 111, 32, 'Inputs: compare the repetition'),
        ('scope', [('linear', 'linear'), ('circular 4', 'cyclic4'), ('padded 8', 'padded8'),
                   ('padded minus linear', 'difference')], 111, 56, 'Outputs: read the second block')]:
        d.nodes.append(dict(id=ident, position=[x, y], observer=True, type='Scope', title=title,
                            params=dict(channel_count=len(channels), channel_names=[a for a, _ in channels],
                                        channel_units=['V']*len(channels))))
        for port, (_, source) in enumerate(channels): d.wire(source, ident, port, observer=True)
    notes = ('**SP10 — Where does the tail go?**  \n'
             '**Predict → Run → Compare → Change one tap. Run 2 s at 8 Hz.** All Delay states start at zero.  \n'
             'Word x=[1, 2, −1, 1] V; h=[1, 0.5, −0.25]. First 8 ticks warm up the periodic paths. '
             'Read n=8…15 at Scope times **1.125…2 s**.  \n'
             'Linear: [1, 2.5, −0.25, 0, 0.75, −0.25, 0, 0]. Circular 4 wraps the tail: '
             '[1.75, 2.25, −0.25, 0], twice. Padded 8 matches linear in this block.  \n'
             'Select last_tap, F4: value −0.25 → 0.5. Predict all three outputs before rerunning. '
             'The input word builders are on the left; the same three-tap FIR is on each right-hand row.  \n'
             '[Open SP10 student lab](qrc:/s7studio/help/education/labs/SP10-circular-convolution/README.md)'
             f' · [MIT circular convolution]({MIT})'
             ' · [S7 convolution implementation](qrc:/s7studio/help/docs/CONVOLUTION.md)')
    return dict(id='sp10_circular_convolution', document_kind='s7.diagram', diagram_version=2,
                description='Original SP10 lab: linear/circular convolution, explicit padding and warm-up.',
                dt=.125, studio_run=dict(runtime_seconds=2, clear_scope_traces=True),
                sheet=dict(orientation='landscape', width_mm=660, height_mm=520),
                schema=dict(name='SP10 — Where does the tail go?', dt=.125,
                            elements=d.elements, connections=d.connections), probes=[],
                diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667), nodes=d.nodes, wires=d.wires,
                             text_frames=[dict(id='notes', position=[2, 1], params=dict(
                                 text=notes, text_format='markdown', width=2350, height=520, font_size=24))]))


if __name__ == '__main__':
    diagram_path('sp10_circular_convolution.s7diagram').write_text(json.dumps(make(), indent=2)+'\n')
