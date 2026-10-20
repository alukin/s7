#!/usr/bin/env python3
"""Generate the final PLL and signal-measurement teaching laboratories."""
import copy
import math
import sys

sys.dont_write_bytecode = True
from generate_control_examples import COMPONENTS, Diagram, controller_schema


def component(kind, dt):
    schema = copy.deepcopy(COMPONENTS[kind])
    schema['dt'] = dt
    for element in schema['elements']:
        if 'dt' in element['params']:
            element['params']['dt'] = dt
    return schema


def pll(mode):
    d = Diagram('pll_'+mode, .001, 'pll', 6,
                'Ideal phase-domain detector; reference 20 Hz, VCO 18+10*u Hz, initial phase error 0.8 rad.')
    final = 35 if mode == 'out_of_range' else 22 if mode == 'frequency_step' else 20
    d.step('reference_frequency', 20, final, 2, 3, 18)
    d.element('reference_rate', 'Gain', dict(factor=2*math.pi), 24, 18, '2*pi*f_ref (rad/s)')
    d.element('reference_phase', 'EulerIntegrator', dict(dt=d.dt, initial_value=.8), 46, 18, 'Reference phase (rad)')
    d.step('phase_step', 0, 1 if mode == 'phase_step' else 0, 2, 24, 36)
    d.element('reference_angle', 'Adder', dict(weights=[1, 1]), 66, 18)
    d.element('phase_error', 'PhaseDifference', {}, 88, 18,
              'wrap(reference - VCO) (rad)', schema=component('PhaseDifference', d.dt))
    d.element('controller', 'PIController', {}, 110, 18,
              'PI loop filter: kp=0.1, ki=1, u in [-1,1]', schema=controller_schema('PIController', d.dt))
    d.constant('kp', .1, 88, 36)
    d.constant('ki', 1, 110, 36)
    d.constant('nominal_frequency', 18, 132, 36)
    d.element('vco_frequency', 'Adder', dict(weights=[1, 10]), 132, 18, '18 + 10*u (Hz)')
    d.element('vco_rate', 'Gain', dict(factor=2*math.pi), 154, 18, '2*pi*f_vco (rad/s)')
    d.element('vco_phase', 'EulerIntegrator', dict(dt=d.dt, initial_value=0), 154, 48, 'VCO phase (rad)')
    for source, target, port in [('reference_frequency', 'reference_rate', 0),
            ('reference_rate', 'reference_phase', 0), ('reference_phase', 'reference_angle', 0),
            ('phase_step', 'reference_angle', 1), ('reference_angle', 'phase_error', 0),
            ('vco_phase', 'phase_error', 1), ('phase_error', 'controller', 0),
            ('kp', 'controller', 1), ('ki', 'controller', 2),
            ('nominal_frequency', 'vco_frequency', 0), ('controller', 'vco_frequency', 1),
            ('vco_frequency', 'vco_rate', 0), ('vco_rate', 'vco_phase', 0)]:
        d.wire(source, target, input_=port)
    for label, phase, x in [('reference', 'reference_angle', 24), ('vco', 'vco_phase', 88)]:
        d.element(label+'_wrapped', 'PhaseWrap', {}, x, 50)
        d.element(label+'_signal', 'Trig', dict(function='sin'), x, 63, label+' waveform')
        d.element(label+'_meter', 'FrequencyMeter', dict(sample_rate_hz=1/d.dt), x+22, 63)
        d.wire(phase, label+'_wrapped')
        d.wire(label+'_wrapped', label+'_signal')
        d.wire(label+'_signal', label+'_meter')
    d.scope('frequency_scope', [('reference_Hz', 'reference_frequency', 0, 'Hz'),
                               ('vco_Hz', 'vco_frequency', 0, 'Hz'),
                               ('measured_vco_Hz', 'vco_meter', 0, 'Hz')], 175, 18)
    d.scope('phase_scope', [('phase_error_rad', 'phase_error', 0, 'rad'),
                           ('control', 'controller', 0, '1'), ('unlimited', 'controller', 1, '1')], 175, 38)
    d.scope('waveform_scope', [('reference', 'reference_signal', 0, '1'),
                              ('vco', 'vco_signal', 0, '1')], 175, 56)
    d.scope('measurement_scope', [('measured_reference_Hz', 'reference_meter', 0, 'Hz'),
                                 ('reference_valid', 'reference_meter', 1, '1'),
                                 ('vco_valid', 'vco_meter', 1, '1')], 175, 72)
    d.save()


def lag(d, ident, source, tau, x, y):
    d.element(ident, 'FirstOrderLag', {}, x, y, schema=component('FirstOrderLag', d.dt))
    d.constant(ident+'_tau', tau, x, y+14)
    d.wire(source, ident)
    d.wire(ident+'_tau', ident, input_=1)


def am():
    d = Diagram('am_coherent_detection', .0001, 'signal_measurement', 2,
                '100 Hz carrier, 2 Hz message, modulation index 0.6; coherent detection, tau=0.02 s.')
    d.element('message', 'AnalogOscillator', dict(amplitude=1, frequency_hz=2, sample_rate_hz=10000), 3, 18)
    d.constant('bias', 1, 3, 34)
    d.element('envelope', 'Adder', dict(weights=[1, .6]), 24, 18, '1 + 0.6*message')
    d.wire('bias', 'envelope')
    d.wire('message', 'envelope', input_=1)
    d.element('carrier', 'AnalogOscillator', dict(amplitude=1, frequency_hz=100, sample_rate_hz=10000), 24, 50)
    d.element('am_signal', 'Multiply', {}, 46, 18)
    d.wire('envelope', 'am_signal')
    d.wire('carrier', 'am_signal', input_=1)
    d.element('mixer', 'Multiply', {}, 68, 18, 'Received signal * coherent carrier')
    d.wire('am_signal', 'mixer')
    d.wire('carrier', 'mixer', input_=1)
    d.element('twice_mixer', 'Gain', dict(factor=2), 90, 18)
    d.wire('mixer', 'twice_mixer')
    lag(d, 'lowpass', 'twice_mixer', .02, 112, 18)
    d.element('recovered', 'Adder', dict(weights=[1/.6, -1/.6]), 134, 18, '(Filtered envelope - 1)/0.6')
    d.wire('lowpass', 'recovered')
    d.wire('bias', 'recovered', input_=1)
    d.scope('message_scope', [('message', 'message', 0, '1'), ('recovered', 'recovered', 0, '1')], 170, 18)
    d.scope('carrier_scope', [('am_V', 'am_signal', 0, 'V'), ('envelope_V', 'envelope', 0, 'V'),
                             ('carrier', 'carrier', 0, '1')], 170, 42)
    d.scope('detector_scope', [('mixer_V', 'twice_mixer', 0, 'V'), ('filtered_V', 'lowpass', 0, 'V')], 112, 58)
    d.save()


def fm():
    d = Diagram('fm_frequency_detection', .0001, 'signal_measurement', 2,
                '100 Hz carrier, +/-20 Hz deviation, 2 Hz message; crossing-period detection and 0.02 s smoothing.')
    d.element('message', 'AnalogOscillator', dict(amplitude=1, frequency_hz=2, sample_rate_hz=10000), 3, 18)
    d.constant('carrier_frequency', 100, 3, 36)
    d.element('frequency', 'Adder', dict(weights=[1, 20]), 25, 18, '100 + 20*message (Hz)')
    d.wire('carrier_frequency', 'frequency')
    d.wire('message', 'frequency', input_=1)
    d.element('fm_signal', 'ControlledOscillator', dict(amplitude=1, sample_rate_hz=10000, phase=0), 47, 18)
    d.wire('frequency', 'fm_signal')
    d.element('meter', 'FrequencyMeter', dict(sample_rate_hz=10000), 69, 18)
    d.wire('fm_signal', 'meter')
    d.element('raw_message', 'Adder', dict(weights=[.05, -.05]), 91, 18, '(Measured Hz - 100)/20')
    d.wire('meter', 'raw_message')
    d.wire('carrier_frequency', 'raw_message', input_=1)
    d.constant('zero', 0, 69, 50)
    d.element('valid_message', 'Switch', {}, 113, 18, 'Suppress estimate until meter valid')
    d.wire('zero', 'valid_message')
    d.wire('raw_message', 'valid_message', input_=1)
    d.wire('meter', 'valid_message', output=1, input_=2)
    lag(d, 'lowpass', 'valid_message', .02, 135, 18)
    d.scope('message_scope', [('message', 'message', 0, '1'), ('raw_detected', 'valid_message', 0, '1'),
                             ('smoothed', 'lowpass', 0, '1')], 172, 18)
    d.scope('frequency_scope', [('command_Hz', 'frequency', 0, 'Hz'),
                               ('measured_Hz', 'meter', 0, 'Hz')], 172, 42)
    d.scope('waveform_scope', [('fm_V', 'fm_signal', 0, 'V')], 69, 66)
    d.scope('validity_scope', [('valid', 'meter', 1, '1')], 135, 66)
    d.save()


def rms(dc):
    name = 'rms_dc_step' if dc else 'rms_sine_step'
    d = Diagram(name, .0001, 'signal_measurement', 2,
                'Exponential RMS tau=0.05 s; source step at 1 s; initial mean-square state is zero.')
    d.step('amplitude', -2 if dc else 2, 1, 1, 3, 18)
    if dc:
        d.element('signal', 'Gain', dict(factor=1), 47, 18, 'Signed DC voltage (V)')
        d.wire('amplitude', 'signal')
    else:
        d.element('carrier', 'AnalogOscillator', dict(amplitude=1, frequency_hz=50, sample_rate_hz=10000), 3, 42)
        d.element('signal', 'Multiply', {}, 47, 18, 'Amplitude * sin(2*pi*50*t) (V)')
        d.wire('amplitude', 'signal')
        d.wire('carrier', 'signal', input_=1)
    d.element('rms', 'RMSDetector', {}, 91, 18, 'sqrt(exponentially filtered mean square)',
              schema=component('RMSDetector', d.dt))
    d.constant('rms_tau', .05, 69, 42)
    d.wire('signal', 'rms')
    d.wire('rms_tau', 'rms', input_=1)
    d.element('expected', 'MathFunction' if dc else 'Gain',
              dict(function='abs') if dc else dict(factor=1/math.sqrt(2)), 91, 58, 'Steady RMS target (V)')
    d.wire('amplitude', 'expected')
    d.scope('rms_scope', [('rms_V', 'rms', 0, 'V'), ('steady_target_V', 'expected', 0, 'V')], 140, 18)
    d.scope('source_scope', [('signal_V', 'signal', 0, 'V'), ('amplitude_V', 'amplitude', 0, 'V')], 140, 48)
    d.save()


if __name__ == '__main__':
    for mode in ('frequency_step', 'phase_step', 'out_of_range'):
        pll(mode)
    am()
    fm()
    rms(False)
    rms(True)
