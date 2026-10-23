#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check a relocated Linux package from a fresh working directory.

Run under a usable DISPLAY (e.g. xvfb-run -a). Outputs belong in a temporary
artifact directory. This does not establish support for other distributions.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def check(package, artifacts):
    package = package.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    for name in ('LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'S7_PLUGIN_PATH'):
        env.pop(name, None)
    env.update(XDG_CONFIG_HOME=str(artifacts / 'config'), XDG_CACHE_HOME=str(artifacts / 'cache'),
               LIBGL_ALWAYS_SOFTWARE='1', QSG_RHI_BACKEND='opengl', QT_QPA_PLATFORM='xcb')
    env.pop('WAYLAND_DISPLAY', None)
    runtime = artifacts / 'runtime'
    runtime.mkdir(mode=0o700, exist_ok=True)
    env['XDG_RUNTIME_DIR'] = str(runtime)
    results = []
    with tempfile.TemporaryDirectory(prefix='s7-installed-') as directory:
        cwd = Path(directory)

        def run(name, *args):
            result = subprocess.run([str(a) for a in args], cwd=cwd, env=env,
                                    capture_output=True, text=True, timeout=120)
            (artifacts / f'{name}.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'{name}: exit {result.returncode}; see {artifacts / (name+".log")}')
            results.append(name)
            return result.stdout

        for program in ('s7runtime', 's7ctl', 's7studio'):
            output = run('ldd-' + program, 'ldd', package / program)
            if 'not found' in output:
                raise RuntimeError(f'Unresolved shared libraries for {program}')
        run('ctl-help', package / 's7ctl', '--help')
        default = run('runtime-default', package / 's7runtime', '--state-dir', cwd / 'default-state', '--serve-seconds', '1')
        if str(package) not in default or 'runtime.plugin_count=1' not in default:
            raise RuntimeError('Runtime did not use packaged default schema and plugin')
        noise = cwd / 'noise.json'
        noise.write_text(json.dumps({'name': 'installed-noise', 'elements': [
            {'id': 'noise', 'type': 'WhiteNoiseGenerator', 'params': {'amplitude': 1., 'seed': 42}}],
            'connections': [], 'execution': {'step_rate_hz': 1000}}))
        run('noise-plugin', package / 's7runtime', '--schema', noise,
            '--state-dir', cwd / 'noise-state', '--serve-seconds', '1')
        run('bundled-demo', package / 's7studio', '--bundled-demo-check')
        model = package / 'usr/share/s7/examples/linear/rc_charge_euler.s7diagram'
        saved = cwd / 'saved.s7diagram'
        run('save-normalize', package / 's7studio', '--normalize', model, saved)
        reopened = cwd / 'reopened.s7diagram'
        run('reopen-check', package / 's7studio', '--normalize', saved, reopened)
        if json.loads(saved.read_text()) != json.loads(reopened.read_text()):
            raise RuntimeError('Saved/reopened diagram changed')
        env['S7_SPECTRUM_PNG'] = str(artifacts / 'live-spectrum.png')
        for name, mode in (('scope-run', 'scope-run'), ('phase-portrait', 'phase-portrait'),
                           ('phase-trajectory-3d', 'phase-trajectory-3d'),
                           ('live-oscilloscope-spectrum', 'live-spectrum')):
            run(name, package / 's7studio', '--' + mode + '-check')
        (artifacts / 'summary.json').write_text(json.dumps({'checks': results, 'package': str(package),
            'checks_passed': True}, indent=2) + '\n')
    print(f'{len(results)} installed-package checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('artifacts', type=Path)
    args = parser.parse_args()
    check(args.package, args.artifacts.resolve())
