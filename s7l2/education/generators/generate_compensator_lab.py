#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original CT08 lead/lag experiment, using reusable schema elements."""
import copy
import json
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
        schema['diagram_version']=2;schema['sheet']=dict(orientation='landscape',width_mm=480,height_mm=340)
        schema['diagram']=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=inner.nodes,wires=inner.wires,
            text_frames=[dict(id='lead_notes',position=[2,1],params=dict(text='u[n] = b u[n−1] + K (e[n] − a e[n−1]).\nTwo explicit memories, no actuator limiter. Gains a,b,K enter as signals.\nThis instance is drawn here; its reusable definition is in schema_elements.s7library.',width=1640,height=240,font_size=18))])
    d.element(ident,kind,{},x,y,title);d.elements[-1]['schema']=schema


def make():
    d=Drawing()
    d.element('reference','StepGenerator',dict(initial_value=0,final_value=.5,start_time=1),4,30,'Reference 0 → 0.5 at source time 1 s')
    d.element('pulse','ImpulseGenerator',dict(area=.1,start_time=.8),4,90,'One unit-height pulse at n=8')
    for ident,value,x in [('base_gain',4,4),('scale',16,20),('zero',.8,36),('pole',.2,52),('tau',1,68)]:
        d.element(ident,'Constant',dict(value=value),x,111,{'zero':'Zero a: edit 0.8 → 0.2','pole':'Pole b = 0.2','scale':'Controller scale K = 16','base_gain':'Baseline P gain = 4','tau':'Both plant time constants = 1 s'}[ident])
    for name,y in [('base',30),('lead',60),('open',90)]:
        if name!='open':
            d.element(name+'_error','Adder',dict(weights=[1,-1]),22,y,'Reference minus this output')
            wire(d,'reference',name+'_error');wire(d,name+'_y',name+'_error',1,bends=([94,y+15],[19,y+15],[19,y+4]))
        if name=='base':
            d.element('base','Multiply',{},40,y,'Ideal P command: 4 × error');wire(d,'base_error','base');wire(d,'base_gain','base',1)
        else:
            component(d,name,'LeadLagController',40,y,'Lead/lag controller: inspect inside')
            for port,source in enumerate(['pulse' if name=='open' else 'lead_error','scale','zero','pole']):wire(d,source,name,port)
        for state,x in [('x',60),('y',80)]:
            component(d,name+'_'+state,'FirstOrderLag',x,y,'First lag: old state x' if state=='x' else 'Second lag: output y')
            wire(d,name if state=='x' else name+'_x',name+'_'+state);wire(d,'tau',name+'_'+state,1)
    scope(d,'scope',[('reference','reference',0),('P_output','base_y',0),('lead_output','lead_y',0)],100,30,'Same DC loop gain, different transients')
    scope(d,'command_scope',[('P_command','base',0),('lead_command','lead',0)],100,60,'Control effort: lead has a larger initial command')
    scope(d,'pulse_scope',[('pulse','pulse',0),('open_output','open_y',0)],100,111,'Open-loop pulse and tail')
    d.nodes.append(dict(id='response',position=[100,90],observer=True,type='FrequencyResponse',title='Lead open-loop Bode / Nyquist',params=dict(channel_units=['1','1'])))
    wire(d,'pulse','response',observer=True);wire(d,'open_y','response',1,observer=True)
    for node in d.nodes:node['position']=[.65*node['position'][0],.65*node['position'][1]+5]
    for w in d.wires:w['bends']=[[.65*x,.65*y+5] for x,y in w['bends']]
    notes=('**CT08 — Can a lead network improve settling without changing DC gain?**  \n'
           '**Predict → Run → Inspect → Change the zero. Run 64 s at 10 Hz.** P gain 4 and lead C(z)=16(z−0.8)/(z−0.2) both have DC gain 4.  \n'
           'Compare output settling and initial command: lead demands 8, P demands 2 for a 0.5 step. No actuator limiter is present. '
           'The separate pulse path measures the lead open loop; Bode gain reference must be 1.  \n'
           'Select zero, F4: value 0.8 → 0.2; save/reopen/rerun. Exact cancellation makes this controller P gain 16, not P gain 4. '
           'Plotted signals use normalized unit 1; sample n is observed at Scope time (n+1)/10 s.  \n'
           '[Open CT08 student lab](qrc:/s7studio/help/education/labs/CT08-lead-lag/README.md)'
           f' · [MIT lead compensation]({MIT.format(25)}) · [MIT lag compensation]({MIT.format(14)})')
    return dict(id='ct08_lead_lag',document_kind='s7.diagram',diagram_version=2,dt=.1,
        studio_run=dict(runtime_seconds=64,clear_scope_traces=True),sheet=dict(orientation='landscape',width_mm=470,height_mm=485),
        schema=dict(name='CT08 — Lead/lag compensation and control effort',dt=.1,elements=d.elements,connections=d.connections),probes=[],
        diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
            text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',width=1640,height=390,font_size=20))]))

if __name__=='__main__':
    diagram_path('ct08_lead_lag.s7diagram').write_text(json.dumps(make(),indent=2)+'\n')
