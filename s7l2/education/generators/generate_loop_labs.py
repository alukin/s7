#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Original CT06/CT07 sampled-loop graphs, no numerical solver."""
import json
from generate_foundations import diagram_path, Drawing
from generate_control_labs import wire, scope
MIT='https://ocw.mit.edu/courses/16-06-principles-of-automatic-control-fall-2012/resources/mit16_06f12_lecture_{}/'
LABS={'CT06':('ct06_closed_loop_poles','CT06-closed-loop-poles',32),
      'CT07':('ct07_stability_margins','CT07-stability-margins',64)}


def plant(d,prefix,source,y):
    for state,x in [('x',72),('y',105)]:
        d.element(prefix+state,'EulerIntegrator',dict(initial_value=0),x,y,'First lag state x' if state=='x' else 'Output y; both lags τ=1 s')
        d.element(prefix+state+'_rate','Adder',dict(weights=[1,-1]),x-16,y,'Derivative = input − old state')
        wire(d,source if state=='x' else prefix+'x',prefix+state+'_rate')
        wire(d,prefix+state,prefix+state+'_rate',1)
        wire(d,prefix+state+'_rate',prefix+state)


def make(lab):
    name,slug,seconds=LABS[lab];d=Drawing();margins=lab=='CT07'
    d.element('gain','Constant',dict(value=4),4,95 if margins else 62,'Loop gain K: edit 4 → 16')
    y=72 if margins else 36;prefix='closed_' if margins else ''
    d.element('reference','StepGenerator',dict(initial_value=0,final_value=.5 if margins else 1,start_time=1),4,y,'Step reference at source time 1 s')
    d.element('error','Adder',dict(weights=[1,-1]),22,y,'Negative feedback: r − y')
    d.element('command','Multiply',{},39,y,'Ideal command K × error; no limiter')
    wire(d,'reference','error');wire(d,prefix+'y','error',1,bends=([119,y+17],[19,y+17],[19,y+4]))
    wire(d,'error','command');wire(d,'gain','command',1);plant(d,prefix,'command',y)
    if margins:
        d.element('pulse','ImpulseGenerator',dict(area=.1,start_time=.8),4,36,'One unit-height pulse at n=8')
        d.element('open_command','Multiply',{},39,36,'Same K; open-loop input')
        wire(d,'pulse','open_command');wire(d,'gain','open_command',1);plant(d,'open_','open_command',36)
        scope(d,'scope',[('reference','reference',0),('closed_output','closed_y',0)],122,72,'Closed-loop step: stability cross-check')
        scope(d,'pulse_scope',[('pulse','pulse',0),('open_output','open_y',0)],122,36,'Open-loop pulse and complete tail')
        d.nodes.append(dict(id='response',position=[89,102],observer=True,type='FrequencyResponse',title='Open-loop Bode / Nyquist; reference gain 1',params=dict(channel_units=['1','1'])))
        wire(d,'pulse','response',observer=True);wire(d,'open_y','response',1,observer=True)
        title='How much gain or phase reserve does this loop have?'
        details='Run 64 s at 10 Hz. Open Frequency response: Bode and Nyquist. Gain reference must be 1; keep phase unwrapped.'
        prediction='L(z)=0.01K/(z−0.9)². At K=4, gain margin=19/4; at K=16, 19/16. DC is excluded from the log-frequency Bode plot, but retained in Nyquist/export.'
    else:
        scope(d,'scope',[('reference','reference',0),('first_state','x',0),('output','y',0)],122,36,'Step and both causal states')
        scope(d,'command_scope',[('command','command',0),('error','error',0)],72,64,'Ideal command and tracking error')
        title='Can increasing feedback gain move stable poles outside the unit circle?'
        details='Run 32 s at 10 Hz. Open both Scopes. The two forward-Euler lag states supply causal memory; command has no actuator limiter.'
        prediction='Closed poles z=0.9 ± 0.1i√K. K=4 is stable; K=16 rings longer; K=19 is marginal, K=25 grows. Stability is |z|<1, not Re(z)<0.'
    notes=(f'**{lab} — {title}**  \n**Predict → Run → Inspect → Change gain.** {details}  \n'
           f'{prediction}  \nSelect gain, F4: value 4 → 16; save/reopen/rerun. All signals use normalized unit 1.  \n'
           'Source/state sample n is observed at Scope time (n+1)/10 s. Each lag updates from the same old states; do not add a feedback delay.  \n'
           f'[Open {lab} student lab](qrc:/s7studio/help/education/labs/{slug}/README.md)'
           f' · [MIT root locus]({MIT.format(11)}) · [MIT stability margins]({MIT.format(23)})')
    for node in d.nodes: node['position']=[.65*node['position'][0],.65*node['position'][1]+5]
    for w in d.wires:w['bends']=[[.65*x,.65*y+5] for x,y in w['bends']]
    return dict(id=name,document_kind='s7.diagram',dt=.1,
        studio_run=dict(runtime_seconds=seconds,clear_scope_traces=True),sheet=dict(orientation='landscape',width_mm=515,height_mm=455 if margins else 355),
        schema=dict(name=f'{lab} — {title}',dt=.1,elements=d.elements,connections=d.connections),probes=[],
        diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=d.nodes,wires=d.wires,
                     text_frames=[dict(id='notes',position=[2,1],params=dict(text=notes,text_format='markdown',width=1700,height=440,font_size=20))]))

if __name__=='__main__':
    for lab,(name,*_) in LABS.items():diagram_path(name+'.s7diagram').write_text(json.dumps(make(lab),indent=2)+'\n')
