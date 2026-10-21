#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0

"""Rebuild the nine small-angle double-pendulum integration examples."""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def make(method, mode):
    initial = {'slow': (0.1, math.sqrt(2) * 0.1),
               'fast': (0.1, -math.sqrt(2) * 0.1),
               'mixed': (0.2, 0.1)}[mode]
    name = f'double_pendulum_{mode}_{method}'
    elements, nodes, connections, wires = [], [], [], []

    def element(ident, kind, params, x, y, title):
        elements.append({'id': ident, 'type': kind, 'params': params})
        nodes.append({'id': ident, 'position': [x, y], 'title': title})

    def wire(source, destination, output=0, input_=0, observer=False):
        if not observer:
            connections.append(dict(from_element=source, from_port=output,
                                    to_element=destination, to_port=input_))
        wires.append({'id': f'w{len(wires) + 1}',
                      'from': {'port': [source, 'out', output]},
                      'to': {'port': [destination, 'in', input_]}, 'bends': []})

    state = []
    feedback = []
    for i, q0 in enumerate(initial, 1):
        row = 20 + (i - 1) * 24
        if method == 'symplectic':
            ident = f'state{i}'
            element(ident, 'SymplecticIntegrator',
                    {'dt': 0.001, 'initial_position': q0, 'initial_velocity': 0.0},
                    62, row, f'Pendulum {i}: position / velocity')
            state.extend([(ident, 0), (ident, 1)])
            feedback.append((ident, 0))
        else:
            kind = 'EulerIntegrator' if method == 'euler' else 'TrapezoidIntegrator'
            element(f'omega{i}', kind, {'dt': 0.001, 'initial_value': 0.0},
                    48, row, f'Angular velocity {i}')
            element(f'theta{i}', kind, {'dt': 0.001, 'initial_value': q0},
                    76, row, f'Angle {i}')
            state.extend([(f'theta{i}', 0), (f'omega{i}', 0)])
            wire(f'omega{i}', f'theta{i}')
            feedback.append((f'theta{i}', 0))

    for i, weights in enumerate(((-19.62, 9.81), (19.62, -19.62)), 1):
        row = 20 + (i - 1) * 24
        for j, weight in enumerate(weights, 1):
            ident = f'coefficient_{i}_{j}'
            element(ident, 'Gain', {'factor': weight}, 6 + (j - 1) * 14,
                    row, f'{weight:g} times angle {j}')
            wire(feedback[j - 1][0], ident, feedback[j - 1][1])
        element(f'acceleration{i}', 'Adder', {'weights': [1.0, 1.0]},
                34, row, f'Acceleration {i}')
        wire(f'coefficient_{i}_1', f'acceleration{i}', input_=0)
        wire(f'coefficient_{i}_2', f'acceleration{i}', input_=1)
        wire(f'acceleration{i}', f'state{i}' if method == 'symplectic' else f'omega{i}')

    names = ['theta1', 'omega1', 'theta2', 'omega2']
    units = ['rad', 'rad/s', 'rad', 'rad/s']
    nodes.append({'id': 'scope_states', 'position': [108, 29], 'observer': True,
                  'type': 'Scope', 'params': {'channel_count': 4,
                  'channel_names': [f'{n} ({u})' for n, u in zip(names, units)]}})
    for port, (source, output) in enumerate(state):
        wire(source, 'scope_states', output, port, observer=True)
    explanation = {
        'euler': 'Forward Euler on all four states; secular amplitude/energy growth is expected.',
        'symplectic': 'Velocity-first semi-implicit Euler; each acceleration uses both old angles.',
        'trapezoid': ('Implicit trapezoidal rule: z[n+1]=z[n]+h/2*(F(z[n])+F(z[n+1])). '
            'Coupled states are solved together; no artificial Delay. Second-order accuracy. '
            'Fixed-point iteration must converge; reduce h if the runtime reports failure.')}

    note = (f'SMALL-ANGLE DOUBLE PENDULUM / {mode.upper()} MODE / {method.upper()}\n'
            'm1=m2=l1=l2=1, g=9.81; theta1\'\'=-2g*theta1+g*theta2; '
            'theta2\'\'=2g*theta1-2g*theta2.\n'
            f'Initial angles: {initial[0]:.16g}, {initial[1]:.16g} rad; velocities zero. '
            'Default h=0.001 s. Run for 20 s at 1, 10 and 100 kHz.\n'
            + explanation[method] + '\n'
            'Normal frequencies: sqrt(g*(2-sqrt(2))), sqrt(g*(2+sqrt(2))). '
            'After step k, plotted state is z[k-1]; runtime timestamp is k*h. See README.md.')
    return {'id': name, 'dt': 0.001, 'document_kind': 's7.diagram', 'diagram_version': 2,
            'sheet': {'orientation': 'landscape', 'width_mm': 650.0, 'height_mm': 330.0},
            'schema': {'name': name, 'elements': elements, 'connections': connections},
            'probes': [{'name': n, 'element': e, 'port': p, 'unit': u}
                       for n, (e, p), u in zip(names, state, units)],
            'diagram': {'grid': {'step': 20, 'mm_per_step': 5.291666666666667},
                        'nodes': nodes, 'wires': wires,
                        'text_frames': [{'id': 'model_notes', 'position': [2, 2],
                            'params': {'text': note, 'width': 2100.0, 'height': 180.0,
                                       'font_size': 10}}]}}


if __name__ == '__main__':
    for method in ('euler', 'symplectic', 'trapezoid'):
        for mode in ('slow', 'fast', 'mixed'):
            diagram = make(method, mode)
            (ROOT / (diagram['id'] + '.s7diagram')).write_text(
                json.dumps(diagram, indent=2, ensure_ascii=False) + '\n')
