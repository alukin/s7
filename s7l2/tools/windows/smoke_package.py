#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify a relocated sound-only Windows bundle under Wine, without build DLL paths.

Checks are offline or use fake graph data; no sound device is opened. GUI checks
use offscreen/null rendering and do not qualify native Windows desktop rendering.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from teaching_assets import check as check_teaching_assets


def check(package, artifacts):
    package = package.resolve()
    artifacts = artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    teaching = check_teaching_assets(package / 'share/s7')
    profile = json.loads((package / 'build-profile.json').read_text())['build_options']
    if profile.get('S7_BUILD_SDR:BOOL') != 'OFF' or profile.get('S7_PORTAUDIO:STRING') != 'ON':
        raise RuntimeError('Expected the sound-enabled, SDR-disabled Windows profile')
    binaries = {path.name.lower() for path in package.rglob('*.dll')}
    if any(any(token in name for token in ('s7sdr', 'soapy', 'libiio', 'ad936')) for name in binaries):
        raise RuntimeError('The sound-only bundle contains SDR libraries')
    for name in ('libs7hardware.dll', 'libs7audio.dll', 'libs7audio-elements.dll', 'libportaudio.dll'):
        if name not in binaries:
            raise RuntimeError('Missing sound dependency: ' + name)

    env = os.environ.copy()
    for name in ('WINEPATH', 'LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QML_IMPORT_PATH',
                 'QML2_IMPORT_PATH', 'S7_PLUGIN_PATH', 'DISPLAY', 'WAYLAND_DISPLAY'):
        env.pop(name, None)
    prefix = artifacts / 'wine-prefix'
    env.update(WINEPREFIX=str(prefix), WINEDEBUG='-all',
               WINEDLLOVERRIDES='mscoree,mshtml=;winedbg.exe=d', QSG_RHI_BACKEND='null')
    results = []
    with tempfile.TemporaryDirectory(prefix='s7-windows-installed-') as temporary:
        cwd = Path(temporary)

        def run(name, *command, expected=0):
            result = subprocess.run([str(part) for part in command], cwd=cwd, env=env,
                                    capture_output=True, text=True, timeout=180)
            (artifacts / f'{name}.log').write_text(result.stdout + result.stderr)
            if (expected == 0 and result.returncode) or (expected != 0 and not result.returncode):
                raise RuntimeError(f'{name}: exit {result.returncode}; see {artifacts / (name + ".log")}')
            results.append(name)
            return result.stdout + result.stderr

        prefix.mkdir(exist_ok=True)
        run('wine-initialize', 'wineboot', '-i')
        run('wine-offscreen', 'wine', 'reg', 'add', r'HKCU\Environment', '/v',
            'QT_QPA_PLATFORM', '/t', 'REG_SZ', '/d', 'offscreen', '/f')
        for directory in (Path('/usr/share/fonts/dejavu-sans-fonts'), Path('/usr/share/fonts/truetype/dejavu')):
            if (directory / 'DejaVuSans.ttf').is_file():
                windows_fonts = run('wine-font-path', 'winepath', '-w', directory).strip()
                run('wine-fonts', 'wine', 'reg', 'add', r'HKCU\Environment', '/v',
                    'QT_QPA_FONTDIR', '/t', 'REG_SZ', '/d', windows_fonts, '/f')
                break
        run('control-help', 'wine', package / 's7ctl.exe', '--help')
        output = run('runtime-default', 'wine', package / 's7runtime.exe',
                     '--state-dir', cwd / 'default-state', '--serve-seconds', '1')
        expected_plugins = len(list((package / 'plugins').glob('libs7*.dll')))
        if f'runtime.plugin_count={expected_plugins}\n' not in output or 'schema_minimal.json' not in output:
            raise RuntimeError('Runtime did not discover its packaged schema/plugins')
        noise = cwd / 'noise.json'
        noise.write_text(json.dumps({'name': 'installed-noise', 'elements': [
            {'id': 'noise', 'type': 'WhiteNoiseGenerator', 'params': {'amplitude': 1., 'seed': 42}}],
            'connections': [], 'execution': {'step_rate_hz': 1000}}))
        run('noise-element', 'wine', package / 's7runtime.exe', '--schema', noise,
            '--state-dir', cwd / 'noise-state', '--serve-seconds', '1')
        run('sound-help', 'wine', package / 's7hw.exe', '--help')
        for example in ('portaudio_capture', 'portaudio_playback'):
            run(example, 'wine', package / 's7hw.exe', '--check',
                package / f'share/s7/examples/hardware/{example}.json')
        sdr = cwd / 'excluded-sdr.json'
        sdr.write_text(json.dumps({'name': 'excluded-sdr', 'elements': [
            {'id': 'radio', 'type': 'SdrSource', 'params': {'type': 'C32', 'backend': 'soapy'}}],
            'connections': []}))
        rejected = run('sdr-excluded', 'wine', package / 's7hw.exe', '--check', sdr, expected=1)
        if 'SdrSource' not in rejected:
            raise RuntimeError('SDR rejection did not identify the excluded element type')
        for example in ('fourier', 'wavelet'):
            run('analysis-' + example, 'wine', package / 's7runtime.exe', '--schema',
                package / f'examples/schema_{example}_coefficient.json',
                '--state-dir', cwd / (example + '-state'), '--serve-seconds', '1')
        run('studio-demo', 'wine', package / 's7studio.exe', '--bundled-demo-check')
        model = package / 'share/s7/examples/linear/rc_charge_euler.s7diagram'
        run('studio-save', 'wine', package / 's7studio.exe', '--normalize', model, cwd / 'saved.s7diagram')
        run('studio-reopen', 'wine', package / 's7studio.exe', '--normalize',
            cwd / 'saved.s7diagram', cwd / 'reopened.s7diagram')
        if json.loads((cwd / 'saved.s7diagram').read_text()) != json.loads((cwd / 'reopened.s7diagram').read_text()):
            raise RuntimeError('Studio changed the reopened model')
        run('studio-sound-element', 'wine', package / 's7studio.exe', '--normalize',
            package / 'share/s7/examples/hardware/portaudio_capture.json', cwd / 'sound.s7diagram')
        for mode in ('scope-run', 'phase-portrait', 'phase-trajectory-3d', 'live-spectrum'):
            run('studio-' + mode, 'wine', package / 's7studio.exe', '--' + mode + '-check')
    (artifacts / 'summary.json').write_text(json.dumps({'passed': True, 'checks': results, 'teaching_assets': teaching,
        'sdr_excluded': True, 'sound_backend': 'portaudio', 'physical_sound_device_opened': False,
        'native_windows_qualified': False}, indent=2) + '\n')
    print(f'{len(results)} relocated Windows package checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('artifacts', type=Path)
    args = parser.parse_args()
    check(args.package, args.artifacts)
