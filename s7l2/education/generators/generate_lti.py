#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original SP05/SP07 teaching graphs; construction only, no solver."""
import json
from generate_foundations import diagram_path, Drawing

LABS={
 'SP05':('sp05_convolution','SP05-convolution','Add shifted impulse responses',20,2),
 'SP07':('sp07_filter_transfer','SP07-filter-transfer','Which filter resonates?',64,64),
}


def scope(d, ident, channels, x, y, title):
    d.nodes.append(dict(id=ident,position=[x,y],observer=True,type='Scope',title=title,
                        params=dict(channel_count=len(channels),channel_names=[c[0] for c in channels],channel_units=['V']*len(channels))))
    for port,(_,source) in enumerate(channels): d.wire(source,ident,port,observer=True)


def fir(d, ident, source, y):
    d.element(ident+'_d1','Delay',dict(initial_value=0),25,y+9,'One sample of input history')
    d.element(ident+'_d2','Delay',dict(initial_value=0),37,y+9,'Two samples of input history')
    d.element(ident,'Adder',dict(weights=[1,.5,-.25]),49,y,'Kernel weights: 1, 0.5, −0.25')
    d.wire(source,ident)
    d.wire(source,ident+'_d1')
    d.wire(ident+'_d1',ident+'_d2')
    d.wire(ident+'_d1',ident,1,bends=([35,y+11],[35,y+4]))
    d.wire(ident+'_d2',ident,2,bends=([47,y+11],[47,y+6]))


def make(lab):
    ident,slug,title,rate,seconds=LABS[lab]
    d=Drawing()
    if lab=='SP05':
        d.element('first','ImpulseGenerator',dict(area=1/rate,start_time=.2),3,18,'First unit sample: n=4')
        d.element('second','ImpulseGenerator',dict(area=1/rate,start_time=.3),3,36,'Second unit sample: n=6')
        d.element('weight','Gain',dict(factor=-.5),15,36,'Second pulse weight: edit factor')
        d.wire('second','weight')
        fir(d,'first_response','first',18)
        fir(d,'second_response','weight',36)
        d.element('input','Adder',dict(weights=[1,1]),15,54,'Combined input')
        d.wire('first','input',bends=([13,20],[13,56]));d.wire('weight','input',1)
        fir(d,'output','input',54)
        d.element('sum','Adder',dict(weights=[1,1]),63,36,'Sum of shifted responses')
        d.wire('first_response','sum');d.wire('second_response','sum',1)
        d.element('residual','Adder',dict(weights=[1,-1]),63,54,'Actual minus shifted sum')
        d.wire('output','residual');d.wire('sum','residual',1)
        scope(d,'parts_scope',[('first contribution','first_response'),('second contribution','second_response')],63,18,'Shifted contributions')
        scope(d,'scope',[('input','input'),('actual','output'),('shifted sum','sum'),('difference','residual')],79,54,'Convolution comparison')
        prediction='Kernel h=[1, 0.5, −0.25]. At n=4…8 predict 1, 0.5, −0.75, −0.25, 0.125 V; difference stays zero.'
        edit='Change weight factor −0.5 → 0.75; predict the overlap at n=6. All six Delay states start at zero. Zoom to read the three rows.'
        sheet=(500,390);lecture='08';question='Does filtering a sum equal adding the shifted responses?'
    else:
        d.element('input','ImpulseGenerator',dict(area=1/64,start_time=.125),3,19,'One 1 V sample: n=8 at 64 Hz')
        d.element('low_error','Adder',dict(weights=[1,-1]),18,19,'Input minus low-pass state')
        d.element('low_rate','Gain',dict(factor=2),33,19,'Low-pass rate: 2 per second')
        d.element('low','EulerIntegrator',dict(initial_value=0),48,19,'Low-pass voltage: starts at 0 V')
        d.wire('input','low_error');d.wire('low','low_error',1)
        d.wire('low_error','low_rate');d.wire('low_rate','low')
        d.element('position_error','Adder',dict(weights=[1,-1]),18,40,'Input minus resonant state')
        d.element('stiffness','Gain',dict(factor=16),33,40,'Natural frequency squared: 16 per s²')
        d.element('acceleration','Adder',dict(weights=[1,-1]),49,40,'Acceleration: drive minus damping')
        d.element('velocity','EulerIntegrator',dict(initial_value=0),64,40,'Rate state: starts at 0 V/s')
        d.element('resonant','EulerIntegrator',dict(initial_value=0),64,56,'Resonant voltage: starts at 0 V')
        d.element('damping','Gain',dict(factor=1.6),49,56,'Damping coefficient: edit factor')
        for a,b,p in [('input','position_error',0),('resonant','position_error',1),('position_error','stiffness',0),
                      ('stiffness','acceleration',0),('damping','acceleration',1),('acceleration','velocity',0),
                      ('velocity','resonant',0),('velocity','damping',0)]: d.wire(a,b,p)
        scope(d,'input_scope',[('input','input')],3,56,'Input pulse')
        scope(d,'scope',[('low-pass','low'),('resonant','resonant')],80,19,'Filter outputs: inspect 0–10 s')
        for name,source,y,title_ in [('low_response','low',39,'Low-pass frequency response'),('resonant_response','resonant',56,'Resonant frequency response')]:
            d.nodes.append(dict(id=name,position=[80,y],observer=True,type='FrequencyResponse',title=title_,params=dict(channel_units=['V','V'])))
            d.wire('input',name,observer=True);d.wire(source,name,1,observer=True)
        prediction='Both DC gains are 1. The resonant peak is about 2.99 near 0.625 Hz. Low-pass starts one sample after the pulse; resonant starts two later.'
        edit='Change damping factor 1.6 → 3.2; predict a lower resonance. Capture 64 s for the decaying tail; Scope: inspect 0–10 s.'
        sheet=(500,365);lecture='11';question='Does the measured response match the actual discrete filter?'
    notes=(f'**{lab} — {title}**  \n**{question}** Run **{seconds} s at {rate} Hz**.  \n'
           f'{prediction}  \n{edit}  \n'
           f'[Open {lab} student lab](qrc:/s7studio/help/education/labs/{slug}/README.md)'
           f' · [MIT teaching materials](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/resources/mit6_003f11_lec{lecture}/)'
           ' · [S7 timing and integrators](qrc:/s7studio/help/docs/INTEGRATORS.md)')
    return dict(id=ident,dt=1/rate,document_kind='s7.diagram',
                studio_run=dict(runtime_seconds=seconds,clear_scope_traces=True),
                sheet=dict(orientation='landscape',width_mm=sheet[0],height_mm=sheet[1]),
                schema=dict(name=f'{lab} — {title}',dt=1/rate,elements=d.elements,connections=d.connections),probes=[],
                diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
                  text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',
                      width=int(sheet[0]*96/25.4)-100,height=300,font_size=20))]))


if __name__=='__main__':
    for lab,(ident,*_) in LABS.items():
        diagram_path(ident+'.s7diagram').write_text(json.dumps(make(lab),indent=2)+'\n')
