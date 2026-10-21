#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Reproduce explicit Van der Pol, Lorenz and Lotka–Volterra teaching graphs."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Diagram:
    def __init__(self, folder, name, dt, duration, description):
        self.folder, self.name, self.dt = folder, name, dt
        self.elements, self.connections, self.nodes, self.wires, self.probes = [], [], [], [], []
        self.notes = (f'{name.upper()}\n{description}\n'
                      f'Run {duration} model seconds at {1/dt:g} Hz. Double-click Scope and phase viewers.\n'
                      f'Equations, experiments and numerical limits: examples/{folder}/README.md.\n'
                      'State at step k represents (k-1)*dt; observation label is k*dt.\n'
                      'Experimental teaching model; manual Studio workflow remains unqualified.')

    def element(self, ident, kind, params, x, y, title=None):
        self.elements.append(dict(id=ident, type=kind, params=params))
        self.nodes.append(dict(id=ident, position=[x, y], title=title or ident))

    def wire(self, source, target, output=0, input_=0, observer=False):
        if not observer:
            self.connections.append(dict(from_element=source, from_port=output,
                                         to_element=target, to_port=input_))
        self.wires.append(dict(id=f'w{len(self.wires)+1}',
                               **{'from': {'port': [source, 'out', output]},
                                  'to': {'port': [target, 'in', input_]}}, bends=[]))

    def viewer(self, ident, kind, signals, x, y):
        params = dict(channel_count=len(signals), channel_names=[s[0] for s in signals]) if kind == 'Scope' else {}
        self.nodes.append(dict(id=ident, position=[x, y], observer=True, type=kind, params=params))
        for i, (label, source, port) in enumerate(signals):
            self.wire(source, ident, port, i, True)
            probe = dict(name=label, element=source, port=port, unit='1')
            if probe not in self.probes:
                self.probes.append(probe)

    def save(self):
        document = dict(id=self.name, dt=self.dt, document_kind='s7.diagram', diagram_version=2,
                        sheet=dict(orientation='landscape', width_mm=1000, height_mm=600),
                        schema=dict(name=self.name, dt=self.dt, elements=self.elements,
                                    connections=self.connections), probes=self.probes,
                        diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667),
                                     nodes=self.nodes, wires=self.wires,
                                     text_frames=[dict(id='notes', position=[2, 2], params=dict(
                                         text=self.notes, width=3000, height=200, font_size=10))]))
        folder = ROOT / 'examples' / self.folder
        folder.mkdir(parents=True, exist_ok=True)
        (folder / f'{self.name}.s7diagram').write_text(json.dumps(document, indent=2)+'\n')


def van_der_pol(mu, method):
    name = f'van_der_pol_mu{mu}_{method}'
    d = Diagram('van_der_pol', name, .001, 60 if mu == 3 else 40,
                f"x'' = {mu}*(1-x*x)*v - x; x(0) = 0.1, 2, 4; v(0) = 0.")
    for label, initial, row in [('small', .1, 20), ('cycle', 2, 50), ('large', 4, 80)]:
        def add(ident, kind, params, x, y=0, title=None):
            d.element(label+'_'+ident, kind, params, x, row+y, title)
        def wire(source, target, output=0, input_=0):
            d.wire(label+'_'+source, label+'_'+target, output, input_)
        if method == 'symplectic':
            add('state', 'SymplecticIntegrator', dict(dt=d.dt, initial_position=initial, initial_velocity=0),
                102, title='Euler-Cromer x / v')
            position, velocity = (label+'_state', 0), (label+'_state', 1)
            target = 'state'
        else:
            add('v', 'EulerIntegrator', dict(dt=d.dt, initial_value=0), 88, title='Velocity v')
            add('x', 'EulerIntegrator', dict(dt=d.dt, initial_value=initial), 110, title='Position x')
            wire('v', 'x')
            position, velocity, target = (label+'_x', 0), (label+'_v', 0), 'v'
        add('one', 'Constant', dict(value=1), 3, title='1')
        add('square', 'Multiply', {}, 24, 10, 'x squared')
        add('damping', 'Adder', dict(weights=[mu, -mu]), 44, title='mu*(1-x*x)')
        add('drive', 'Multiply', {}, 64, title='mu*(1-x*x)*v')
        add('acceleration', 'Adder', dict(weights=[1, -1]), 84, 12, 'Nonlinear drive - x')
        for port in (0, 1):
            d.wire(position[0], label+'_square', position[1], port)
        wire('one', 'damping')
        wire('square', 'damping', input_=1)
        wire('damping', 'drive')
        d.wire(velocity[0], label+'_drive', velocity[1], 1)
        wire('drive', 'acceleration')
        d.wire(position[0], label+'_acceleration', position[1], 1)
        wire('acceleration', target)
        signals = [(label+'_x', *position), (label+'_v', *velocity)]
        d.viewer(label+'_scope', 'Scope', signals, 140, row)
        d.viewer(label+'_phase', 'PhasePortrait', signals, 140, row+12)
    d.save()


def lorenz(rho):
    name = 'lorenz_attractor' if rho == 28 else 'lorenz_stable'
    d = Diagram('lorenz', name, .00025, 40 if rho == 28 else 10,
                f'sigma=10, rho={rho}, beta=8/3; initial (1,1,1) and (1.000001,1,1).')
    for label, initial_x, row in [('base', 1, 20), ('nearby', 1.000001, 60)]:
        def add(ident, kind, params, x, y=0, title=None):
            d.element(label+'_'+ident, kind, params, x, row+y, title)
        def wire(source, target, input_=0):
            d.wire(label+'_'+source, label+'_'+target, input_=input_)
        for ident, initial, y in [('x', initial_x, 0), ('y', 1, 12), ('z', 1, 24)]:
            add(ident, 'EulerIntegrator', dict(dt=d.dt, initial_value=initial), 96, y, ident)
        add('dx', 'Adder', dict(weights=[10, -10]), 22, title="x' = 10*(y-x)")
        wire('y', 'dx')
        wire('x', 'dx', 1)
        wire('dx', 'x')
        add('xz', 'Multiply', {}, 22, 12, 'x*z')
        wire('x', 'xz')
        wire('z', 'xz', 1)
        add('dy', 'Adder', dict(weights=[rho, -1, -1]), 60, 12, "y' = rho*x - y - x*z")
        wire('x', 'dy')
        wire('y', 'dy', 1)
        wire('xz', 'dy', 2)
        wire('dy', 'y')
        add('xy', 'Multiply', {}, 22, 24, 'x*y')
        wire('x', 'xy')
        wire('y', 'xy', 1)
        add('dz', 'Adder', dict(weights=[1, -8/3]), 60, 24, "z' = x*y - (8/3)*z")
        wire('xy', 'dz')
        wire('z', 'dz', 1)
        wire('dz', 'z')
        signals = [(label+'_'+axis, label+'_'+axis, 0) for axis in ('x', 'y', 'z')]
        d.viewer(label+'_scope', 'Scope', signals, 124, row)
        d.viewer(label+'_trajectory', 'PhaseTrajectory3D', signals, 146, row+12)
    differences = []
    for i, axis in enumerate(('x', 'y', 'z')):
        d.element('delta_'+axis, 'Adder', dict(weights=[1, -1]), 166, 26+20*i,
                  'nearby '+axis+' - base '+axis)
        d.wire('nearby_'+axis, 'delta_'+axis)
        d.wire('base_'+axis, 'delta_'+axis, input_=1)
        differences.append(('delta_'+axis, 'delta_'+axis, 0))
    d.viewer('difference_scope', 'Scope', differences, 166, 90)
    d.save()


def lotka_volterra(name, alpha, prey, predator):
    d = Diagram('lotka_volterra', name, .00025 if alpha == 1.5 else .001, 40,
                f"prey'={alpha}*prey-0.5*prey*predator; predator'=0.5*prey*predator-predator; initial ({prey},{predator}).")
    d.element('prey', 'EulerIntegrator', dict(dt=d.dt, initial_value=prey), 80, 20, 'Prey population (scaled)')
    d.element('predator', 'EulerIntegrator', dict(dt=d.dt, initial_value=predator), 80, 50, 'Predator population (scaled)')
    d.element('interaction', 'Multiply', {}, 15, 36, 'Prey * predator')
    d.element('prey_rate', 'Adder', dict(weights=[alpha, -.5]), 48, 20, 'Births - predation')
    d.element('predator_rate', 'Adder', dict(weights=[.5, -1]), 48, 50, 'Predation growth - deaths')
    for source, target, port in [('prey', 'interaction', 0), ('predator', 'interaction', 1),
            ('prey', 'prey_rate', 0), ('interaction', 'prey_rate', 1),
            ('interaction', 'predator_rate', 0), ('predator', 'predator_rate', 1),
            ('prey_rate', 'prey', 0), ('predator_rate', 'predator', 0)]:
        d.wire(source, target, input_=port)
    signals = [('prey', 'prey', 0), ('predator', 'predator', 0)]
    d.viewer('population_scope', 'Scope', signals, 110, 20)
    d.viewer('population_phase', 'PhasePortrait', signals, 110, 50)
    d.save()


if __name__ == '__main__':
    van_der_pol(1, 'euler')
    van_der_pol(1, 'symplectic')
    van_der_pol(3, 'symplectic')
    lorenz(28)
    lorenz(10)
    lotka_volterra('predator_prey', 1, 4, 1)
    lotka_volterra('predator_prey_near_equilibrium', 1, 2.2, 2)
    lotka_volterra('predator_prey_equilibrium', 1, 2, 2)
    lotka_volterra('predator_prey_faster_growth', 1.5, 4, 1)
