#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""New linear-systems lessons. Drawing helpers only; no numerical solver."""
import json
import math
from generate_foundations import diagram_path, Drawing

MIT = 'https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec{}/'
LABS = {
    'FN04': ('fn04_impulse_transfer', 'FN04-impulse-transfer', 'From one pulse to a transfer function', .05, 10),
    'FN05': ('fn05_integration_rules', 'FN05-integration-rules', 'Three rules for the same decay', .5, 6),
    'FN06': ('fn06_pole_maps', 'FN06-pole-maps', 'Stable does not mean accurate', 3, 30),
    'CT01': ('ct01_first_order', 'CT01-first-order', 'A capacitor takes time to charge', .1, 5),
    'CT02': ('ct02_damped_modes', 'CT02-damped-modes', 'How damping changes a spring', .01, 8),
    'CT03': ('ct03_sample_time', 'CT03-sample-time', 'Can a slow sample rate invent growth?', .5, 10),
}


def scope(d, channels, x, y, ident='scope', title='Compare the named signals'):
    d.nodes.append(dict(id=ident, position=[x, y], observer=True, type='Scope', title=title,
                        params=dict(channel_count=len(channels), channel_names=[c[0] for c in channels],
                                    channel_units=[c[2] for c in channels])))
    for port, (_, source, _) in enumerate(channels):
        d.wire(source, ident, port, observer=True)


def mode(d, name, pole, x, y, title):
    d.element(name, 'Delay', dict(initial_value=1), x, y, title)
    d.element(name+'_pole', 'Gain', dict(factor=pole), x+14, y+5, f'Next = {pole:.4g} × state')
    d.wire(name, name+'_pole', bends=([x+11, y+2], [x+11, y+7]))
    d.wire(name+'_pole', name, bends=([x+25, y+7], [x+25, y+10], [x-2, y+10], [x-2, y+2]))


def make(lab):
    ident, slug, title, h, duration = LABS[lab]
    d = Drawing()
    if lab == 'FN04':
        d.element('input', 'ImpulseGenerator', dict(area=h, start_time=.2), 3, 21, 'One 1 V sample at n=4')
        d.element('input_gain', 'Gain', dict(factor=.5), 17, 21, 'Input weight = 0.5')
        d.element('output', 'Adder', dict(weights=[1, 1]), 33, 21, 'Current output y[n]')
        d.element('memory', 'Delay', dict(initial_value=0), 33, 33, 'Previous y: starts at 0 V')
        d.element('feedback_gain', 'Gain', dict(factor=.5), 17, 33, 'Feedback weight = 0.5')
        for a, b, p in [('input', 'input_gain', 0), ('input_gain', 'output', 0),
                         ('output', 'memory', 0), ('memory', 'feedback_gain', 0), ('feedback_gain', 'output', 1)]:
            d.wire(a, b, p)
        scope(d, [('input', 'input', 'V'), ('output', 'output', 'V')], 51, 21)
        d.nodes.append(dict(id='response', position=[51, 36], observer=True, type='FrequencyResponse',
                            title='Frequency response', params=dict(channel_units=['V', 'V'])))
        d.wire('input', 'response', observer=True)
        d.wire('output', 'response', 1, observer=True)
        prediction = 'Pulse response: 0.5, 0.25, 0.125 V; DC gain 1, Nyquist gain 1/3.'
        experiment = 'Change feedback_gain factor 0.5 → 0.8; predict DC gain 2.5. Inspect Scope at 0–1 s.'
        question = 'Can one pulse predict the response at every frequency?'
        sheet = (355, 280)
    elif lab in ('FN05', 'FN06'):
        p = {'forward': 1-h, 'backward': 1/(1+h), 'trapezoid': (1-h/2)/(1+h/2)}
        for name, y in [('forward', 17), ('backward', 31), ('trapezoid', 45)]:
            mode(d, name, p[name], 7, y, name.capitalize()+' state: starts at 1 V')
        if lab == 'FN05':
            scope(d, [(name, name, 'V') for name in p], 49, 31)
            question = 'Which approximation follows exp(−t) most closely?'
            prediction = 'After one update: forward 0.5, backward 2/3, trapezoid 0.6 V; exact ≈ 0.60653 V.'
            experiment = 'At the saved rate, change forward_pole factor 0.5 → 0.75. Why is this no longer the stated method?'
            sheet = (355, 325)
        else:
            mode(d, 'exact', math.exp(-h), 36, 45, 'Exact sampled mode: exp(−3)')
            scope(d, [('forward', 'forward', 'V')], 49, 17, title='Forward Euler: growing error')
            scope(d, [(n, n, 'V') for n in ['backward', 'trapezoid', 'exact']], 53, 31,
                  ident='stable_scope', title='Stable methods: inspect first 12 s')
            question = 'Does a stable discrete pole guarantee a faithful transient?'
            prediction = 'Poles: forward −2, backward 0.25, trapezoid −0.2, exact exp(−3). Separate plots prevent hidden curves.'
            experiment = 'Change backward initial_value 1 → 2. Does the pole change, or only the amplitude?'
            sheet = (370, 325)
    elif lab == 'CT01':
        d.element('input', 'Constant', dict(value=1), 3, 22, 'Supply: 1 V')
        d.element('difference', 'Adder', dict(weights=[1, -1]), 17, 22, 'Supply minus capacitor voltage')
        d.element('tau', 'Constant', dict(value=1), 17, 35, 'Time constant RC: 1 s')
        d.element('derivative', 'Divider', {}, 33, 22, 'Divide by RC: derivative (V/s)')
        d.element('voltage', 'EulerIntegrator', dict(initial_value=0), 49, 22, 'Capacitor voltage: starts at 0 V')
        for a,b,p in [('input','difference',0),('voltage','difference',1),('difference','derivative',0),
                      ('tau','derivative',1),('derivative','voltage',0)]: d.wire(a,b,p)
        scope(d, [('input','input','V'), ('voltage','voltage','V')], 65, 22)
        question = 'What does a one-second time constant mean?'
        prediction = 'Physical voltage at t=1 s: 0.63212 V. Forward Euler at h=0.1 s: 0.65132 V.'
        experiment = 'Change tau value 1 → 2; rerun. Does charging get faster or slower? State time = Scope time minus h.'
        sheet = (420, 280)
    elif lab == 'CT02':
        d.element('position', 'EulerIntegrator', dict(initial_value=1), 7, 22, 'Displacement x: starts at 1 m')
        d.element('velocity', 'EulerIntegrator', dict(initial_value=0), 53, 22, 'Velocity v: starts at 0 m/s')
        d.element('spring', 'Gain', dict(factor=4), 7, 36, 'Stiffness / mass: 4 per s²')
        d.element('damping', 'Gain', dict(factor=1), 53, 36, 'Damping / mass: 1 per s')
        d.element('acceleration', 'Adder', dict(weights=[-1, -1]), 30, 36, 'Acceleration = −4x − v')
        for a,b,p in [('velocity','position',0),('position','spring',0),('velocity','damping',0),
                      ('spring','acceleration',0),('damping','acceleration',1),('acceleration','velocity',0)]: d.wire(a,b,p)
        scope(d, [('position','position','m')], 7, 52, title='Displacement (m)')
        scope(d, [('velocity','velocity','m/s')], 53, 52, ident='velocity_scope', title='Velocity (m/s)')
        question = 'How does damping change an oscillation?'
        prediction = 'm=1 kg, k=4 N/m, b=1 N·s/m. Initial x=1 m, v=0. Compare each signal in its own units.'
        experiment = 'Change damping factor 1 → 3; rerun. Expect less overshoot. The gain is b/m, not the damping ratio.'
        sheet = (390, 360)
    else:
        d.element('state', 'EulerIntegrator', dict(initial_value=1), 8, 23, 'Decaying state: starts at 1 V')
        d.element('derivative', 'Gain', dict(factor=-1), 27, 35, 'Derivative = −state / 1 s')
        d.wire('state', 'derivative'); d.wire('derivative', 'state')
        scope(d, [('state','state','V')], 48, 23)
        question = 'Can the sample interval change a correct model into an unstable calculation?'
        prediction = 'At 2 Hz: 1, 0.5, 0.25 V. Physical decay is always positive; Euler pole = 1−h.'
        experiment = 'Set Run Configuration frequency to 0.4 Hz, keep 10 s. Predict 1, −1.5, 2.25, −3.375 V.'
        sheet = (355, 280)

    frame_width = int(sheet[0]*96/25.4)-100
    notes = (f'**{lab} — {title}**  \n'
             f'**{question}** Run **{duration:g} s at {1/h:g} Hz**, then open Scope.  \n'
             f'{prediction}  \n{experiment}  \n'
             f'[Open {lab} student lab](qrc:/s7studio/help/education/labs/{slug}/README.md)'
             f' · [MIT teaching materials]({MIT.format("09" if lab == "FN04" else "04" if lab in ("CT01", "CT02") else "07")}) · [S7 integrator rules](qrc:/s7studio/help/docs/INTEGRATORS.md)')
    return dict(id=ident, dt=h, description=f'Original S7 lesson {lab}; see its linked student guide.',
                document_kind='s7.diagram', diagram_version=2,
                studio_run=dict(runtime_seconds=duration, clear_scope_traces=True),
                sheet=dict(orientation='landscape', width_mm=sheet[0], height_mm=sheet[1]),
                schema=dict(name=f'{lab} — {title}', dt=h, elements=d.elements, connections=d.connections),
                probes=[], diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667),
                                        nodes=d.nodes, wires=d.wires, text_frames=[dict(id='notes', position=[2,1],
                                        params=dict(text=notes, text_format='markdown', width=frame_width,
                                                    height=260, font_size=18))]))


if __name__ == '__main__':
    for lab, (ident, *_) in LABS.items():
        diagram_path(ident+'.s7diagram').write_text(json.dumps(make(lab), indent=2)+'\n')
