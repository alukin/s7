#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Private FFmpeg receive/verify/view worker for the whole-frame spike."""
import hashlib
import json
from pathlib import Path
import resource
import subprocess
import sys
import threading
import time


def main():
    c = json.loads(Path(sys.argv[1]).read_text())
    root = Path(c['directory'])
    codec = c['codec']
    if codec not in ('h264', 'jpeg', 'raw') or c['processing'] not in ('none', 'grayscale'):
        raise ValueError('unsupported codec/processing')
    n = c['images']
    if type(n) is not int or not 3 <= n <= 3600:
        raise ValueError('invalid image count')
    prefix = ['ffmpeg', '-hide_banner', '-nostats', '-loglevel', 'info', '-benchmark', '-progress', 'pipe:1',
              '-stats_period', '0.5', '-filter_complex_threads', '1', '-probesize', '65536', '-analyzeduration', '100000']
    if codec == 'h264':
        prefix += ['-c:v', 'libopenh264']
    transform = 'hue=s=0,' if c['processing'] == 'grayscale' else ''
    filters = f"[0:v]{transform}split=2[all][picture];[all]format=yuv420p[checked];[picture]select='eq(n,{n-1})'[last]"
    container = 'mpegts' if codec == 'h264' else 'nut'
    cmd = [*prefix, '-f', container, '-i', 'pipe:0', '-filter_complex', filters,
           '-map', '[checked]', '-an', '-fps_mode', 'passthrough', '-c:v', 'rawvideo', '-threads:v', '1', '-f', 'framehash', '-hash', 'sha256',
           str(root/'decoded.sha256'), '-map', '[last]', '-fps_mode', 'passthrough', '-c:v', 'png', '-threads:v', '1',
           '-frames:v', '1', str(root/'last.png')]
    log = open(root/'decoder.log', 'w')
    began = time.monotonic()
    decoder = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=False)
    progress = []
    def collect():
        fields = {}
        for line in iter(decoder.stdout.readline, b''):
            line = line.decode().strip()
            if '=' not in line:
                continue
            k,v = line.split('=',1);fields[k] = v
            if k == 'progress':
                progress.append(dict(fields, observed_seconds=time.monotonic()-began))
                fields = {}
    collector = threading.Thread(target=collect, daemon=True);collector.start()
    player = None
    if c.get('view'):
        play = ['ffplay', '-hide_banner', '-loglevel', 'warning', '-stats', '-probesize', '65536', '-analyzeduration', '100000',
                '-an', '-sn', '-sync', 'video', '-noframedrop', '-window_title', 'S7 FFmpeg Full HD',
                '-noborder', '-x', str(c['width']), '-y', str(c['height']), '-left', '0', '-top', '0']
        if codec == 'h264':
            play += ['-vcodec', 'libopenh264']
        if c['processing'] == 'grayscale':
            play += ['-vf', 'hue=s=0']
        player = subprocess.Popen([*play, '-i', 'pipe:0'], stdin=subprocess.PIPE,
                                  stdout=subprocess.DEVNULL, stderr=open(root/'viewer.log','w'))
    digest = hashlib.sha256();count = 0
    print(json.dumps(dict(ready=True)), flush=True)
    try:
        while True:
            block = sys.stdin.buffer.read1(65536)
            if not block:
                break
            digest.update(block);count += len(block)
            decoder.stdin.write(block)
            decoder.stdin.flush()
            if player:
                player.stdin.write(block)
                player.stdin.flush()
        decoder.stdin.close()
        if player:
            player.stdin.close()
        if decoder.wait(timeout=30):
            raise RuntimeError('FFmpeg decoding failed; inspect decoder.log')
        collector.join(timeout=5)
        if not progress or any(int(progress[-1].get(k,'0')) for k in ('dup_frames','drop_frames')):
            raise RuntimeError('missing progress or duplicated/dropped decoded frames')
        content = (root/'decoded.sha256').read_text()
        rows = [line for line in content.splitlines() if line and not line.startswith('#')]
        if len(rows) != n:
            raise RuntimeError(f'decoded {len(rows)} images, expected {n}')
        pts = [int(line.split(',')[2]) for line in rows]
        if any(a >= b for a,b in zip(pts,pts[1:])):
            raise RuntimeError('decoded timestamps not strictly increasing')
        fields = {}
        for line in content.splitlines():
            if line.startswith('#dimensions'):
                fields['dimensions'] = line.split(':',1)[1].strip()
        if fields.get('dimensions') != f'{c["width"]}x{c["height"]}':
            raise RuntimeError('wrong decoded image dimensions')
        usage = resource.getrusage(resource.RUSAGE_CHILDREN)
        result = dict(frames=len(rows), dimensions=fields['dimensions'], input_bytes=count, input_sha256=digest.hexdigest(),
                      wall_seconds=time.monotonic()-began, decoder_cpu_seconds=usage.ru_utime+usage.ru_stime,
                      decoder_peak_rss_bytes=usage.ru_maxrss*1024, progress=progress,
                      framehash_sha256=hashlib.sha256(content.encode()).hexdigest(),
                      unique_decoded_images=len({line.split(',')[-1].strip() for line in rows}),
                      command=cmd, viewer_alive=player.poll() is None if player else None)
        # Keep the viewer alive after EOF for the controller's screen capture.
        (root/'receiver-result.json').write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result),flush=True)
        if player:
            deadline = time.monotonic()+15
            while player.poll() is None and not (root/'stop').exists() and time.monotonic()<deadline:
                time.sleep(.1)
    finally:
        if decoder.poll() is None:
            decoder.kill();decoder.wait()
        if player and player.poll() is None:
            player.terminate()
            try:player.wait(timeout=3)
            except subprocess.TimeoutExpired:player.kill();player.wait()
        log.close()


if __name__=='__main__':
    try:main()
    except Exception as e:
        print('FFmpeg receiver: '+str(e),file=sys.stderr)
        try:
            log=Path(json.loads(Path(sys.argv[1]).read_text())['directory'])/'decoder.log'
            if log.exists():print(log.read_text()[-6000:],file=sys.stderr)
        except (OSError,ValueError,KeyError):pass
        raise SystemExit(1)
