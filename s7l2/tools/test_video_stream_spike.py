#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""No-device checks of scalar/batched video graphs over real local mTLS."""
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import threading
import time

from run_cross_node_s7_spikes import credentials, read_line


def incomplete_and_cancelled(binary, root):
    stages = [root/'source', root/'receiver']
    for stage in stages:
        stage.mkdir()
    credentials(stages)
    fifo = root/'short.fifo'
    os.mkfifo(fifo, 0o600)
    config = dict(role='send', mode='staged', encoding='raw', width=17, height=13, images=1, fps=120,
                  packet_pixels=19, window_packets=1, stream_id='dddddddd-0000-4000-8000-000000000004', input=str(fifo),
                  cert=str(stages[0]/'credentials/node.cert.pem'), key=str(stages[0]/'credentials/node.key.pem'),
                  ca=str(stages[0]/'credentials/ca.cert.pem'))
    path = root/'negative.json'
    path.write_text(json.dumps(config))
    for cancelled in (False, True):
        child = subprocess.Popen([str(binary), str(path)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE, text=True)
        writer = None
        try:
            assert read_line(child, 10)['ready']
            child.stdin.write('start\n')
            child.stdin.flush()
            if cancelled:
                time.sleep(.1)
                began = time.monotonic()
                child.send_signal(signal.SIGTERM)
            else:
                def short_write():
                    with open(fifo, 'wb', buffering=0) as output:
                        output.write(b'\x01\x02\x03')  # less than even one complete pixel
                writer = threading.Thread(target=short_write, daemon=True)
                writer.start()
            out, err = child.communicate(timeout=5)
            assert child.returncode != 0 and 'sha256' not in out, (out, err)
            assert ('cancelled or timed out' if cancelled else 'short input image') in err, err
            if cancelled:
                assert time.monotonic()-began < 2, err
        finally:
            if child.poll() is None:
                child.kill()
                child.communicate()
            if writer:
                writer.join(timeout=2)
                assert not writer.is_alive(), 'FIFO writer leaked'


def main():
    try:
        import cryptography  # noqa: F401
    except ImportError:
        print('Python cryptography is required for isolated test credentials')
        return 77
    binary = Path(sys.argv[1]).resolve()
    driver = Path(__file__).with_name('run_video_stream_spike.py')
    codecs = json.loads(subprocess.check_output([str(binary), '--capabilities'], text=True))['codecs']
    expected = None
    cases = 0
    with tempfile.TemporaryDirectory(prefix='s7-video-contract-') as tmp:
        for mode in ('scalar', 'staged', 'bulk'):
            for codec in codecs:
                output = Path(tmp)/(mode+'-'+codec)
                # Uneven geometry/packet sizes exercise split images, one-packet
                # consumption credit and the final unsealed partial batch.
                subprocess.run([sys.executable, '-B', str(driver), '--binary', str(binary), '--output', str(output),
                                '--mode', mode, '--encoding', codec, '--images', '5', '--width', '17', '--height', '13',
                                '--fps', '120', '--packet-pixels', '19', '--window-packets', '1'], check=True, timeout=45)
                r = json.loads((output/'result.json').read_text())
                assert r['success'] and r['final_consumption_ack_verified'], r
                for side in ('sender', 'receiver'):
                    record = r[side]
                    assert record['pixels'] == 1105, record
                    assert record['graph_steps'] == (0 if mode == 'bulk' else 1105), record
                    expected = expected or record['sha256']
                    assert record['sha256'] == expected, record
                    assert all(s['fault'] == 'NONE' for s in record['streams']), record
                cases += 1
        incomplete_and_cancelled(binary, Path(tmp))
    print(f'{cases} no-device video cases plus short-image/cancellation rejection passed: pixel identity, scalar steps, mTLS, codecs and final consumption')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
