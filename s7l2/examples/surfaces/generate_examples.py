#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
import json, math
from pathlib import Path
root=Path(__file__).resolve().parents[2]
library_path=root/'s7core/resources/schema_elements.s7library'
library=json.loads(library_path.read_text())
class Graph:
 def __init__(self): self.e=[]; self.c=[]; self.inputs=[]
 def node(self,id,t,params=None): self.e.append(dict(id=id,type=t,params=params or {})); return (id,0)
 def wire(self,s,id,p=0): self.c.append(dict(from_element=s[0],from_port=s[1],to_element=id,to_port=p))
 def op(self,id,t,sources,params=None):
  out=self.node(id,t,params)
  for i,s in enumerate(sources): self.wire(s,id,i)
  return out
 def input(self,name,target,port=0): self.inputs.append(dict(name=name,type='F64',element=target,port=port))
 def schema(self,name,outs): return dict(name=name,elements=self.e,connections=self.c,exposed_inputs=self.inputs,exposed_outputs=[dict(name=n,type='F64',element=s[0],port=s[1]) for n,s in outs])

def graph(name):
 g=Graph(); x=g.node('x','Gain',dict(factor=1)); y=g.node('y','Gain',dict(factor=1));g.input('x','x');g.input('y','y')
 if name=='plane':
  z=g.op('sum','Adder',[x,y],dict(weights=[1,2])); outs=[('z',z)]
 elif name=='wave':
  a=g.op('sin_x','Trig',[x],dict(function='sin'));b=g.op('cos_y','Trig',[y],dict(function='cos'));z=g.op('product','Multiply',[a,b]);outs=[('z',z)]
 elif name=='identity': outs=[('re',x),('im',y)]
 else:
  xx=g.op('xx','Multiply',[x,x]); yy=g.op('yy','Multiply',[y,y])
  if name=='square':
   re=g.op('re','Adder',[xx,yy],dict(weights=[1,-1]));xy=g.op('xy','Multiply',[x,y]);im=g.op('im','Gain',[xy],dict(factor=2));outs=[('re',re),('im',im)]
  elif name=='rational':
   one=g.node('one','Constant',dict(value=1));two_x=g.op('two_x','Gain',[x],dict(factor=2));r2=g.op('r2','Adder',[xx,yy]);den=g.op('den','Adder',[r2,two_x,one],dict(weights=[1,1,1]));num=g.op('num','Adder',[r2,one],dict(weights=[1,-1]));re=g.op('re','Divider',[num,den]);two_y=g.op('two_y','Gain',[y],dict(factor=2));im=g.op('im','Divider',[two_y,den]);outs=[('re',re),('im',im)]
  elif name=='saddle': z=g.op('difference','Adder',[xx,yy],dict(weights=[1,-1]));outs=[('z',z)]
  else:
   r2=g.op('radius_squared','Adder',[xx,yy])
   if name=='bowl': outs=[('z',r2)]
   elif name=='gaussian':
    neg=g.op('negative','Gain',[r2],dict(factor=-1));z=g.op('exponential','MathFunction',[neg],dict(function='exp'));outs=[('z',z)]
   elif name=='sinc':
    radius=g.op('radius','MathFunction',[r2],dict(function='sqrt'));sine=g.op('sine','Trig',[radius],dict(function='sin'));z=g.op('ratio','Divider',[sine,radius]);outs=[('z',z)]
   elif name=='inverse':
    re=g.op('re','Divider',[x,r2]);neg=g.op('negative_y','Gain',[y],dict(factor=-1));im=g.op('im','Divider',[neg,r2]);outs=[('re',re),('im',im)]
 return g.schema('Surface'+name.title(),outs)

settings=[('plane',2,65,49,'x + 2y'),('bowl',2,65,65,'x² + y²'),('saddle',2,65,65,'x² − y²'),('gaussian',3,65,65,'exp(−x²−y²)'),('wave',math.pi,65,65,'sin(x) cos(y)'),('sinc',10,64,64,'sin(r)/r; r=√(x²+y²), grid excludes r=0'),('identity',2,65,65,'f(z)=z'),('square',2,65,65,'f(z)=z²'),('inverse',2,64,64,'f(z)=1/z; grid excludes pole'),('rational',2,64,64,'f(z)=(z−1)/(z+1); grid excludes pole')]
for name,extent,nx,ny,formula in settings:
 complex_=name in ('identity','square','inverse','rational'); type_name='Surface'+name.title(); schema=graph(name)
 library['elements']=[e for e in library['elements'] if e['type_name']!=type_name]
 library['elements'].append(dict(type_name=type_name,display_name='Surface: '+name.title(),description=formula+'; editable stateless F64 schema, inputs x/y. '+('Requires nonzero denominator; use a pole-avoiding domain.' if name in ('sinc','inverse','rational') else ''),category='schema_elements',version='1.0.0',schema=schema))
 folder=root/'examples'/('complex_functions' if complex_ else 'surfaces'); folder.mkdir(exist_ok=True)
 elements=[dict(id='grid',type='GridScan',params=dict(x_min=-extent,x_max=extent,y_min=-extent,y_max=extent,columns=nx,rows=ny)),dict(id='function',type=type_name,params={})]
 connections=[dict(from_element='grid',from_port=i,to_element='function',to_port=i) for i in (0,1)]
 nodes=[dict(id='grid',position=[4,16],title='Coordinates and explicit cell identity'),dict(id='function',position=[46,12],title=formula),dict(id='surface',position=[84,16],observer=True,type='ComplexSurface3D' if complex_ else 'Surface3D',params={})]
 wires=[]
 def wire(s,sp,t,tp): wires.append(dict(id='w'+str(len(wires)+1),**{'from':dict(port=[s,'out',sp]),'to':dict(port=[t,'in',tp])},bends=[]))
 for i in (0,1): wire('grid',i,'function',i)
 for i in range(6): wire('grid',i,'surface',i)
 wire('function',0,'surface',6)
 if complex_:wire('function',1,'surface',7)
 note=f'{formula}\nRun at 1 kHz for {math.ceil((nx*ny+1)/1000)} s; open surface. {nx} × {ny} samples, X fastest.\nCoordinates are dimensionless. Gray cells are missing; complex gray also marks undefined phase at zero.\nSchema definition is editable in the standard schema library. See this folder README.'
 doc=dict(id='surface_'+name,dt=.001,document_kind='s7.diagram',diagram_version=2,sheet=dict(orientation='landscape',width_mm=560,height_mm=220),schema=dict(name='surface_'+name,elements=elements,connections=connections),probes=[],diagram=dict(grid=dict(step=20,mm_per_step=5.291666666666667),nodes=nodes,wires=wires,text_frames=[dict(id='notes',position=[2,2],params=dict(text=note,width=1900,height=190,font_size=10))]))
 (folder/(name+'.s7diagram')).write_text(json.dumps(doc,indent=2,ensure_ascii=False)+'\n')
library_path.write_text(json.dumps(library,indent=2,ensure_ascii=False)+'\n')
