#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in V0: numeric video through real S7 graphs/mTLS on two processes/hosts.

GStreamer supplies the PipeWire device/viewer adapters in this spike only.
No installed service, deployment contract, firewall or desktop is changed.
Live images and ephemeral credentials are never put in the source tree.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import shutil
import signal
import socket
import subprocess
import tempfile
import threading
import time
import uuid

from run_cross_node_s7_spikes import credentials, digest


class ByteProxy:
    """Counts the complete encrypted mTLS flows, including their handshakes."""
    def __init__(self, target):
        self.target = target
        self.bytes = [0, 0]
        self.sockets = []
        self.closed = threading.Event()
        self.server = socket.socket()
        self.server.bind(('127.0.0.1', 0))
        self.server.listen(8)
        self.server.settimeout(.2)
        self.port = self.server.getsockname()[1]
        self.thread = threading.Thread(target=self.accept, daemon=True)
        self.thread.start()

    def accept(self):
        while not self.closed.is_set():
            try:
                client, _ = self.server.accept()
            except (TimeoutError, OSError):
                continue
            upstream = socket.create_connection(('127.0.0.1', self.target), timeout=10)
            client.settimeout(None)
            upstream.settimeout(None)
            self.sockets.extend((client, upstream))
            for src, dst, direction in ((client, upstream, 1), (upstream, client, 0)):
                threading.Thread(target=self.copy, args=(src, dst, direction), daemon=True).start()

    def copy(self, src, dst, direction):
        try:
            while not self.closed.is_set():
                data = src.recv(262144)
                if not data:
                    break
                self.bytes[direction] += len(data)
                dst.sendall(data)
        except OSError:
            pass
        finally:
            try:
                dst.shutdown(socket.SHUT_WR)
            except OSError:
                pass

    def close(self):
        self.closed.set()
        self.server.close()
        for stream in self.sockets:
            stream.close()
        self.thread.join(timeout=2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host', help='SSH receiver; omit for a two-process local test')
    parser.add_argument('--key-alias', default='win')
    parser.add_argument('--source', choices=('synthetic', 'camera'), default='synthetic')
    parser.add_argument('--camera-target', default='v4l2_input.pci-0000_04_00.4-usb-0_3_1.0')
    parser.add_argument('--mode', choices=('scalar', 'staged', 'bulk'), default='scalar')
    parser.add_argument('--encoding', choices=('raw', 'lz4', 'zstd'), default='raw')
    parser.add_argument('--width', type=int, default=160)
    parser.add_argument('--height', type=int, default=120)
    parser.add_argument('--fps', type=int, default=30)
    parser.add_argument('--images', type=int, default=90)
    parser.add_argument('--unpaced', action='store_true', help='Synthetic capacity control; keep the declared model rate unchanged')
    parser.add_argument('--packet-pixels', type=int, default=4096)
    parser.add_argument('--window-packets', type=int, default=8)
    parser.add_argument('--pipewire-output', action='store_true', help='Export and consume the receiver PipeWire video node; save a PNG')
    parser.add_argument('--virtual-display', action='store_true', help='Render the receiver viewer on an isolated temporary Xvfb display')
    args = parser.parse_args()
    args.binary = args.binary.resolve()
    args.output = args.output.resolve()
    if not args.binary.is_file() or args.output.exists():
        parser.error('binary must exist and output directory must be new')
    if args.host and (args.host.startswith('-') or any(c.isspace() for c in args.host)):
        parser.error('invalid SSH host')
    if not (1 <= args.width <= 1920 and 1 <= args.height <= 1080 and 1 <= args.fps <= 120 and 1 <= args.images <= 3000):
        parser.error('invalid video size/rate/count')
    if args.source == 'camera' and (args.width % 2 or args.fps != 30):
        parser.error('this webcam spike explicitly requests even-width YUY2 at native 30 fps')
    if args.unpaced and args.source != 'synthetic':
        parser.error('--unpaced is a synthetic capacity control')
    if args.virtual_display and (not args.pipewire_output or not shutil.which('Xvfb')):
        parser.error('--virtual-display needs --pipewire-output and host Xvfb')
    args.output.mkdir(parents=True, mode=0o700)
    ssh_options = ['-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', '-o', 'HostKeyAlias=' + args.key_alias]
    children, logs, remote, master, proxy = [], [], None, None, None
    record = {'source': args.source, 'mode': args.mode, 'encoding': args.encoding,
              'width': args.width, 'height': args.height, 'requested_images_per_second': args.fps,
              'image_count': args.images, 'packet_pixels': args.packet_pixels, 'window_packets': args.window_packets,
              'synthetic_pacing': not args.unpaced,
              'host_b': args.host or 'local', 'binary_sha256': digest(args.binary),
              'driver_sha256': digest(__file__), 'cpp_source_sha256': digest(Path(__file__).parents[1] / 's7tests/video_stream_spike.cpp'),
              'pipewire_adapter': 'GStreamer spike adapter, not the production native provider',
              'physical_display_qualified': False}
    error = None
    with tempfile.TemporaryDirectory(prefix='s7-video-spike-') as directory:
        root = Path(directory)
        stages = [root / role for role in ('A', 'B')]
        for stage in stages:
            stage.mkdir(mode=0o700)
        record['credentials'] = credentials(stages)

        def command(argv, on_b=False):
            return ['ssh', *ssh_options, args.host, shlex.join(argv)] if args.host and on_b else list(map(str, argv))

        def run(argv, on_b=False, **kwargs):
            return subprocess.run(command(list(map(str, argv)), on_b), check=True, **kwargs)

        def launch(argv, name, on_b=False, controlled=False):
            log = open(args.output / (name + '.log'), 'w')
            logs.append(log)
            argv = list(map(str, argv))
            if on_b and args.host:
                # Isolate a group, record its owner, then exec with inherited I/O.
                wrapper = 'import os,sys;os.setpgid(0,0) if os.getpgrp()!=os.getpid() else None;open(sys.argv[1],"w").write(str(os.getpid()));os.execvp(sys.argv[2],sys.argv[2:])'
                argv = ['python3', '-c', wrapper, remote+'/'+name+'.pid', *argv]
            process = subprocess.Popen(command(argv, on_b), stdin=subprocess.PIPE if controlled else subprocess.DEVNULL,
                                       stdout=subprocess.PIPE if controlled else log, stderr=log, text=True, start_new_session=True)
            children.append(process)
            return process

        def line(process, timeout=30):
            import select
            if not select.select([process.stdout], [], [], timeout)[0]:
                raise TimeoutError('node report timeout')
            value = process.stdout.readline()
            if not value:
                raise RuntimeError('node exited; inspect node logs')
            return json.loads(value)

        try:
            receiver_stage = stages[1]
            receiver_binary = args.binary
            receiver_env = []
            if args.host:
                remote = subprocess.check_output(['ssh', *ssh_options, args.host, 'mktemp -d /tmp/s7-video-spike.XXXXXX'], text=True).strip()
                if not remote.startswith('/tmp/s7-video-spike.') or not remote.replace('/', '').replace('-', '').replace('.', '').isalnum():
                    raise RuntimeError('unexpected remote temporary directory')
                bundle = stages[1] / 'bundle'
                bundle.mkdir()
                shutil.copy2(args.binary, bundle / args.binary.name)
                record['libraries'] = {}
                executables = [args.binary]
                if args.virtual_display:
                    executables.append(Path(shutil.which('Xvfb')))
                    shutil.copy2(executables[-1], bundle/'Xvfb')
                for executable in executables:
                    dependencies = subprocess.check_output(['ldd', str(executable)], text=True)
                    for text in dependencies.splitlines():
                        fields = text.split()
                        if len(fields) > 2 and fields[1] == '=>' and fields[2].startswith('/'):
                            path = Path(fields[2])
                            if path.name.startswith(('libc.so', 'libm.so', 'libdl.so', 'libpthread.so', 'librt.so')):
                                continue
                            shutil.copy2(path, bundle / path.name)
                            record['libraries'][path.name] = digest(path)
                subprocess.run(['scp', *ssh_options, '-qr', str(bundle), str(stages[1]/'credentials'), args.host + ':' + remote + '/'], check=True)
                receiver_stage = Path(remote)
                receiver_binary = receiver_stage / 'bundle' / args.binary.name
                receiver_env = ['env', 'LD_LIBRARY_PATH=' + str(receiver_stage / 'bundle'), 'XDG_RUNTIME_DIR=/run/user/1000']
            stream_id = str(uuid.uuid4())
            base = dict(mode=args.mode, encoding=args.encoding, width=args.width, height=args.height,
                        fps=args.fps, images=args.images, packet_pixels=args.packet_pixels,
                        window_packets=args.window_packets, stream_id=stream_id, pace_synthetic=not args.unpaced)
            sender = dict(base, role='send', snapshot=str(args.output/'sender-last.ppm'))
            receiver = dict(base, role='receive', snapshot=str(receiver_stage/'receiver-last.ppm'))
            for c, stage in ((sender, stages[0]), (receiver, receiver_stage)):
                c.update(cert=str(stage/'credentials/node.cert.pem'), key=str(stage/'credentials/node.key.pem'), ca=str(stage/'credentials/ca.cert.pem'))
            if args.source == 'camera':
                sender['input'] = str(stages[0]/'capture.fifo')
                os.mkfifo(sender['input'], 0o600)
            else:
                receiver['input'] = 'synthetic-reference'
            if args.pipewire_output:
                receiver['output'] = str(receiver_stage/'output.fifo')
                run(['mkfifo', '-m', '600', receiver['output']], on_b=True)
            (stages[0]/'config.json').write_text(json.dumps(sender))
            a = launch([args.binary, stages[0]/'config.json'], 'sender', controlled=True)
            ready = line(a)
            if not ready.get('ready'):
                raise RuntimeError('producer did not start')
            proxy = ByteProxy(ready['port'])
            receiver_port = proxy.port
            if args.host:
                receiver_port = 28000 + int(uuid.uuid4().hex[:4], 16) % 20000
                master = str(root/'ssh-master')
                subprocess.run(['ssh', *ssh_options, '-M', '-S', master, '-f', '-N', '-o', 'ExitOnForwardFailure=yes',
                                '-R', f'127.0.0.1:{receiver_port}:127.0.0.1:{proxy.port}', args.host], check=True)
            receiver['peer'] = f'wss://127.0.0.1:{receiver_port}'
            (stages[1]/'config.json').write_text(json.dumps(receiver))
            if args.host:
                subprocess.run(['scp', *ssh_options, '-q', str(stages[1]/'config.json'), args.host+':'+remote+'/config.json'], check=True)
            # The copied manifests are checked here, not negotiated as a new S7 protocol.
            if any(sender[k] != receiver[k] for k in base):
                raise RuntimeError('video manifest mismatch')
            b = launch([*receiver_env, receiver_binary, receiver_stage/'config.json'], 'receiver', on_b=True, controlled=True)
            if not line(b).get('ready'):
                raise RuntimeError('receiver did not start')
            trial_started = time.monotonic()
            for process in (a, b):
                process.stdin.write('start\n')
                process.stdin.flush()
            pipelines = []
            if args.pipewire_output:
                node_name = 's7-video-spike-' + uuid.uuid4().hex
                pipelines.append(launch(['env', 'XDG_RUNTIME_DIR=/run/user/1000', 'gst-launch-1.0', '-q',
                    'filesrc', 'location='+receiver['output'], 'blocksize='+str(args.width*args.height*4), '!',
                    'rawvideoparse', 'format=bgra', 'width='+str(args.width), 'height='+str(args.height), 'framerate='+str(args.fps)+'/1', '!',
                    'pipewiresink', 'mode=provide', 'sync=false', 'use-bufferpool=false', 'stream-properties=props,media.class=Video/Source,media.role=Camera,node.name='+node_name],
                    'pipewire-output', on_b=True))
                record['pipewire_node'] = node_name
            if args.source == 'camera':
                pipelines.append(launch(['gst-launch-1.0', '-q', 'pipewiresrc', 'target-object='+args.camera_target,
                    'num-buffers='+str(args.images), '!',
                    f'video/x-raw,format=YUY2,width={args.width},height={args.height},framerate={args.fps}/1', '!',
                    'videoconvert', '!', 'video/x-raw,format=BGRA', '!', 'filesink', 'location='+sender['input'], 'sync=false'], 'capture'))
            if args.pipewire_output:
                display_env = []
                if args.virtual_display:
                    xserver = receiver_stage/'bundle/Xvfb' if args.host else Path(shutil.which('Xvfb'))
                    x = launch([*receiver_env, xserver, '-displayfd', '1', '-screen', '0', '800x600x24', '-nolisten', 'tcp'],
                               'xserver', on_b=True, controlled=True)
                    display = ':'+str(line(x))
                    display_env = ['DISPLAY='+display]
                    record['virtual_display'] = display
                # Wait for the exported node, then independently consume images.
                deadline = time.monotonic()+20
                while time.monotonic() < deadline:
                    dump = run(['env', 'XDG_RUNTIME_DIR=/run/user/1000', 'pw-dump'], on_b=True, capture_output=True, text=True).stdout
                    nodes = [v for v in json.loads(dump) if v.get('type') == 'PipeWire:Interface:Node' and
                             v.get('info', {}).get('props', {}).get('node.name') == node_name]
                    if nodes:
                        node_serial = str(nodes[0]['info']['props']['object.serial'])
                        break
                    time.sleep(.1)
                else:
                    raise TimeoutError('receiver PipeWire node did not appear')
                # PNG verifies an actual consumer on B, without claiming a physical desktop.
                viewer_command = ['env', 'XDG_RUNTIME_DIR=/run/user/1000', *display_env, 'gst-launch-1.0', '-q', 'pipewiresrc',
                    'target-object='+node_serial, '!', 'videoconvert', '!',
                    'tee', 'name=t', 't.', '!', 'queue', 'max-size-buffers=2', 'max-size-bytes=0', 'max-size-time=0', '!',
                    'pngenc', '!', 'multifilesink', 'location='+str(receiver_stage/'view-%04d.png'), 'max-files=1']
                if args.virtual_display:
                    viewer_command += ['t.', '!', 'queue', 'max-size-buffers=2', 'max-size-bytes=0', 'max-size-time=0',
                                       'leaky=downstream', '!', 'videoconvert', '!', 'ximagesink', 'sync=false']
                viewer = launch(viewer_command, 'viewer', on_b=True)
            timeout = max(60, args.images/args.fps*4+30)
            record['sender'] = line(a, timeout)
            record['receiver'] = line(b, timeout)
            record['controller_trial_seconds'] = time.monotonic()-trial_started
            if record['sender']['sha256'] != record['receiver']['sha256']:
                raise RuntimeError('decoded pixel digest mismatch')
            if args.pipewire_output:
                probe = 'import glob,json,os;print(json.dumps([p for p in glob.glob('+repr(str(receiver_stage/'view-*.png'))+') if os.path.getsize(p)>0]))'
                deadline = time.monotonic()+5
                while time.monotonic() < deadline:
                    views = json.loads(run(['python3', '-c', probe], on_b=True, capture_output=True, text=True).stdout)
                    if views and int(Path(views[-1]).stem.split('-')[-1]) >= 2:
                        record['viewer_images_observed'] = int(Path(views[-1]).stem.split('-')[-1])+1
                        break
                    time.sleep(.1)
                else:
                    raise RuntimeError('PipeWire consumer did not produce at least three images')
                if viewer.poll() is not None:
                    raise RuntimeError('PipeWire viewer exited before the controller stopped the producer')
                record['viewer_alive_before_shutdown'] = True
                if args.virtual_display:
                    run(['env', 'DISPLAY='+display, 'gst-launch-1.0', '-q', 'ximagesrc', 'display-name='+display,
                         'use-damage=false', 'num-buffers=1', '!', 'videoconvert', '!', 'pngenc', '!', 'filesink',
                         'location='+str(receiver_stage/'viewer-screen.png')], on_b=True, timeout=15)
                    record['viewer_rendered_on_virtual_display'] = True
            for process in (a, b):
                process.stdin.write('stop\n')
                process.stdin.flush()
                if process.wait(timeout=15):
                    raise RuntimeError('S7 process failed after report')
            for pipeline in pipelines:
                if pipeline.wait(timeout=20):
                    raise RuntimeError('PipeWire/GStreamer adapter failed; inspect logs')
            if args.pipewire_output:
                try:
                    record['viewer_exit_after_producer_stop'] = viewer.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    record['viewer_exit_after_producer_stop'] = 'controller cleanup required'
            record['final_consumption_ack_verified'] = True
            if args.host:
                subprocess.run(['scp', *ssh_options, '-q', args.host+':'+str(receiver_stage/'receiver-last.ppm'), str(args.output/'receiver-last.ppm')], check=True)
                if args.pipewire_output:
                    subprocess.run(['scp', *ssh_options, '-q', args.host+':'+str(receiver_stage/'view-*.png'), str(args.output)], check=True)
                    if args.virtual_display:
                        subprocess.run(['scp', *ssh_options, '-q', args.host+':'+str(receiver_stage/'viewer-screen.png'), str(args.output)], check=True)
            else:
                shutil.copy2(receiver_stage/'receiver-last.ppm', args.output/'receiver-last.ppm')
                for png in receiver_stage.glob('view-*.png'):
                    shutil.copy2(png, args.output/png.name)
                if args.virtual_display:
                    shutil.copy2(receiver_stage/'viewer-screen.png', args.output/'viewer-screen.png')
            if (args.output/'sender-last.ppm').read_bytes() != (args.output/'receiver-last.ppm').read_bytes():
                raise RuntimeError('final reconstructed images differ')
            record['tls_forward_bytes'], record['tls_reverse_bytes'] = proxy.bytes
            record['tls_payload_scope'] = 'TLS records and handshakes, both control/data connections; excludes TCP/SSH/link headers'
            record['success'] = True
        except Exception as failure:
            error = failure
            record['success'] = False
            record['error'] = str(failure)
        finally:
            for child in reversed(children):
                if child.poll() is None:
                    os.killpg(child.pid, signal.SIGTERM)
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        os.killpg(child.pid, signal.SIGKILL)
                        child.wait()
            if proxy:
                proxy.close()
            if master:
                subprocess.run(['ssh', *ssh_options, '-S', master, '-O', 'exit', args.host], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if remote:
                cleanup = '''import glob,os,pathlib,signal,sys,time
directory=sys.argv[1]
owners=[]
for filename in glob.glob(directory+'/*.pid'):
 try:
  pid=int(pathlib.Path(filename).read_text())
  cmd=pathlib.Path(f'/proc/{pid}/cmdline').read_bytes()
  if directory.encode() in cmd and os.getpgid(pid)==pid: owners.append(pid)
 except (OSError,ValueError): pass
for sig in (signal.SIGTERM,signal.SIGKILL):
 for pid in owners:
  try: os.killpg(pid,sig)
  except ProcessLookupError: pass
 time.sleep(.2)
'''
                run(['python3', '-c', cleanup, remote], on_b=True)
                subprocess.run(['ssh', *ssh_options, args.host, shlex.join(['rm', '-rf', remote])], check=True)
            for log in logs:
                log.close()
            (args.output/'result.json').write_text(json.dumps(record, indent=2)+'\n')
    if error:
        raise SystemExit(str(error)+'; logs: '+str(args.output))
    print(json.dumps({k:record[k] for k in ('success', 'source', 'mode', 'encoding', 'tls_forward_bytes')}))
    print('receiver images/s:', record['receiver']['images_per_second'])


if __name__ == '__main__':
    main()
