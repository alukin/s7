#!/usr/bin/env python3
"""Generate the three large-amplitude single-pendulum benchmarks."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def make(method):
    name = f'nonlinear_pendulum_{method}'
    elements, nodes, connections, wires = [], [], [], []

    def element(ident, kind, params, x, y, title):
        elements.append({'id': ident, 'type': kind, 'params': params})
        nodes.append({'id': ident, 'position': [x, y], 'title': title})

    def wire(source, destination, output=0, input_=0, observer=False):
        if not observer:
            connections.append(dict(from_element=source, from_port=output,
                                    to_element=destination, to_port=input_))
        wires.append({'id': f'w{len(wires)+1}', 'from': {'port': [source, 'out', output]},
                      'to': {'port': [destination, 'in', input_]}, 'bends': []})

    if method == 'symplectic':
        element('state', 'SymplecticIntegrator',
                {'dt': 0.001, 'initial_position': 1.5, 'initial_velocity': 0.0},
                47, 18, 'Angle / angular velocity')
        theta, omega = ('state', 0), ('state', 1)
        acceleration_target = 'state'
    else:
        kind = 'EulerIntegrator' if method == 'euler' else 'TrapezoidIntegrator'
        element('omega', kind, {'dt': 0.001, 'initial_value': 0.0}, 33, 18, 'Angular velocity (rad/s)')
        element('theta', kind, {'dt': 0.001, 'initial_value': 1.5}, 47, 18, 'Angle (rad)')
        wire('omega', 'theta')
        theta, omega = ('theta', 0), ('omega', 0)
        acceleration_target = 'omega'
    element('sin_theta', 'Trig', {'function': 'sin'}, 5, 18, 'sin(angle)')
    element('gravity', 'Gain', {'factor': -9.81}, 19, 18, '-g/l = -9.81 per s^2')
    wire(theta[0], 'sin_theta', theta[1])
    wire('sin_theta', 'gravity')
    wire('gravity', acceleration_target)
    nodes.append({'id': 'scope_states', 'position': [65, 18], 'observer': True, 'type': 'Scope',
                  'params': {'channel_count': 2, 'channel_names': ['theta', 'omega']}})
    wire(theta[0], 'scope_states', theta[1], 0, True)
    wire(omega[0], 'scope_states', omega[1], 1, True)
    note = (f'NONLINEAR SINGLE PENDULUM / {method.upper()}\n'
            "theta'' = -9.81*sin(theta), theta(0)=1.5 rad, omega(0)=0 rad/s.\n"
            'Run Configuration: 100 model seconds at 1 and 10 kHz; default dt=0.001 s.\n'
            'E = omega^2/2 + 9.81*(1-cos(theta)); exact period from complete elliptic integral.\n'
            'Reference and predeclared quality budgets: examples/pendulum/README.md. '
            'Euler is a drift baseline, expected to FAIL the long-run accuracy budgets.\n'
            'Trapezoid solves the nonlinear endpoint jointly; reduce dt on nonconvergence.\n'
            'State after runtime step k is z[k-1], observation timestamp is k*dt.')
    return {'id': name, 'dt': 0.001, 'document_kind': 's7.diagram', 'diagram_version': 2,
            'sheet': {'orientation': 'landscape', 'width_mm': 420.0, 'height_mm': 210.0},
            'schema': {'name': name, 'elements': elements, 'connections': connections},
            'probes': [{'name': n, 'element': e, 'port': p, 'unit': u}
                       for n, (e, p), u in zip(('theta', 'omega'), (theta, omega), ('rad', 'rad/s'))],
            'diagram': {'grid': {'step': 20, 'mm_per_step': 5.291666666666667},
                        'nodes': nodes, 'wires': wires,
                        'text_frames': [{'id': 'model_notes', 'position': [2, 2],
                           'params': {'text': note, 'width': 1450.0, 'height': 200.0, 'font_size': 10}}]}}


if __name__ == '__main__':
    for method in ('euler', 'symplectic', 'trapezoid'):
        diagram = make(method)
        (ROOT/(diagram['id']+'.s7diagram')).write_text(json.dumps(diagram, indent=2) + '\n')
