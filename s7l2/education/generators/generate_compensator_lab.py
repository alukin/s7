#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original CT08 lead/lag experiment, using reusable schema elements."""
import copy
import json
import math
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from diagram_layout import content_bounds
from generate_foundations import diagram_path, Drawing
from generate_control_labs import COMPONENTS, wire, scope
MIT='https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_{}/'


def component(d,ident,kind,x,y,title):
    schema=copy.deepcopy(COMPONENTS[kind]);schema['dt']=.1
    for e in schema['elements']:
        if 'dt' in e['params']: e['params']['dt']=.1
    if kind=='LeadLagController':
        positions={'error':(3,18),'error_memory':(3,34),'zero_product':(20,34),'difference':(37,18),
                   'scaled':(54,18),'sum':(71,18),'control_memory':(71,50),'pole_product':(54,50)}
        inner=Drawing()
        for e in schema['elements']:inner.element(e['id'],e['type'],e['params'],*positions[e['id']],e['id'])
        for c in schema['connections']:wire(inner,c['from_element'],c['to_element'],c['to_port'],output=c['from_port'])
        schema['sheet']=dict(orientation='landscape',width_mm=480,height_mm=340)
        schema['diagram']=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=inner.nodes,wires=inner.wires,
            text_frames=[dict(id='lead_notes',position=[2,1],params=dict(text='u[n] = b u[n−1] + K (e[n] − a e[n−1]).\nTwo explicit memories, no actuator limiter. Gains a,b,K enter as signals.\nThis instance is drawn here; its reusable definition is in schema_elements.s7library.',width=1640,height=240,font_size=18))])
    d.element(ident,kind,{},x,y,title);d.elements[-1]['schema']=schema


def make():
    # Placed directly in grid steps (9-step nodes). Shared parameters sit beside
    # the blocks they feed, so their wires run in short channels instead of
    # crossing the whole worksheet; the router places the feedback returns.
    d=Drawing()
    d.element('reference','StepGenerator',dict(initial_value=0,final_value=.5,start_time=1),3,27,'Reference 0 → 0.5 at source time 1 s')
    d.element('pulse','ImpulseGenerator',dict(area=.1,start_time=.8),3,75,'One unit-height pulse at n=8')
    constants={'base_gain':(3,35),'scale':(17,55),'zero':(17,62),'pole':(17,69),'tau':(31,62)}
    titles={'zero':'Zero a: edit 0.8 → 0.2','pole':'Pole b = 0.2','scale':'Controller scale K = 16','base_gain':'Baseline P gain = 4','tau':'Both plant time constants = 1 s'}
    for ident,value in [('base_gain',4),('scale',16),('zero',.8),('pole',.2),('tau',1)]:
        d.element(ident,'Constant',dict(value=value),*constants[ident],titles[ident])
    for name,y in [('base',27),('lead',44),('open',75)]:
        if name!='open':
            d.element(name+'_error','Adder',dict(weights=[1,-1]),17,y,'Reference minus this output')
            wire(d,'reference',name+'_error');wire(d,name+'_y',name+'_error',1)
        if name=='base':
            d.element('base','Multiply',{},31,y,'Ideal P command: 4 × error');wire(d,'base_error','base');wire(d,'base_gain','base',1)
        else:
            component(d,name,'LeadLagController',31,y,'Lead/lag controller: inspect inside')
            for port,source in enumerate(['pulse' if name=='open' else 'lead_error','scale','zero','pole']):wire(d,source,name,port)
        for state,x in [('x',46),('y',60)]:
            component(d,name+'_'+state,'FirstOrderLag',x,y,'First lag: old state x' if state=='x' else 'Second lag: output y')
            wire(d,name if state=='x' else name+'_x',name+'_'+state);wire(d,'tau',name+'_'+state,1)
    scope(d,'scope',[('reference','reference',0),('P_output','base_y',0),('lead_output','lead_y',0)],74,27,'Same DC loop gain, different transients')
    scope(d,'command_scope',[('P_command','base',0),('lead_command','lead',0)],74,44,'Control effort: lead has a larger initial command')
    d.nodes.append(dict(id='response',position=[74,62],observer=True,type='FrequencyResponse',title='Lead open-loop Bode / Nyquist',params=dict(channel_units=['1','1'])))
    wire(d,'pulse','response',observer=True);wire(d,'open_y','response',1,observer=True)
    scope(d,'pulse_scope',[('pulse','pulse',0),('open_output','open_y',0)],74,75,'Open-loop pulse and tail')
    notes=('**CT08 — Can a lead network improve settling without changing DC gain?**  \n'
           '**Predict → Run → Inspect → Change the zero. Run 64 s at 10 Hz.** P gain 4 and lead C(z)=16(z−0.8)/(z−0.2) both have DC gain 4.  \n'
           'Compare output settling and initial command: lead demands 8, P demands 2 for a 0.5 step. No actuator limiter is present. '
           'The separate pulse path measures the lead open loop; Bode gain reference must be 1.  \n'
           'Select zero, F4: value 0.8 → 0.2; save/reopen/rerun. Exact cancellation makes this controller P gain 16, not P gain 4. '
           'Plotted signals use normalized unit 1; sample n is observed at Scope time (n+1)/10 s.  \n'
           '[Open CT08 student lab](qrc:/s7studio/help/education/labs/CT08-lead-lag/README.md)'
           f' · [MIT lead compensation]({MIT.format(25)}) · [MIT lag compensation]({MIT.format(14)})')
    right,bottom=content_bounds(dict(diagram=dict(nodes=d.nodes,wires=d.wires)))
    return dict(id='ct08_lead_lag',document_kind='s7.diagram',dt=.1,
        studio_run=dict(runtime_seconds=64,clear_scope_traces=True),
        sheet=dict(orientation='landscape',width_mm=470,height_mm=float(math.ceil((bottom+4)*5.291666666666667/10)*10)),
        schema=dict(name='CT08 — Lead/lag compensation and control effort',dt=.1,elements=d.elements,connections=d.connections),probes=[],
        diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
            text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',width=1640,height=390,font_size=20))]))

if __name__=='__main__':
    diagram_path('ct08_lead_lag.s7diagram').write_text(json.dumps(make(),indent=2)+'\n')
