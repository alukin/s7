#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0

"""Reproduce the linear-response teaching diagrams (standard Python only)."""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
MODELS = ('rc_charge', 'rc_discharge', 'rlc_step', 'rlc_resonance', 'mass_spring_damper')


def make(model, method):
    name = f'{model}_{method}'
    elements, nodes, connections, wires, probes = [], [], [], [], []

    def element(ident, kind, params, x, y, title):
        elements.append(dict(id=ident, type=kind, params=params))
        nodes.append(dict(id=ident, position=[x, y], title=title))

    def wire(source, target, output=0, input_=0, observer=False):
        if not observer:
            connections.append(dict(from_element=source, from_port=output,
                                    to_element=target, to_port=input_))
        wires.append(dict(id=f'w{len(wires)+1}',
                          **{'from': {'port': [source, 'out', output]},
                             'to': {'port': [target, 'in', input_]}}, bends=[]))

    rc = model.startswith('rc_')
    mechanical = model == 'mass_spring_damper'
    initial = 1.0 if model in ('rc_discharge', 'mass_spring_damper') else 0.0
    drive = 0.0 if model in ('rc_discharge', 'mass_spring_damper') else 1.0
    if model == 'rlc_resonance':
        element('source', 'AnalogOscillator', dict(amplitude=1, frequency_hz=1/math.pi,
                sample_rate_hz=1000, phase=0), 3, 18, 'Applied voltage: 1 V sin(2t)')
    else:
        element('source', 'Constant', dict(value=drive), 3, 18,
                'Applied force (N)' if mechanical else 'Applied voltage (V)')
    if rc:
        if method != 'symplectic':
            element('state', 'GaussLegendreIntegrator' if method == 'gauss' else 'EulerIntegrator',
                    dict(dt=.001, initial_value=initial),
                    43, 18, 'Capacitor voltage (V)')
            state = ('state', 0)
        else:
            element('state', 'SymplecticIntegrator', dict(dt=.001,
                    initial_position=0, initial_velocity=initial), 43, 18,
                    'out 1: voltage (V); out 0: auxiliary integral (V s)')
            state = ('state', 1)
        element('derivative', 'Adder', dict(weights=[1, -1]), 23, 18,
                "Voltage derivative: (Vs - Vc)/RC; RC = 1 s")
        wire('source', 'derivative')
        wire(state[0], 'derivative', output=state[1], input_=1)
        wire('derivative', 'state')
        outputs = [('voltage', state, 'V')]
    else:
        if method == 'symplectic':
            element('state', 'SymplecticIntegrator', dict(dt=.001,
                    initial_position=initial, initial_velocity=0), 47, 18,
                    'Position / velocity' if mechanical else 'Charge / current')
            position, velocity, target = ('state', 0), ('state', 1), 'state'
        else:
            kind = 'GaussLegendreIntegrator' if method == 'gauss' else 'EulerIntegrator'
            element('velocity', kind, dict(dt=.001, initial_value=0),
                    37, 18, 'Velocity (m/s)' if mechanical else 'Current (A)')
            element('position', kind, dict(dt=.001, initial_value=initial),
                    51, 18, 'Position (m)' if mechanical else 'Charge (C)')
            wire('velocity', 'position')
            position, velocity, target = ('position', 0), ('velocity', 0), 'velocity'
        element('derivative', 'Adder', dict(weights=[1, -1, -4]), 21, 18,
                "x'' = F - x' - 4x" if mechanical else "q'' = Vs - q' - 4q")
        wire('source', 'derivative')
        wire(velocity[0], 'derivative', velocity[1], 1)
        wire(position[0], 'derivative', position[1], 2)
        wire('derivative', target)
        if mechanical:
            outputs = [('position', position, 'm'), ('velocity', velocity, 'm/s')]
        else:
            element('capacitor_voltage', 'Gain', dict(factor=4), 65, 12, 'Vc = q/C (V)')
            wire(position[0], 'capacitor_voltage', position[1])
            outputs = [('charge', position, 'C'), ('current', velocity, 'A'),
                       ('capacitor_voltage', ('capacitor_voltage', 0), 'V')]
    outputs.append(('input', ('source', 0), 'N' if mechanical else 'V'))
    nodes.append(dict(id='scope', position=[78, 20], observer=True, type='Scope',
                      params=dict(channel_count=len(outputs), channel_names=[o[0] for o in outputs])))
    for i, (label, (ident, port), unit) in enumerate(outputs):
        probes.append(dict(name=label, element=ident, port=port, unit=unit))
        wire(ident, 'scope', port, i, True)
    notes = (f'{model.upper()} / {method.upper()}\n'
             'Default dt=0.001 s (1 kHz); run 10 s, resonance 30 s. Open Scope.\n'
             'Equations, units, experiments and limits: examples/linear/README.md.\n'
             'State at runtime step k represents (k-1)*dt; Scope labels k*dt.\n'
             'Experimental teaching model; manual Studio workflow not yet qualified.')
    return dict(id=name, dt=.001, document_kind='s7.diagram', diagram_version=2,
                sheet=dict(orientation='landscape', width_mm=490, height_mm=210),
                schema=dict(name=name, elements=elements, connections=connections), probes=probes,
                diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667),
                             nodes=nodes, wires=wires, text_frames=[dict(id='notes', position=[2, 2],
                             params=dict(text=notes, width=1650, height=200, font_size=10))]))


if __name__ == '__main__':
    for model in MODELS:
        for method in ('euler', 'symplectic'):
            diagram = make(model, method)
            (ROOT / f'{diagram["id"]}.s7diagram').write_text(json.dumps(diagram, indent=2) + '\n')
    for model in ('rc_charge', 'rc_discharge', 'rlc_step'):
        diagram = make(model, 'gauss')
        (ROOT / f'{diagram["id"]}.s7diagram').write_text(json.dumps(diagram, indent=2) + '\n')
