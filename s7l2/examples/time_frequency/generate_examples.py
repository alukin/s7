#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
import json, math, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root/'examples'))
from refine_analysis_examples import refine
p=root/'s7core/resources/schema_elements.s7library';library=json.loads(p.read_text())
nodes=[];connections=[]
def node(id,type,params=None):nodes.append(dict(id=id,type=type,params=params or {}));return id
def wire(a,b,p=0,ap=0):connections.append(dict(from_element=a,from_port=ap,to_element=b,to_port=p))
node('omega','Gain',{'factor':1});node('damping','Gain',{'factor':2});node('difference','Adder',{'weights':[1,-1]});node('omega2','Multiply');wire('omega','omega2');wire('omega','omega2',1)
node('drive','Multiply');wire('difference','drive');wire('omega2','drive',1)
node('loss_factor','Multiply');wire('omega','loss_factor');wire('damping','loss_factor',1)
node('loss','Multiply');wire('loss_factor','loss');node('velocity','EulerIntegrator',{'initial_value':0});node('position','EulerIntegrator',{'initial_value':0});wire('velocity','loss',1)
node('acceleration','Adder',{'weights':[1,-1]});wire('drive','acceleration');wire('loss','acceleration',1);wire('acceleration','velocity');wire('velocity','position');wire('position','difference',1)
schema=dict(name='SecondOrderLowPass',elements=nodes,connections=connections,exposed_inputs=[dict(name=n,type='F64',element=e,port=0) for n,e in [('input','difference'),('omega0','omega'),('damping_ratio','damping')]],exposed_outputs=[dict(name='output',type='F64',element='position',port=0)])
library['elements']=[e for e in library['elements'] if e['type_name']!='SecondOrderLowPass']
library['elements'].append(dict(type_name='SecondOrderLowPass',display_name='Second-order Low-pass Plant',description='Editable zero-state analog-computer graph: x double-prime = omega0²(input-x)-2*zeta*omega0*x prime. F64 input, omega0 (rad/s), damping ratio, output. Two explicit Euler integrators; resolve fastest dynamics at the run rate.',category='schema_elements',version='1.0.0',schema=schema));p.write_text(json.dumps(library,indent=2)+'\n')
folder=root/'examples/time_frequency';folder.mkdir(exist_ok=True)
for name in ['tone','tone_bursts','overlapping_tones','chirp','impulse','ring_down','resonant_chirp']:
 es=[];cs=[]
 def add(id,type,params=None):es.append(dict(id=id,type=type,params=params or {}));return id
 def connect(a,b,port=0):cs.append(dict(from_element=a,from_port=0,to_element=b,to_port=port))
 if name in ['impulse','ring_down']:out=add('excitation','ImpulseGenerator',dict(area=1,start_time=1.5))
 elif name in ['chirp','resonant_chirp']:out=add('excitation','ChirpGenerator',dict(amplitude=1,start_hz=4,end_hz=90,sweep_time=8))
 else:out=add('tone','AnalogOscillator',dict(amplitude=1,frequency_hz=32))
 if name=='tone_bursts':
  add('gate','PulseGenerator',dict(low=0,high=1,frequency_hz=.5,duty=.25));add('burst','Multiply');connect(out,'burst');connect('gate','burst',1);out='burst'
 if name=='overlapping_tones':
  add('second','AnalogOscillator',dict(amplitude=.6,frequency_hz=64));add('sum','Adder');connect(out,'sum');connect('second','sum',1);out='sum'
 if name in ['ring_down','resonant_chirp']:
  add('plant','SecondOrderLowPass');add('omega','Constant',dict(value=2*math.pi*16));add('zeta','Constant',dict(value=.2));connect(out,'plant');connect('omega','plant',1);connect('zeta','plant',2);out='plant'
 doc=json.loads((root/'examples/surfaces/plane.s7diagram').read_text());doc['id']='time_frequency_'+name;doc['schema']=dict(name=doc['id'],elements=es,connections=cs)
 dn=[dict(id=e['id'],position=[4+i*20,20],title=e['id']) for i,e in enumerate(es)];dn.append(dict(id='map',position=[95,42],observer=True,type='TimeFrequency',params={}))
 wires=[]
 for c in cs+[dict(from_element=out,from_port=0,to_element='map',to_port=0)]:wires.append(dict(id='w'+str(len(wires)),**{'from':dict(port=[c['from_element'],'out',c['from_port']]),'to':dict(port=[c['to_element'],'in',c['to_port']])},bends=[]))
 doc['diagram']['nodes']=dn;doc['diagram']['wires']=wires;doc['diagram']['text_frames'][0]['params']['text']=f'{name}: time–frequency laboratory\nRun 8 model seconds at 1024 Hz. Open map; choose STFT or Morlet CWT and Analyze/freeze.\nCWT default 2–100 Hz, 32 scales. Gray map cells have incomplete support.\nSee examples/time_frequency/README.md for expected results.';doc['dt']=1/1024
 (folder/(name+'.s7diagram')).write_text(json.dumps(doc,indent=2)+'\n')
 refine(folder/(name+'.s7diagram'))
# Add a time-frequency observer to the existing, causal PLL experiment.
base=json.loads((root/'examples/pll/pll_frequency_step.s7diagram').read_text())
probe=next((v for v in base.get('probes',[]) if 'error' in v['name']),base.get('probes',[{}])[0])
if probe:
 base['id']='pll_acquisition_time_frequency';base['diagram']['nodes'].append(dict(id='wavelet_map',position=[100,60],observer=True,type='TimeFrequency',params={}))
 base['diagram']['wires'].append(dict(id='wavelet_observation',**{'from':dict(port=[probe['element'],'out',probe.get('port',0)]),'to':dict(port=['wavelet_map','in',0])},bends=[]))
 (folder/'pll_acquisition.s7diagram').write_text(json.dumps(base,indent=2)+'\n')
 refine(folder/'pll_acquisition.s7diagram')
