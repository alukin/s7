#!/usr/bin/env python3
"""Generate the control, buck and relay educational diagrams; standard Python."""
import copy
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESOURCE = ROOT / 's7core/resources/schema_elements.s7library'
COMPONENTS = {e['type_name']: e['schema'] for e in json.loads(RESOURCE.read_text())['elements']}


class Diagram:
    def __init__(self, name, dt, directory, duration, description):
        self.name, self.dt, self.directory = name, dt, directory
        self.elements, self.nodes, self.connections, self.wires, self.probes = [], [], [], [], []
        self.note = (f'{name.upper()}\n{description}\n'
                     f'Run {duration} s at {1/dt:g} Hz; open the scopes.\n'
                     f'Equations, experiments and limits: examples/{directory}/README.md.\n'
                     'Experimental: manual Studio workflow has not been qualified.')

    def element(self, ident, kind, params, x, y, title=None, schema=None):
        element = dict(id=ident, type=kind, params=params)
        if schema is not None:
            element['schema'] = schema
        self.elements.append(element)
        self.nodes.append(dict(id=ident, position=[x, y], title=title or ident))
        return ident

    def constant(self, ident, value, x, y):
        return self.element(ident, 'Constant', dict(value=value), x, y)

    def step(self, ident, initial, final, time, x, y):
        return self.element(ident, 'StepGenerator', dict(initial_value=initial,
                            final_value=final, sample_rate_hz=1/self.dt,
                            start_sample=round(time/self.dt)), x, y)

    def wire(self, source, target, output=0, input_=0, observer=False):
        if not observer:
            self.connections.append(dict(from_element=source, from_port=output,
                                         to_element=target, to_port=input_))
        self.wires.append(dict(id=f'w{len(self.wires)+1}',
                               **{'from': {'port': [source, 'out', output]},
                                  'to': {'port': [target, 'in', input_]}}, bends=[]))

    def scope(self, ident, signals, x, y):
        self.nodes.append(dict(id=ident, position=[x, y], observer=True, type='Scope',
                               params=dict(channel_count=len(signals),
                                           channel_names=[s[0] for s in signals])))
        for channel, (label, element, port, unit) in enumerate(signals):
            self.probes.append(dict(name=label, element=element, port=port, unit=unit))
            self.wire(element, ident, port, channel, True)

    def save(self):
        document = dict(id=self.name, dt=self.dt, document_kind='s7.diagram', diagram_version=2,
                        sheet=dict(orientation='landscape', width_mm=1000, height_mm=450),
                        schema=dict(name=self.name, dt=self.dt, elements=self.elements,
                                    connections=self.connections), probes=self.probes,
                        diagram=dict(grid=dict(step=20, mm_per_step=5.291666666666667),
                                     nodes=self.nodes, wires=self.wires,
                                     text_frames=[dict(id='notes', position=[2, 2],
                                         params=dict(text=self.note, width=2600, height=200, font_size=10))]))
        folder = ROOT / 'examples' / self.directory
        folder.mkdir(parents=True, exist_ok=True)
        (folder / (self.name + '.s7diagram')).write_text(json.dumps(document, indent=2) + '\n')


def controller_schema(kind, dt, tracking=10, lower=-1):
    schema = copy.deepcopy(COMPONENTS[kind])
    schema['dt'] = dt
    for element in schema['elements']:
        if element['id'] == 'i_rate':
            element['params']['weights'][1] = tracking
        if element['id'] == 'limit':
            element['params']['lower_limit'] = lower
        if 'dt' in element['params']:
            element['params']['dt'] = dt
    return schema


def control(plant, mode):
    d = Diagram(f'{plant}_{mode}', .001, 'control', 12,
                'Reference: 0 -> 0.6 at 0.5 s, 2 at 2 s, 0.6 at 4 s; load step at 6 s.')
    d.step('reference_step', 0, .6, .5, 3, 18)
    d.step('overload', 0, 1.4, 2, 3, 30)
    d.step('release', 0, -1.4, 4, 3, 42)
    d.element('reference', 'Adder', dict(weights=[1, 1, 1]), 24, 18)
    for i, ident in enumerate(('reference_step', 'overload', 'release')):
        d.wire(ident, 'reference', input_=i)
    d.step('load', 0, .1 if plant == 'dc_motor' else .2, 6, 84, 56)
    d.element('error', 'Adder', dict(weights=[1, -1]), 43, 18)
    d.wire('reference', 'error')
    if mode == 'open_loop':
        d.element('controller', 'Saturation', dict(lower_limit=-1, upper_limit=1), 64, 18,
                  'Open loop: clamp(reference)')
        d.wire('reference', 'controller')
        raw = ('reference', 0)
    elif mode == 'p':
        d.element('proportional', 'Gain', dict(factor=1.2), 58, 18)
        d.element('controller', 'Saturation', dict(lower_limit=-1, upper_limit=1), 74, 18)
        d.wire('error', 'proportional')
        d.wire('proportional', 'controller')
        raw = ('proportional', 0)
    else:
        kind = 'PIDController' if mode == 'pid' else 'PIController'
        d.element('controller', kind, {}, 64, 18, schema=controller_schema(
            kind, d.dt, tracking=0 if mode == 'pi_no_aw' else 10))
        d.constant('kp', 1.2, 44, 34)
        d.constant('ki', 2, 44, 44)
        if mode == 'pid':
            d.constant('kd', .02, 44, 54)
            d.constant('filter_tau', .03, 44, 64)
            d.wire('reference', 'controller')
            for i, ident in enumerate(('kp', 'ki', 'kd', 'filter_tau'), 2):
                d.wire(ident, 'controller', input_=i)
        else:
            d.wire('error', 'controller')
            d.wire('kp', 'controller', input_=1)
            d.wire('ki', 'controller', input_=2)
        raw = ('controller', 1)
    if plant == 'first_order':
        d.element('plant_rate', 'Adder', dict(weights=[2, -2, -2]), 98, 18,
                  "y' = (u - y - load)/0.5")
        d.element('output', 'EulerIntegrator', dict(dt=d.dt, initial_value=0), 120, 18)
        d.wire('controller', 'plant_rate')
        d.wire('output', 'plant_rate', input_=1)
        d.wire('load', 'plant_rate', input_=2)
        d.wire('plant_rate', 'output')
    else:
        d.element('armature_voltage', 'Gain', dict(factor=4), 85, 18, 'Va = 4*u V')
        d.element('current_rate', 'Adder', dict(weights=[10, -10, -5]), 104, 18,
                  "i' = (Va - i - 0.5*omega)/0.1")
        d.element('current', 'EulerIntegrator', dict(dt=d.dt, initial_value=0), 126, 18, 'Current (A)')
        d.element('speed_rate', 'Adder', dict(weights=[5, -1, -10]), 126, 40,
                  "omega' = (0.5*i - 0.1*omega - load)/0.1")
        d.element('speed', 'EulerIntegrator', dict(dt=d.dt, initial_value=0), 148, 40, 'Speed (rad/s)')
        d.element('output', 'Gain', dict(factor=.2), 148, 18, 'Normalized speed = omega / 5')
        for source, target, port in [('controller', 'armature_voltage', 0),
                ('armature_voltage', 'current_rate', 0), ('current', 'current_rate', 1),
                ('speed', 'current_rate', 2), ('current_rate', 'current', 0),
                ('current', 'speed_rate', 0), ('speed', 'speed_rate', 1),
                ('load', 'speed_rate', 2), ('speed_rate', 'speed', 0), ('speed', 'output', 0)]:
            d.wire(source, target, input_=port)
        d.scope('motor_scope', [('current_A', 'current', 0, 'A'),
                               ('speed_rad_s', 'speed', 0, 'rad/s')], 170, 40)
    d.wire('output', 'error', input_=1)
    if mode == 'pid':
        d.wire('output', 'controller', input_=1)
    d.scope('response_scope', [('reference', 'reference', 0, '1'),
                               ('output', 'output', 0, '1'), ('error', 'error', 0, '1')], 170, 18)
    d.scope('actuator_scope', [('control', 'controller', 0, '1'),
                              ('unlimited', raw[0], raw[1], '1'),
                              ('load', 'load', 0, 'N m' if plant == 'dc_motor' else '1')], 170, 62)
    d.save()


def buck(closed_loop):
    name = 'buck_pid_load_step' if closed_loop else 'buck_open_loop_load_step'
    d = Diagram(name, 1e-5, 'buck', 1,
                '24 V synchronous buck; R: 8 -> 4 ohm at 0.5 s; PWM 1 kHz.')
    # Reuse the existing example's power-stage parameters and topology.
    original = json.loads((ROOT / 'examples/pwm_pid_rlc.s7diagram').read_text())
    plant_ids = {'switch_24V', 'inductor_voltage', 'inverse_L', 'inductor_current',
                 'winding_drop', 'capacitor_current', 'inverse_C', 'output_voltage', 'load_current'}
    for element in original['schema']['elements']:
        if element['id'] in plant_ids:
            node = next(n for n in original['diagram']['nodes'] if n['id'] == element['id'])
            d.element(element['id'], element['type'], element['params'], *node['position'])
    for wire in original['schema']['connections']:
        if wire['from_element'] in plant_ids and wire['to_element'] in plant_ids:
            d.wire(wire['from_element'], wire['to_element'], wire['from_port'], wire['to_port'])
    d.constant('reference', 12, 3, 18)
    d.step('load_resistance', 8, 4, .5, 130, 58)
    d.wire('load_resistance', 'load_current', input_=1)
    if closed_loop:
        d.element('controller', 'PIDController', {}, 54, 18, 'Editable voltage PID; duty [0,1]',
                  schema=controller_schema('PIDController', d.dt, tracking=100, lower=0))
        for ident, value, y, port in [('kp', .04, 30, 2), ('ki', 8, 40, 3),
                                      ('kd', .0004, 50, 4), ('filter_tau', .002, 60, 5)]:
            d.constant(ident, value, 28, y)
            d.wire(ident, 'controller', input_=port)
        d.wire('reference', 'controller')
        d.wire('output_voltage', 'controller', input_=1)
    else:
        d.constant('controller', .5, 54, 18)
    d.element('pwm', 'PWM', dict(period_samples=100, sample_rate_hz=100000), 76, 18)
    d.wire('controller', 'pwm')
    d.wire('pwm', 'switch_24V')
    d.scope('voltage_scope', [('reference_V', 'reference', 0, 'V'),
                             ('voltage_V', 'output_voltage', 0, 'V')], 179, 18)
    d.scope('current_scope', [('inductor_A', 'inductor_current', 0, 'A'),
                             ('load_A', 'load_current', 0, 'A')], 179, 38)
    signals = [('duty', 'controller', 0, '1'), ('gate', 'pwm', 0, '1')]
    if closed_loop:
        signals.append(('unlimited_duty', 'controller', 1, '1'))
    d.scope('switching_scope', signals, 76, 60)
    d.scope('load_scope', [('resistance_ohm', 'load_resistance', 0, 'ohm')], 155, 60)
    d.save()


def thermostat(band):
    d = Diagram('thermostat' if band == 1 else 'thermostat_narrow_band', .1, 'relay', 1200,
                f'Heater lag 10 s; setpoint 20 degC; hysteresis +/-{band} K; ambient 15 -> 10 at 600 s.')
    d.constant('setpoint', 20, 3, 18)
    d.step('ambient', 15, 10, 600, 94, 58)
    d.element('error', 'Adder', dict(weights=[1, -1]), 23, 18, 'Setpoint - temperature (K)')
    d.element('relay', 'HysteresisRelay', dict(lower_threshold=-band, upper_threshold=band,
              low_value=0, high_value=1, initial_on=False), 44, 18)
    d.element('heater_rate', 'Adder', dict(weights=[200, -.1]), 66, 18,
              "P' = (2000*relay - P)/10 W/s")
    d.element('heater_power', 'EulerIntegrator', dict(dt=d.dt, initial_value=0), 88, 18, 'Heater power (W)')
    d.element('thermal_rate', 'Adder', dict(weights=[.0001, -.01, .01]), 110, 18,
              "T' = (P - 100*(T-Ta))/10000 K/s")
    d.element('temperature', 'EulerIntegrator', dict(dt=d.dt, initial_value=18), 132, 18, 'Temperature (degC)')
    for source, target, port in [('setpoint', 'error', 0), ('temperature', 'error', 1),
            ('error', 'relay', 0), ('relay', 'heater_rate', 0), ('heater_power', 'heater_rate', 1),
            ('heater_rate', 'heater_power', 0), ('heater_power', 'thermal_rate', 0),
            ('temperature', 'thermal_rate', 1), ('ambient', 'thermal_rate', 2),
            ('thermal_rate', 'temperature', 0)]:
        d.wire(source, target, input_=port)
    d.scope('temperature_scope', [('temperature_C', 'temperature', 0, 'degC'),
                                  ('setpoint_C', 'setpoint', 0, 'degC'),
                                  ('ambient_C', 'ambient', 0, 'degC')], 157, 18)
    d.scope('heater_scope', [('heater_W', 'heater_power', 0, 'W')], 111, 58)
    d.scope('relay_scope', [('on', 'relay', 0, '1'), ('error_K', 'error', 0, 'K')], 52, 58)
    d.save()


if __name__ == '__main__':
    for plant in ('first_order', 'dc_motor'):
        for mode in ('open_loop', 'p', 'pi', 'pid', 'pi_no_aw'):
            control(plant, mode)
    buck(False)
    buck(True)
    thermostat(1)
    thermostat(.1)
