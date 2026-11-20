#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original SP14 AM/coherent detector graph; no recorded external media."""
import copy
import json
from generate_foundations import diagram_path, Drawing
from generate_control_labs import COMPONENTS, wire


def make():
    d=Drawing();h=1/256
    for ident,amplitude,frequency,phase,x,y,title in [
        ('message',.6,4,1.5707963267948966,4,25,'Message: 0.6 V peak, 4 Hz cosine'),
        ('carrier',1,32,0,22,45,'Carrier: unit sine, 32 Hz'),
        ('local',2,32,0,40,45,'Local sine ×2; edit phase 0 → π/3')]:
        d.element(ident,'AnalogOscillator',dict(amplitude=amplitude,frequency_hz=frequency,phase=phase),x,y,title)
    d.element('bias','Constant',dict(value=1),4,40,'Carrier bias: 1 V')
    d.element('envelope','Adder',dict(weights=[1,1]),22,25,'Target envelope: 1 + message (V)')
    wire(d,'message','envelope');wire(d,'bias','envelope',1)
    for ident,a,b,x,title in [('transmit','envelope','carrier',40,'AM radio signal (V)'),('mixed','transmit','local',58,'Coherent mixing before filtering (V)')]:
        d.element(ident,'Multiply',{},x,25,title);wire(d,a,ident);wire(d,b,ident,1)
    d.element('tau','Constant',dict(value=.02),76,45,'Each low-pass time constant: 0.02 s')
    for ident,source,x in [('low1','mixed',76),('recovered','low1',94)]:
        schema=copy.deepcopy(COMPONENTS['FirstOrderLag']);schema['dt']=h
        for e in schema['elements']:
            if 'dt' in e['params']:e['params']['dt']=h
        d.element(ident,'FirstOrderLag',{},x,25,'First low-pass state' if ident=='low1' else 'Recovered envelope: two old-state lags')
        d.elements[-1]['schema']=schema;wire(d,source,ident);wire(d,'tau',ident,1)
    for ident,channels,x,y,title in [
        ('rf_scope',[('rf','transmit','V')],40,65,'RF: inspect carrier and sidebands'),
        ('baseband_scope',[('target','envelope','V'),('recovered','recovered','V')],94,58,'Baseband: settling, gain and phase'),
        ('mixer_scope',[('mixed','mixed','V')],58,65,'Unfiltered mixer: DC, message and images'),
        ('carrier_scope',[('carrier','carrier','1'),('local','local','1')],22,65,'Dimensionless carrier / local reference')]:
        d.nodes.append(dict(id=ident,position=[x,y],observer=True,type='Scope',title=title,
            params=dict(channel_count=len(channels),channel_names=[c[0] for c in channels],channel_units=[c[2] for c in channels])))
        for port,(_,source,_) in enumerate(channels):wire(d,source,ident,port,observer=True)
    for node in d.nodes:node['position']=[.75*node['position'][0],.75*node['position'][1]+5]
    for w in d.wires:w['bends']=[[.75*x,.75*y+5] for x,y in w['bends']]
    notes=('**SP14 — Which parts of an AM signal survive coherent detection?**  \n'
           '**Predict → Run → Inspect → Change local phase. Run 4 s at 256 Hz.** Envelope=1+0.6cos(2π4t) V; sine carrier=32 Hz. '
           'Expect RF peaks at 28,32,36 Hz: 0.3,1,0.3 V.  \n'
           'Open the separate RF, baseband and mixer Scopes. For steady spectra select From 2.002 s to 4 s, Rectangular, no mean removal. '
           'The 512 selected samples give Δf=0.5 Hz. Use Peak amplitude (V).  \n'
           'Two forward-Euler low-pass states settle from zero; the 4 Hz message is attenuated/delayed and 60/64/68 Hz images are finite. '
           'Select local, F4: phase 0 → 1.0471975511965976 (π/3); DC/message halve, RF stays unchanged.  \n'
           'Sample n is observed at Scope time (n+1)/256 s. Carrier/local use unit 1; RF and baseband use V.  \n'
           '[Open SP14 student lab](qrc:/s7studio/help/education/labs/SP14-coherent-am/README.md)'
           ' · [MIT modulation](https://ocw.mit.edu/courses/6-003-signals-and-systems-fall-2011/08c6a7bc7e56eda548248c354886dc37_MIT6_003F11_lec23.pdf)'
           ' · [MIT synchronous detection](https://ocw.mit.edu/courses/res-6-007-signals-and-systems-spring-2011/a774c49855ba6bfe7266943eb8b5bc41_MITRES_6_007S11_lec13.pdf)')
    return dict(id='sp14_coherent_am',document_kind='s7.diagram',diagram_version=2,dt=h,
        studio_run=dict(runtime_seconds=4,clear_scope_traces=True),sheet=dict(orientation='landscape',width_mm=475,height_mm=365),
        schema=dict(name='SP14 — AM sidebands and coherent detection',dt=h,elements=d.elements,connections=d.connections),probes=[],
        diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
            text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',width=1680,height=420,font_size=20))]))

if __name__=='__main__':diagram_path('sp14_coherent_am.s7diagram').write_text(json.dumps(make(),indent=2)+'\n')
