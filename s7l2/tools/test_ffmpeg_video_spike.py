#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Opt-in, no-device FFmpeg checks; no SSH, camera, display or S7 service."""
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def hashes(text):
    return [line.split(',')[-1].strip() for line in text.splitlines()
            if line and not line.startswith('#')]


def main():
    if not shutil.which('ffmpeg'):
        print('SKIP: FFmpeg is unavailable')
        return 77
    driver = Path(__file__).with_name('run_ffmpeg_video_spike.py')
    capabilities = subprocess.check_output(['ffmpeg', '-hide_banner', '-encoders'], text=True)
    decoders = subprocess.check_output(['ffmpeg', '-hide_banner', '-decoders'], text=True)
    codecs = ['raw', 'jpeg']
    if 'libx264' in capabilities and 'libopenh264' in decoders:
        codecs.append('h264')
    else:
        print('SKIP H.264: this experiment requires libx264 and libopenh264')
    with tempfile.TemporaryDirectory(prefix='s7-ffmpeg-check-') as tmp:
        root = Path(tmp)
        for codec in codecs:
            out = root/codec
            subprocess.run([sys.executable, '-B', str(driver), '--local', '--output', str(out),
                            '--width', '320', '--height', '240', '--fps', '30',
                            '--images', '12', '--codec', codec], check=True, timeout=45)
            result = json.loads((out/'result.json').read_text())
            assert result['status'] == 'passed' and result['receiver']['frames'] == 12
            assert result['transmitted_sha256'] == result['receiver']['input_sha256']
            assert result['receiver']['unique_decoded_images'] == 12
            if codec == 'raw':
                reference = subprocess.check_output([
                    'ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=320x240:rate=30',
                    '-frames:v', '12', '-pix_fmt', 'yuv420p', '-c:v', 'rawvideo', '-threads:v', '1',
                    '-f', 'framehash', '-hash', 'sha256', 'pipe:1'], text=True)
                assert hashes(reference) == hashes((out/'decoded.sha256').read_text()), 'raw pixels differ'
        out = root/'grayscale'
        subprocess.run([sys.executable, '-B', str(driver), '--local', '--output', str(out),
                        '--width', '320', '--height', '240', '--fps', '30', '--images', '12',
                        '--codec', 'raw', '--processing', 'grayscale'], check=True, timeout=45)
        # Independently decode the last PNG and check neutral chroma throughout
        # the whole frame, rather than only accepting FFmpeg's exit status.
        pixels = subprocess.check_output(['ffmpeg', '-v', 'error', '-i', str(out/'last.png'),
                                          '-f', 'rawvideo', '-pix_fmt', 'rgb24', 'pipe:1'])
        assert len(pixels) == 320*240*3
        assert pixels[0::3] == pixels[1::3] == pixels[2::3], 'non-neutral grayscale output'
        assert len(set(pixels[0::3])) > 10, 'empty or constant grayscale output'
        failed = subprocess.run([sys.executable, '-B', str(driver), '--local', '--output', str(root/'invalid'),
                                 '--width', '321'], capture_output=True, text=True, timeout=5)
        assert failed.returncode != 0 and not (root/'invalid').exists(), 'invalid geometry activated a worker'
        encoded = subprocess.check_output(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i',
            'testsrc2=size=320x240:rate=30', '-frames:v', '3', '-pix_fmt', 'yuv420p',
            '-c:v', 'rawvideo', '-threads:v', '1', '-f', 'nut', 'pipe:1'])
        worker = driver.with_name('ffmpeg_frame_receiver.py')
        for name, count, width, errors in [('short', 4, 320, ('decoded 3 images, expected 4', 'FFmpeg decoding failed')),
                                           ('geometry', 3, 322, ('wrong decoded image dimensions',))]:
            out = root/name
            out.mkdir()
            config = out/'config.json'
            config.write_text(json.dumps(dict(directory=str(out), codec='raw', processing='none',
                                              images=count, width=width, height=240, view=False)))
            rejected = subprocess.run([sys.executable, '-B', str(worker), str(config)], input=encoded,
                                      capture_output=True, timeout=10)
            assert rejected.returncode != 0 and any(e.encode() in rejected.stderr for e in errors), rejected.stderr.decode()
            assert not (out/'receiver-result.json').exists(), 'invalid stream reported success'
    print(f'{len(codecs)} codec cases, raw reference, grayscale and 3 input/stream rejection checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
