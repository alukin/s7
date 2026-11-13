#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Keep the analysis galleries compact, readable and reproducible.

Called by the surface/time-frequency generators; can also refresh all layouts
without changing their computational graphs. Positions are drawing-grid steps.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def refine(path):
    doc = json.loads(path.read_text())
    family, name = path.parent.name, path.stem
    diagram = doc['diagram']
    if name == 'pll_acquisition':
        for node in diagram['nodes']:
            if node['id'] == 'wavelet_map':
                node['position'] = [132, 74]
                node['title'] = 'Phase-error scalogram'
        diagram['text_frames'] = [dict(id='notes', position=[2, 2], params=dict(
            text='PLL frequency step: control-loop settling in time and frequency\n'
                 'Run 6 s at 1000 Hz. Reference changes from 20 to 22 Hz at 2 s.\n'
                 'Open Phase-error scalogram, analyze, then compare STFT with Morlet CWT.\n'
                 'Relate transient phase-error energy to the frequency overshoot and subsequent lock.\n'
                 'Ideal phase-domain detector; inspect the existing time scopes too. Model details: examples/pll/README.md.',
            width=2600, height=220, font_size=16))]
        path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + '\n')
        return

    def observer(id, kind, source, port=0):
        if not any(n['id'] == id for n in diagram['nodes']):
            diagram['nodes'].append(dict(id=id, observer=True, type=kind, params={}))
        if kind == 'Scope':
            node = next(n for n in diagram['nodes'] if n['id'] == id)
            node['params'] = dict(channel_count=1, channel_names=[id.replace('_', ' ').title()])
        if not any(w['to']['port'][0] == id for w in diagram['wires']):
            diagram['wires'].append(dict(id='observe_' + id,
                **{'from': dict(port=[source, 'out', port]),
                   'to': dict(port=[id, 'in', 0])}, bends=[]))

    titles = {}
    if family in ('surfaces', 'complex_functions'):
        positions = {'grid': [3, 15], 'function': [23, 13], 'surface': [43, 15]}
        titles = {'grid': 'Coordinate scan', 'function': 'f(x,y)' if family == 'surfaces' else 'f(z)',
                  'surface': 'Surface viewer'}
        descriptions = {
            'plane': ('Tilted plane: f(x,y) = x + 2y', 'Compare the two slopes: Y changes height twice as fast as X.'),
            'bowl': ('Paraboloid: f(x,y) = x² + y²', 'Find the minimum 0 at the origin; corners reach 8. Compare circular contours.'),
            'saddle': ('Saddle: f(x,y) = x² − y²', 'X and Y sections curve in opposite directions; both diagonals have height 0.'),
            'gaussian': ('Gaussian hill: f(x,y) = exp(−x²−y²)', 'Locate the central peak 1; follow radial decay with heatmap contours.'),
            'wave': ('Standing wave: f(x,y) = sin(x) cos(y)', 'Find alternating crests and troughs. The nodal lines have height 0.'),
            'sinc': ('Radial sinc: f(x,y) = sin(r)/r, r = √(x²+y²)', 'The analytic extension f(0,0)=1 is included; examine rings and decreasing lobes.'),
            'identity': ('Identity: f(z) = z, z = x + iy', 'Height is |f|; hue is phase. Walk around the zero: hue makes one full turn.'),
            'square': ('Complex square: f(z) = z²', 'Height is x²+y². Walk once around zero: phase makes two full turns.'),
            'inverse': ('Reciprocal: f(z) = 1/z', 'Opposite phase winding; try log height near the pole. The finite peak depends on grid spacing.'),
            'rational': ('Zero and pole: f(z) = (z−1)/(z+1)', 'Compare the zero at +1 with the pole at −1. Try magnitude and log height.'),
        }
        title, lesson = descriptions[name]
        notes = [title, 'Run 5 s at 1000 Hz; double-click Surface viewer. Coordinates are dimensionless.',
                 lesson, 'Compare Surface, Wireframe and Heatmap; hover cells and enable contours.']
    elif family == 'frequency_response':
        output = 'delay2' if name == 'delay' else 'plant'
        observer('input_scope', 'Scope', 'excitation')
        observer('output_scope', 'Scope', output)
        positions = {'excitation': [3, 22], 'plant': [23, 22], 'omega': [3, 12], 'zeta': [23, 12],
                     'tau': [23, 12], 'response': [43, 22], 'input_scope': [3, 36], 'output_scope': [43, 36],
                     'force_scale': [16, 22], 'delay0': [16, 22], 'delay1': [29, 22], 'delay2': [42, 22]}
        if name == 'delay':
            positions.update(response=[29, 36], output_scope=[43, 36])
        if name == 'mass_spring_damper':
            positions['force_scale'] = [16, 22]
            positions['plant'] = [30, 22]
            positions['response'] = [43, 22]
        titles = {'excitation': 'One-sample pulse', 'plant': 'Plant output', 'omega': 'ω₀ = 2π·5 rad/s',
                  'zeta': 'ζ = 0.3', 'tau': 'τ = 0.1 s', 'force_scale': '1 / stiffness',
                  'input_scope': 'Excitation vs time', 'output_scope': 'Response vs time', 'response': 'Frequency response',
                  'delay0': 'Delay 1', 'delay1': 'Delay 2', 'delay2': 'Delay 3'}
        lessons = {
            'gain': ('Constant gain 2.5', 'The output pulse is 2.5 times the input; Bode gain is 7.96 dB and phase is 0°.'),
            'delay': ('Three-sample delay', 'Magnitude stays 1; unwrapped phase falls linearly to −540° at Nyquist.'),
            'rc': ('RC low-pass: τ = 0.1 s', 'The output decays exponentially; the corner is near 1/(2πτ) = 1.59 Hz.'),
            'rlc': ('RLC resonance: f₀ = 5 Hz, ζ = 0.3', 'Observe the decaying oscillation and resonant peak. Continuous reference: ω₀=2π·5, ζ=0.3, DC gain=1.'),
            'mass_spring_damper': ('Mass–spring–damper: m=1 kg, f₀=5 Hz, ζ=0.3', 'Force → displacement; DC gain=1/(2π·5)² ≈ 0.0010132 m/N. Match this in the reference.'),
        }
        title, lesson = lessons[name]
        rate = 1024 if name in ('rlc', 'mass_spring_damper') else 1000
        notes = [title, f'Run 2 s at {rate} Hz; open the time scopes, then Frequency response → Analyze captured impulse.',
                 lesson, 'Pulse area = 1; inspect input and output separately so the pulse does not hide the response.']
    elif family == 'time_frequency':
        positions = {'tone': [3, 14], 'second': [3, 25], 'gate': [3, 25], 'sum': [23, 18],
                     'burst': [23, 18], 'map': [43, 22], 'excitation': [3, 24], 'plant': [23, 24],
                     'omega': [3, 13], 'zeta': [23, 13]}
        titles = {'tone': '32 Hz, amplitude 1', 'second': '64 Hz, amplitude 0.6', 'gate': '0.5 s every 2 s',
                  'sum': 'Superposition', 'burst': 'Gated tone', 'map': 'Time–frequency viewer',
                  'omega': 'ω₀ = 2π·16 rad/s', 'zeta': 'ζ = 0.2', 'plant': 'Resonant low-pass',
                  'excitation': 'Chirp: 4 → 90 Hz' if name in ('chirp', 'resonant_chirp') else 'Pulse at 1.5 s'}
        lessons = {
            'tone': ('Stationary tone', 'A horizontal ridge at 32 Hz. STFT magnitude at the coherent bin is 1.'),
            'tone_bursts': ('Localized tone bursts', '32 Hz appears for 0.5 s every 2 s. Compare time smearing in STFT and CWT.'),
            'overlapping_tones': ('Two simultaneous tones', 'Resolve 32 Hz and 64 Hz ridges; STFT amplitudes are 1 and 0.6.'),
            'chirp': ('Linear chirp: 4 → 90 Hz in 8 s', 'Compare the straight ridge on a linear frequency axis with its curved log-axis view.'),
            'impulse': ('A one-sample event', 'Broadband energy is localized at 1.5 s. CWT time spread increases at low frequencies.'),
            'ring_down': ('Resonant ring-down', 'An impulse starts a decaying oscillation near 16 Hz; find its transient ridge.'),
            'resonant_chirp': ('Chirp through a resonance', 'Follow the rising ridge and the strongest response near the 16 Hz plant resonance.'),
        }
        title, lesson = lessons[name]
        notes = [title, 'Run 8 s at 1024 Hz; open Time–frequency viewer → Analyze / freeze.', lesson,
                 'Compare STFT and Morlet CWT; gray means incomplete support, not zero energy.']
    else:
        wavelet = name == 'wavelet_coefficient'
        diagram['wires'] = [w for w in diagram['wires']
                            if not (w['to']['port'][0] == 'scope' and w['to']['port'][2] >= 2)]
        for node in diagram['nodes']:
            if node['id'] == 'scope':
                node['params']['channel_count'] = 2
                node['params'].pop('channels', None)
                node['params']['channel_names'] = ['Real coefficient', 'Imaginary coefficient']
        observer('validity', 'Scope', 'analysis', 1)
        if wavelet:
            observer('delay_scope', 'Scope', 'analysis', 2)
        positions = {'source': [3, 15], 'analysis': [19, 15], 'parts': [35, 15], 'scope': [43, 29]}
        positions.update(validity=[19, 29], delay_scope=[3, 38])
        titles = {'source': '32 Hz unit sine', 'analysis': 'Complex coefficient', 'parts': 'Real / imaginary',
                  'scope': 'Complex coefficient', 'validity': 'Full support' if wavelet else 'Update flag',
                  'delay_scope': 'Center delay (samples)'}
        notes = [f'{name.replace("_", " ").title()}: computation inside the causal graph',
                 'Load the optional analysis plugin; run 2 s at 1024 Hz and open the scope.',
                 'The C64 coefficient is split explicitly. Inspect the update/valid flag before using held values.',
                 'Fourier uses peak-amplitude normalization; Morlet uses discrete L2 normalization (see README).']

    for node in diagram['nodes']:
        node['position'] = positions[node['id']]
        node['title'] = titles.get(node['id'], node['id'])
    # Discard obsolete routes: the production router uses the new node/port bounds.
    for wire in diagram['wires']:
        wire['bends'] = []
        wire.pop('path', None)
    diagram['text_frames'] = [dict(id='notes', position=[2, 2],
        params=dict(text='\n'.join(notes), width=1040, height=160, font_size=12))]
    doc['sheet'] = dict(orientation='landscape', width_mm=300, height_mm=250)
    path.write_text(json.dumps(doc, indent=2, ensure_ascii=False) + '\n')


if __name__ == '__main__':
    for family in ('surfaces', 'complex_functions', 'frequency_response', 'time_frequency', 'analysis_elements'):
        for path in sorted((ROOT / 'examples' / family).glob('*.s7diagram')):
            refine(path)
