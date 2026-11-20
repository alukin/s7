#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original CT04/CT05 comparisons; reusable controllers remain schema networks."""
import copy
import json
from pathlib import Path
from generate_foundations import Drawing

ROOT = Path(__file__).resolve().parents[2]
COMPONENTS = {e['type_name']: e['schema'] for e in json.loads(
    (ROOT/'s7core/resources/schema_elements.s7library').read_text())['elements']}
MIT_PID = 'https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_10/'
MIT_WINDUP = 'https://ocw.mit.edu/courses/2-017j-design-of-electromechanical-robotic-systems-fall-2009/MIT2_017JF09_feedback.pdf'
LABS = {'CT04': ('ct04_controller_comparison', 'CT04-controller-comparison', 16),
        'CT05': ('ct05_actuator_windup', 'CT05-actuator-windup', 12)}


def wire(d, source, target, port=0, *, output=0, observer=False, bends=()):
    d.wire(source, target, port, observer=observer, bends=bends)
    d.wires[-1]['from']['port'][2] = output
    if not observer: d.connections[-1]['from_port'] = output


def scope(d, ident, channels, x, y, title):
    d.nodes.append(dict(id=ident, position=[x,y], observer=True, type='Scope', title=title,
                        params=dict(channel_count=len(channels), channel_names=[c[0] for c in channels],
                                    channel_units=['1']*len(channels))))
    for port, (_,source,out) in enumerate(channels): wire(d,source,ident,port,output=out,observer=True)


def controller(d, ident, kind, tracking, x, y):
    schema=copy.deepcopy(COMPONENTS[kind]);schema['dt']=.01
    for e in schema['elements']:
        if e['id']=='i_rate': e['params']['weights']=[1,tracking]
        if 'dt' in e['params']: e['params']['dt']=.01
    # Named read-only outputs make the actual state visible without a second model.
    for name, element in [('integral','i_state')]+([('filter','filter_state'),('derivative','derivative')] if kind=='PIDController' else []):
        schema['exposed_outputs'].append(dict(name=name,type='F64',element=element,port=0))
    if kind=='PIController':
        # Original instance drawing: all eight blocks fit the nested page.
        positions={'error':(3,18),'p':(20,18),'sum':(37,18),'limit':(54,18),
                   'i_gain':(20,34),'tracking':(54,34),'i_rate':(37,34),'i_state':(37,50)}
        inner=Drawing()
        for e in schema['elements']:
            inner.element(e['id'],e['type'],e['params'],*positions[e['id']],
                          'i_rate: weights [1, Kt]' if e['id']=='i_rate' else e['id'])
        for c in schema['connections']:
            wire(inner,c['from_element'],c['to_element'],c['to_port'],output=c['from_port'])
        schema['diagram_version']=2
        schema['sheet']=dict(orientation='landscape',width_mm=390,height_mm=340)
        schema['diagram']=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=inner.nodes,wires=inner.wires,
            text_frames=[dict(id='pi_notes',position=[2,1],params=dict(text=f'PI instance: current error → proportional + integral → actuator.\nIntegral rate = Ki × error + Kt × (actual − unlimited); current Kt={tracking}/s.\nTo disable back-calculation, set the second i_rate weight to 0; return to the root tab before Run.',width=1360,height=230,font_size=18))])
    d.element(ident,kind,{},x,y,f'{ident}: {kind}; double-click to inspect')
    d.elements[-1]['schema']=schema


def make(lab):
    name,slug,seconds=LABS[lab];d=Drawing();comparison=lab=='CT04'
    d.element('reference_step','StepGenerator',dict(initial_value=0,final_value=.5 if comparison else 1.5,start_time=1),4,25,'Reference starts at source time 1 s')
    if comparison:
        reference='reference_step'
        d.element('load','StepGenerator',dict(initial_value=0,final_value=.2,start_time=8),4,39,'Opposing load: 0 → 0.2 at 8 s')
    else:
        d.element('release','StepGenerator',dict(initial_value=0,final_value=-1.1,start_time=4),4,39,'Release unreachable command at 4 s')
        d.element('reference','Adder',dict(weights=[1,1]),22,25,'Reference: 0 → 1.5 → 0.4')
        wire(d,'reference_step','reference');wire(d,'release','reference',1);reference='reference'
    for ident,value,y,title in [('kp',2 if comparison else 1.5,53,'Kp (control/error)'),('ki',1.5 if comparison else 2,67,'Ki (control/error/s)')]:
        d.element(ident,'Constant',dict(value=value),4,y,title)
    if comparison:
        d.element('kd','Constant',dict(value=.15),4,81,'Kd = 0.15 s; edit to 0')
        d.element('filter_tau','Constant',dict(value=.1),4,95,'Derivative filter: 0.1 s')
    modes=['p','pi','pid'] if comparison else ['aw','free']
    rows=[25,53,81] if comparison else [53,81]
    for mode,y in zip(modes,rows):
        d.element(mode+'_error','Adder',dict(weights=[1,-1]),22,y if comparison else y,'Error = reference − output')
        wire(d,reference,mode+'_error')
        if mode=='p':
            d.element(mode,'Multiply',{},40,y,'P: Kp × error')
            wire(d,mode+'_error',mode);wire(d,'kp',mode,1)
            d.element('p_limit','Saturation',dict(lower_limit=-1,upper_limit=1),54,y,'Actuator: −1…1')
            wire(d,'p','p_limit');applied='p_limit'
        else:
            controller(d,mode,'PIDController' if mode=='pid' else 'PIController',6 if mode=='aw' else 0,40,y)
            if mode=='pid':
                wire(d,reference,mode);wire(d,mode+'_y',mode,1)
                for port,ident in enumerate(['kp','ki','kd','filter_tau'],2): wire(d,ident,mode,port)
            else:
                for port,ident in enumerate([mode+'_error','kp','ki']): wire(d,ident,mode,port)
            applied=mode
        weights=[1,-1,-1] if comparison else [1/.8,-1/.8]
        d.element(mode+'_rate','Adder',dict(weights=weights),69,y,'Plant derivative (1/s)')
        d.element(mode+'_y','EulerIntegrator',dict(initial_value=0),84,y,'Plant state: initial output 0')
        wire(d,applied,mode+'_rate');wire(d,mode+'_y',mode+'_rate',1)
        if comparison: wire(d,'load',mode+'_rate',2)
        wire(d,mode+'_rate',mode+'_y')
        wire(d,mode+'_y',mode+'_error',1,bends=([96,y+20 if mode=="pid" else y+10],[19,y+20 if mode=="pid" else y+10],[19,y+4]))
    scope(d,'scope',[('reference',reference,0)]+[(m+'_output',m+'_y',0) for m in modes],104,25,'Tracking: reference and plant outputs')
    scope(d,'actuator_scope',[(m+'_control','p_limit' if m=='p' else m,0) for m in modes],104,53,'Applied actuator commands: all bounded ±1')
    if comparison:
        scope(d,'error_scope',[(m+'_error',m+'_error',0) for m in modes],104,81,'Tracking errors; P keeps an offset')
        title='Does integral action remove the load error?'
        question='Run 16 s at 100 Hz. Reference 0 → 0.5 at 1 s; opposing load 0 → 0.2 at 8 s.'
        prediction='Predict P equilibrium 1/3 before load and 4/15 after it. PI/PID return toward 0.5. All units are normalized (1).'
        edit='Open the three Scopes. Select kd, F4: value 0.15 → 0; save/reopen/rerun. PID should become PI, not a better controller by definition.'
    else:
        scope(d,'raw_scope',[(m+'_unlimited',m,1) for m in modes],104,81,'Unlimited demand: not bounded by the actuator')
        scope(d,'state_scope',[(m+'_integral',m,2) for m in modes],69,103,'Integral states: windup and recovery')
        title='Why does a clipped actuator keep pushing after release?'
        question='Run 12 s at 100 Hz. Reference 0 → 1.5 at 1 s → 0.4 at 4 s; actuator limited to ±1.'
        prediction='The 1.5 command is unreachable. Compare actual control, unlimited demand and integral state; free has no anti-windup, aw uses 6/s back-calculation.'
        edit='Double-click aw; select i_rate, F4: weights [1,6] → [1,0]. Return to the root tab, save/reopen/rerun. Both paths should now match.'
    # Compact the worksheet while keeping default node bodies and port rows clear.
    for node in d.nodes: node['position']=[.75*node['position'][0],.75*node['position'][1]+5]
    for w in d.wires: w['bends']=[[.75*x,.75*y+5] for x,y in w['bends']]
    notes=(f'**{lab} — {title}**  \n**Predict → Run → Inspect → Change one thing.** {question}  \n'
           f'{prediction}  \n{edit}  \n'
           'Source/state index n is observed at Scope time (n+1)/100 s. States update afterward; no extra feedback delay is needed.  \n'
           f'[Open {lab} student lab](qrc:/s7studio/help/education/labs/{slug}/README.md)'
           f' · [MIT PID lecture]({MIT_PID}) · [MIT windup notes]({MIT_WINDUP})')
    return dict(id=name,document_kind='s7.diagram',diagram_version=2,dt=.01,
                studio_run=dict(runtime_seconds=seconds,clear_scope_traces=True),
                sheet=dict(orientation='landscape',width_mm=530,height_mm=500),
                schema=dict(name=f'{lab} — {title}',dt=.01,elements=d.elements,connections=d.connections),probes=[],
                diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
                             text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',width=1800,height=400,font_size=20))]))

if __name__=='__main__':
    for lab,(name,*_) in LABS.items():
        Path(__file__).with_name(name+'.s7diagram').write_text(json.dumps(make(lab),indent=2)+'\n')
