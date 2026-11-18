#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in whole-frame FFmpeg experiment. SSH protects the media stream.

Synthetic input is native at the requested dimensions. Camera input uses an
explicitly temporary PipeWire/GStreamer MJPEG bridge, then FFmpeg upscales 720p.
No S7 protocol/runtime/deployment contract or installed service is changed.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def progress(path):
    records=[];fields={}
    for line in Path(path).read_text().splitlines():
        if '=' not in line:continue
        k,v=line.split('=',1);fields[k]=v
        if k=='progress':records.append(fields);fields={}
    return records


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',default='win.local');p.add_argument('--key-alias',default='win')
    p.add_argument('--local',action='store_true');p.add_argument('--output',type=Path,required=True)
    p.add_argument('--source',choices=('synthetic','camera-upscaled'),default='synthetic')
    p.add_argument('--codec',choices=('h264','jpeg','raw'),default='h264')
    p.add_argument('--width',type=int,default=1920);p.add_argument('--height',type=int,default=1080)
    p.add_argument('--fps',type=int,default=30);p.add_argument('--images',type=int,default=300)
    p.add_argument('--bitrate-kbps',type=int,default=6000)
    p.add_argument('--jpeg-q',type=int,default=5)
    p.add_argument('--processing',choices=('none','grayscale'),default='none')
    p.add_argument('--view',action='store_true')
    a=p.parse_args();a.output=a.output.resolve()
    if a.output.exists():p.error('output directory must be new')
    if a.host.startswith('-') or any(c.isspace() for c in a.host):p.error('invalid host')
    if not (2<=a.width<=1920 and 2<=a.height<=1080 and a.width%2==a.height%2==0 and 1<=a.fps<=60 and
            3<=a.images<=3600 and 64<=a.bitrate_kbps<=100000 and 2<=a.jpeg_q<=31):p.error('invalid size/rate/codec settings')
    if a.source=='camera-upscaled' and (a.width,a.height,a.fps)!=(1920,1080,30):p.error('camera test explicitly scales 720p/30 to 1080p/30')
    if a.view and not shutil.which('Xvfb'):p.error('local Xvfb is needed for the temporary receiver display')
    a.output.mkdir(parents=True,mode=0o700)
    opts=['-o','BatchMode=yes','-o','ConnectTimeout=10','-o','HostKeyAlias='+a.key_alias]
    worker=Path(__file__).with_name('ffmpeg_frame_receiver.py').resolve()
    container='mpegts' if a.codec=='h264' else 'nut'
    record=dict(status='running',source=a.source,codec=a.codec,width=a.width,height=a.height,container=container,
                nominal_fps=a.fps,images=a.images,bitrate_kbps=a.bitrate_kbps,jpeg_q=a.jpeg_q,processing=a.processing,
                transport='Media container over private SSH stdin; no S7 wire/backend qualification',
                host_b='local' if a.local else a.host,driver_sha256=digest(__file__),worker_sha256=digest(worker),
                physical_display_qualified=False)
    children=[];logs=[];remote=None;failure=None
    def command(args,b=False):
        args=list(map(str,args))
        return ['ssh',*opts,a.host,shlex.join(args)] if b and not a.local else args
    def run(args,b=False,**kw):return subprocess.run(command(args,b),check=True,**kw)
    def version(b=False):
        return run(['ffmpeg','-version'],b,capture_output=True,text=True,timeout=15).stdout.splitlines()[0]
    def launch(args,name,b=False,**kw):
        log=open(a.output/(name+'.log'),'w');logs.append(log)
        args=list(map(str,args))
        if b and not a.local:
            wrapper='import os,sys;os.setpgid(0,0) if os.getpgrp()!=os.getpid() else None;open(sys.argv[1],"w").write(str(os.getpid()));os.execvp(sys.argv[2],sys.argv[2:])'
            args=['python3','-c',wrapper,remote+'/'+name+'.pid',*args]
        child=subprocess.Popen(command(args,b),stdout=subprocess.PIPE,stderr=log,start_new_session=True,**kw)
        children.append(child);return child
    def line(child,timeout=30):
        if not select.select([child.stdout],[],[],timeout)[0]:raise TimeoutError('worker report timeout')
        data=child.stdout.readline()
        if not data:raise RuntimeError('worker exited; inspect logs')
        return json.loads(data)
    with tempfile.TemporaryDirectory(prefix='s7-ffmpeg-frame-') as tmp:
        root=Path(tmp);stage=root/'B';stage.mkdir()
        try:
            record['ffmpeg_versions']={'sender':version(), 'receiver':version(True)}
            if a.source=='camera-upscaled':
                record['camera_input']={'width':1280,'height':720,'format':'MJPEG','requested_fps':30,
                    'timestamp_policy':'MJPEG pipe assigns nominal 30 fps; original capture timestamps are not retained'}
            if not a.local:
                remote=subprocess.check_output(['ssh',*opts,a.host,'mktemp -d /tmp/s7-ffmpeg-frame.XXXXXX'],text=True).strip()
                if not remote.startswith('/tmp/s7-ffmpeg-frame.') or not remote.replace('/','').replace('-','').replace('.','').isalnum():raise RuntimeError('unexpected remote path')
                stage=Path(remote)
                subprocess.run(['scp',*opts,'-q',str(worker),a.host+':'+remote+'/'],check=True)
            else:shutil.copy2(worker,stage/worker.name)
            env=[];display=None
            if a.view:
                server=Path(shutil.which('Xvfb'))
                if not a.local:
                    bundle=root/'xserver';bundle.mkdir();shutil.copy2(server,bundle/'Xvfb')
                    for l in subprocess.check_output(['ldd',str(server)],text=True).splitlines():
                        f=l.split()
                        if len(f)>2 and f[1]=='=>' and f[2].startswith('/'):
                            path=Path(f[2])
                            if not path.name.startswith(('libc.so','libm.so','libdl.so','libpthread.so','librt.so')):shutil.copy2(path,bundle/path.name)
                    subprocess.run(['scp',*opts,'-qr',str(bundle),a.host+':'+remote+'/'],check=True)
                    server=stage/'xserver/Xvfb';extra=['env','LD_LIBRARY_PATH='+str(stage/'xserver')]
                else:extra=[]
                x=launch([*extra,server,'-displayfd','1','-screen','0','2048x1152x24','-nolisten','tcp'],'xserver',True,stdin=subprocess.DEVNULL)
                display=':'+str(line(x));env=['env','DISPLAY='+display,'SDL_RENDER_DRIVER=software']
            c=dict(directory=str(stage),codec=a.codec,processing=a.processing,images=a.images,
                   width=a.width,height=a.height,view=a.view)
            (root/'receiver.json').write_text(json.dumps(c))
            if not a.local:subprocess.run(['scp',*opts,'-q',str(root/'receiver.json'),a.host+':'+remote+'/'],check=True)
            else:shutil.copy2(root/'receiver.json',stage/'receiver.json')
            receiver=launch([*env,'python3','-B',stage/worker.name,stage/'receiver.json'],'receiver',True,stdin=subprocess.PIPE)
            if not line(receiver).get('ready'):raise RuntimeError('receiver did not start')
            prefix=['ffmpeg','-hide_banner','-nostdin','-nostats','-loglevel','info','-benchmark','-stats_period','0.5',
                    '-progress',str(a.output/'sender-progress.txt')]
            bridge=None
            if a.source=='synthetic':
                source=['-readrate','1','-readrate_initial_burst','0','-f','lavfi','-i',f'testsrc2=size={a.width}x{a.height}:rate={a.fps}']
            else:
                bridge=launch(['gst-launch-1.0','-q','pipewiresrc','client-name=S7-FFmpeg-Capture-Bridge',
                    'stream-properties=props,node.name=s7.video.ffmpeg.capture,node.description=S7-FFmpeg-Webcam',
                    'target-object=v4l2_input.pci-0000_04_00.4-usb-0_3_1.0','num-buffers='+str(a.images),
                    '!','image/jpeg,width=1280,height=720,framerate=30/1','!','fdsink','fd=1','sync=false'],
                    'capture',stdin=subprocess.DEVNULL)
                source=['-f','mjpeg','-framerate','30','-i','pipe:0','-vf',f'scale={a.width}:{a.height}:flags=bilinear']
            encoder={'h264':['-c:v','libx264','-preset','ultrafast','-tune','zerolatency','-threads:v','2',
                             '-b:v',str(a.bitrate_kbps)+'k','-g',str(a.fps),'-bf','0','-profile:v','baseline'],
                     'jpeg':['-c:v','mjpeg','-q:v',str(a.jpeg_q),'-threads:v','2'],
                     'raw':['-c:v','rawvideo','-threads:v','1']}[a.codec]
            args=[*prefix,*source,'-an','-frames:v',str(a.images),'-pix_fmt','yuv420p',*encoder,'-fps_mode','passthrough','-f',container,'pipe:1']
            record['sender_command']=args
            began=time.monotonic()
            sender=launch(args,'sender',stdin=bridge.stdout if bridge else subprocess.DEVNULL)
            if bridge:bridge.stdout.close()
            hashed=hashlib.sha256();size=0
            deadline=began+max(60,a.images/a.fps*5+30)
            os.set_blocking(receiver.stdin.fileno(),False)
            while True:
                if time.monotonic()>deadline:raise TimeoutError('media relay deadline exceeded')
                if not select.select([sender.stdout],[],[],1)[0]:continue
                block=sender.stdout.read1(65536)
                if not block:break
                remaining=memoryview(block)
                while remaining:
                    if time.monotonic()>deadline:raise TimeoutError('media receiver backpressure deadline exceeded')
                    if not select.select([],[receiver.stdin],[],1)[1]:continue
                    try:sent=os.write(receiver.stdin.fileno(),remaining)
                    except BlockingIOError:continue
                    remaining=remaining[sent:]
                hashed.update(block);size+=len(block)
            if sender.wait(timeout=15):raise RuntimeError('FFmpeg encoder failed; inspect sender.log')
            if bridge and bridge.wait(timeout=10):raise RuntimeError('PipeWire capture bridge failed')
            receiver.stdin.close()
            record['sender_wall_seconds']=time.monotonic()-began
            record['sender_progress']=progress(a.output/'sender-progress.txt')
            if not record['sender_progress'] or int(record['sender_progress'][-1]['frame'])!=a.images:raise RuntimeError('incomplete encoded frame count')
            if any(int(record['sender_progress'][-1].get(k,'0')) for k in ('dup_frames','drop_frames')):raise RuntimeError('encoder duplicated/dropped frames')
            record['receiver']=line(receiver,45)
            record['transmitted_bytes']=size;record['transmitted_sha256']=hashed.hexdigest()
            if record['receiver']['input_bytes']!=size or record['receiver']['input_sha256']!=hashed.hexdigest():raise RuntimeError('media stream digest mismatch')
            if a.view:
                if not record['receiver']['viewer_alive']:raise RuntimeError('FFplay did not stay alive for inspection')
                time.sleep(1)
                run(['env','DISPLAY='+display,'gst-launch-1.0','-q','ximagesrc','display-name='+display,'use-damage=false',
                     'num-buffers=1','!','videoconvert','!','pngenc','!','filesink','location='+str(stage/'screen.png')],True,timeout=15)
                run(['touch',stage/'stop'],True)
            if receiver.wait(timeout=20):raise RuntimeError('receiver worker failed')
            for name in ['receiver-result.json','decoder.log','decoded.sha256','last.png']+(['screen.png','viewer.log'] if a.view else []):
                if not a.local:subprocess.run(['scp',*opts,'-q',a.host+':'+str(stage/name),str(a.output/name)],check=True)
                else:shutil.copy2(stage/name,a.output/name)
            record['status']='passed'
        except Exception as error:
            failure=error;record['status']='failed';record['error']=str(error)
        finally:
            for child in reversed(children):
                if child.poll() is None:
                    os.killpg(child.pid,signal.SIGTERM)
                    try:child.wait(timeout=3)
                    except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
            if remote:
                cleanup='''import glob,os,pathlib,signal,sys,time
d=sys.argv[1];pids=[]
for f in glob.glob(d+'/*.pid'):
 try:
  p=int(pathlib.Path(f).read_text())
  if d.encode() in pathlib.Path(f'/proc/{p}/cmdline').read_bytes() and os.getpgid(p)==p:pids.append(p)
 except (OSError,ValueError):pass
for s in (signal.SIGTERM,signal.SIGKILL):
 for p in pids:
  try:os.killpg(p,s)
  except ProcessLookupError:pass
 time.sleep(.2)
'''
                run(['python3','-c',cleanup,remote],True);run(['rm','-rf',remote],True)
            for log in logs:log.close()
            (a.output/'result.json').write_text(json.dumps(record,indent=2)+'\n')
    if failure:raise SystemExit(str(failure)+'; logs: '+str(a.output))
    print(json.dumps(dict(status='passed',source=a.source,codec=a.codec,dimensions=record['receiver']['dimensions'],
          frames=record['receiver']['frames'],sender_wall_seconds=record['sender_wall_seconds'],bytes=size)))


if __name__=='__main__':main()
