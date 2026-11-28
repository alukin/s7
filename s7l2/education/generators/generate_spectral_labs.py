#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original SP08/SP09 source graphs; windows belong to observation, not runtime."""
import json
import math
from generate_foundations import diagram_path, Drawing

LABS={
 'SP08':('sp08_dft_leakage','SP08-dft-leakage','Where did the tone amplitude go?'),
 'SP09':('sp09_window_normalization','SP09-window-normalization','Change the window, preserve the units'),
}


def make(lab):
    ident,slug,title=LABS[lab]
    d=Drawing()
    def tone(name,amplitude,frequency,x,y,label):
        d.element(name,'AnalogOscillator',dict(amplitude=amplitude,frequency_hz=frequency,phase=math.pi/2),x,y,label)
    if lab=='SP08':
        tone('coherent',1,16,5,20,'Reference: coherent at 16 Hz')
        tone('test',1,16.5,5,35,'Test tone: edit frequency_hz')
        d.element('dc','Constant',dict(value=.25),5,50,'Known offset: 0.25 V')
        d.element('offset','Adder',dict(weights=[1,1]),29,35,'Test tone plus offset')
        d.wire('test','offset');d.wire('dc','offset',1)
        channels=[('coherent reference','coherent'),('test tone','test'),('offset mixture','offset'),('DC reference','dc')]
        question='Does the tallest FFT bin equal the amplitude of an off-bin tone?'
        prediction='Run 2 s at 128 Hz. Analyze 0–1 s: N=128, Δf=1 Hz; a 16.5 Hz tone leaks. Analyze 0–2 s: N=256, Δf=0.5 Hz; it is coherent.'
        experiment='Use Rectangular, Peak amplitude, Linear, All inputs, Remove mean off. Edit test frequency_hz 16.5 → 18; predict a single-bin peak.'
        reading='https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec18/'
    else:
        tone('calibration',2,20,5,20,'Coherent calibration: 2 V peak, 20 Hz')
        tone('strong',2,20.5,5,35,'Strong test tone: 20.5 Hz')
        tone('weak',.05,24,5,50,'Weak reference: edit amplitude')
        d.element('mixture','Adder',dict(weights=[1,1]),29,35,'Strong plus weak')
        d.wire('strong','mixture');d.wire('weak','mixture',1)
        channels=[('calibration','calibration'),('strong tone','strong'),('weak reference','weak'),('mixture','mixture')]
        question='Which normalization preserves amplitude, and which preserves power?'
        prediction='Run 2 s at 128 Hz; analyze 0–1 s, N=128. Compare Rectangular, Periodic Hann and Periodic Hamming. The calibration peak stays 2 V.'
        experiment='CG: 1, 0.5, 0.54; ENBW: 1, 1.5, 1.362826 Hz. Use All inputs. Edit weak amplitude 0.05 → 0.1; inspect the mixture near 24 Hz.'
        reading='https://ocw.mit.edu/courses/2-161-signal-processing-continuous-and-discrete-fall-2008/resources/lecture_12/'
    d.nodes.append(dict(id='scope',position=[51,30],observer=True,type='Scope',title='Record, then choose the analysis interval',
                        params=dict(channel_count=4,channel_names=[c[0] for c in channels],channel_units=['V']*4)))
    for port,(_,source) in enumerate(channels):d.wire(source,'scope',port,observer=True)
    notes=(f'**{lab} — {title}**  \n**{question}**  \n{prediction}  \n{experiment}  \n'
           f'[Open {lab} student lab](qrc:/s7studio/help/education/labs/{slug}/README.md)'
           f' · [MIT teaching materials]({reading})'
           ' · [S7 spectrum guide](qrc:/s7studio/help/docs/STUDIO_SPECTRUM.md)')
    return dict(id=ident,dt=1/128,document_kind='s7.diagram',
                studio_run=dict(runtime_seconds=2,clear_scope_traces=True),
                sheet=dict(orientation='landscape',width_mm=370,height_mm=335),
                schema=dict(name=f'{lab} — {title}',dt=1/128,elements=d.elements,connections=d.connections),probes=[],
                diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
                  text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',width=1298,height=340,font_size=18))]))


if __name__=='__main__':
    for lab,(ident,*_) in LABS.items():diagram_path(ident+'.s7diagram').write_text(json.dumps(make(lab),indent=2)+'\n')
