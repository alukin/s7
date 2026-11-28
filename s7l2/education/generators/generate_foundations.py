#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Create new FN01–FN03 teaching graphs; standard Python only.

Shared construction helpers describe explicit blocks, not a simulation solver.
Each lab has its own saved graph, initial-state contract and student experiment.
"""
import json
from pathlib import Path

def diagram_path(filename):
    """Write each canonical graph beside its lab guide."""
    labs = Path(__file__).resolve().parents[1] / 'labs'
    lab_id = filename.split('_', 1)[0].upper()
    folders = sorted(labs.glob(lab_id + '-*'))
    if len(folders) != 1 or not (folders[0] / 'README.md').is_file():
        raise ValueError(f'Expected one student guide for {lab_id}')
    return folders[0] / filename


MIT = 'https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/'
LABS = {
    'FN01': ('fn01_unit_delay', 'FN01-unit-delay', 'One sample of memory'),
    'FN02': ('fn02_natural_forced', 'FN02-natural-forced', 'Three ways to excite a system'),
    'FN03': ('fn03_initial_conditions', 'FN03-initial-conditions', 'Where does the response come from?'),
}


class Drawing:
    def __init__(self):
        self.elements, self.nodes, self.connections, self.wires = [], [], [], []

    def element(self, ident, kind, params, x, y, title):
        self.elements.append(dict(id=ident, type=kind, params=params))
        self.nodes.append(dict(id=ident, position=[x, y], title=title))

    def wire(self, source, target, port=0, *, observer=False, bends=()):
        if not observer:
            self.connections.append(dict(from_element=source, from_port=0,
                                         to_element=target, to_port=port))
        self.wires.append(dict(id=f'w{len(self.wires)}',
                               **{'from': {'port': [source, 'out', 0]},
                                  'to': {'port': [target, 'in', port]}}, bends=list(bends)))

    def recurrence(self, prefix, source, initial, y, label):
        """Output=sum(current input, gain(previous output)); explicit loop memory."""
        self.element(prefix, 'Adder', dict(weights=[1, 1]), 35, y, label)
        self.element(prefix+'_memory', 'Delay', dict(initial_value=initial),
                     35, y+9, f'Past output: starts at {initial:g} V')
        self.element(prefix+'_gain', 'Gain', dict(factor=.5), 20, y+9, 'Remember half: a = 0.5')
        self.wire(source, prefix)
        self.wire(prefix, prefix+'_memory', bends=([46, y+2], [46, y+11]))
        self.wire(prefix+'_memory', prefix+'_gain', bends=([46, y+11], [46, y+16],
                                                        [18, y+16], [18, y+11]))
        self.wire(prefix+'_gain', prefix, 1, bends=([31, y+11], [31, y+4]))

    def scope(self, channels, x, y):
        self.nodes.append(dict(id='scope', position=[x, y], observer=True, type='Scope',
                               title='Compare the named signals', params=dict(
                                   channel_count=len(channels), channel_names=[name for name, _ in channels],
                                   channel_units=['V']*len(channels))))
        for port, (_, source) in enumerate(channels):
            self.wire(source, 'scope', port, observer=True)


def make(lab):
    ident, slug, title = LABS[lab]
    d = Drawing()
    if lab == 'FN01':
        d.element('input', 'Constant', dict(value=1), 4, 20, 'Input u[n] = 1 V')
        d.recurrence('output', 'input', 0, 20, 'Output y[n]')
        d.scope([('input', 'input'), ('output', 'output'), ('previous', 'output_memory')], 52, 20)
        prediction = 'Predict output: 1, 1.5, 1.75, 1.875 V. The previous channel is one sample behind.'
        experiment = 'Try: select output_gain, press F4, change factor 0.5 to 1.25; rerun. Does it settle?'
        equation = '`y[n]=u[n]+0.5 y[n−1]`; stored past `y[−1]=0 V`.'
        chapters = [1, 3]
        height = 240
    elif lab == 'FN02':
        d.element('zero', 'Constant', dict(value=0), 4, 20, 'No input: 0 V')
        d.element('impulse', 'ImpulseGenerator', dict(area=.1, start_time=.5),
                  4, 40, 'One sample: 1 V at n=5')
        d.element('step', 'StepGenerator', dict(initial_value=0, final_value=1, start_time=.5),
                  4, 60, 'Sustained: 1 V from n=5')
        d.recurrence('natural', 'zero', 2, 20, 'Natural: stored past = 2 V')
        d.recurrence('impulse_response', 'impulse', 0, 40, 'Impulse response: zero past')
        d.recurrence('step_response', 'step', 0, 60, 'Step response: zero past')
        d.scope([('natural', 'natural'), ('impulse response', 'impulse_response'),
                 ('step response', 'step_response')], 52, 40)
        prediction = 'Before n=5 only natural response remains. At n=5 both forced responses start at 1 V.'
        experiment = 'Try: select step, press F4, change final_value 1 to 2; rerun. Which curve changes?'
        equation = 'Same rule: `y[n]=u[n]+0.5 y[n−1]`. Inputs begin at n=5 (Scope time 0.6 s).'
        chapters = [1, 3]
        height = 450
    else:
        d.element('step', 'StepGenerator', dict(initial_value=0, final_value=1, start_time=.5),
                  4, 20, 'Input: 1 V from n=5')
        d.element('zero', 'Constant', dict(value=0), 4, 40, 'No input: 0 V')
        d.recurrence('total', 'step', 2, 20, 'TOTAL: input + stored past')
        d.recurrence('natural', 'zero', 2, 40, 'NATURAL: stored past only')
        d.recurrence('forced', 'step', 0, 60, 'FORCED: input only')
        d.element('sum_parts', 'Adder', dict(weights=[1, 1]), 51, 57, 'Natural + forced')
        d.element('residual', 'Adder', dict(weights=[1, -1]), 65, 35, 'Check: total minus parts')
        d.wire('natural', 'sum_parts')
        d.wire('forced', 'sum_parts', 1)
        d.wire('total', 'residual')
        d.wire('sum_parts', 'residual', 1)
        d.scope([('total', 'total'), ('natural', 'natural'), ('forced', 'forced'),
                 ('residual', 'residual')], 80, 35)
        prediction = 'Predict: total = natural + forced at every sample; residual = 0 V.'
        experiment = 'Try: select total_memory, press F4, change initial_value 2 to 0; rerun. Is the residual zero?'
        equation = '`y[n]=u[n]+0.5 y[n−1]`. Total/natural start with `y[−1]=2 V`; forced starts with 0 V.'
        chapters = [1, 2]
        height = 450

    width = 500 if lab == 'FN03' else 355
    frame_width = 1780 if lab == 'FN03' else 1240
    font = 18 if lab == 'FN03' else 16
    second_reading = 'MIT block diagrams' if chapters[1] == 3 else 'MIT modularity'
    links = (f'[Open {lab} student lab](qrc:/s7studio/help/education/labs/{slug}/README.md)'
             f' · [MIT difference equations]({MIT}mit6_003f11_chap{chapters[0]}/)'
             f' · [{second_reading}]({MIT}mit6_003f11_chap{chapters[1]}/)')
    notes = (f'**{lab} — {title}**  \n'
             '**1 Predict → 2 Run → 3 Inspect → 4 Change one thing.** Run **2 s at 10 Hz**, then open Scope.  \n'
             f'{equation}  \n{prediction}  \n{experiment}  \n{links}')
    return dict(id=ident, description=f'Original S7 lab {lab}. Start with the yellow instructions and linked student guide.',
                dt=.1, studio_run=dict(runtime_seconds=2, clear_scope_traces=True),
                document_kind='s7.diagram',
                sheet=dict(orientation='landscape', width_mm=width, height_mm=height),
                schema=dict(name=f'{lab} — {title}', dt=.1, elements=d.elements, connections=d.connections),
                probes=[], diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667),
                                        nodes=d.nodes, wires=d.wires, text_frames=[dict(id='notes', position=[2, 1],
                                        params=dict(text=notes, text_format='markdown', width=frame_width,
                                                    height=360, font_size=font))]))


if __name__ == '__main__':
    for lab, (ident, _, _) in LABS.items():
        diagram_path(ident+'.s7diagram').write_text(json.dumps(make(lab), indent=2)+'\n')
