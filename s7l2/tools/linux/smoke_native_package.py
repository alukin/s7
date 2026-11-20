#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise native packages installed by dnf/apt, from outside the source tree.

No check enumerates or opens physical hardware. Run Studio checks under Xvfb
for rendering evidence, or use --studio with Qt's offscreen/null backend for
behavior-only checks. --sdk-source points at s7hardware/tests/sdk.
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


def check(args):
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    teaching = check_teaching_assets('/usr/share/s7')
    env = os.environ.copy()
    for name in ('LD_LIBRARY_PATH', 'QT_PLUGIN_PATH', 'QML2_IMPORT_PATH', 'QML_IMPORT_PATH', 'S7_PLUGIN_PATH'):
        env.pop(name, None)
    results = []
    with tempfile.TemporaryDirectory(prefix='s7-native-check-') as temporary:
        cwd = Path(temporary)
        (cwd / 'home').mkdir()
        (cwd / 'runtime').mkdir(mode=0o700)
        env.update(HOME=str(cwd / 'home'), XDG_CONFIG_HOME=str(cwd / 'config'),
                   XDG_CACHE_HOME=str(cwd / 'cache'), XDG_RUNTIME_DIR=str(cwd / 'runtime'))

        def run(name, *command):
            result = subprocess.run([str(value) for value in command], cwd=cwd, env=env,
                                    capture_output=True, text=True, timeout=180)
            (artifacts / f'{name}.log').write_text(result.stdout + result.stderr)
            if result.returncode:
                raise RuntimeError(f'{name}: exit {result.returncode}; see {artifacts / (name + ".log")}')
            results.append(name)
            return result.stdout

        programs = ['s7runtime', 's7ctl']
        if args.hardware:
            programs.append('s7hw')
        if args.studio:
            programs.append('s7studio')
        for program in programs:
            linked = run('libraries-' + program, 'ldd', '/usr/bin/' + program)
            if 'not found' in linked:
                raise RuntimeError(f'Unresolved libraries for {program}')
        run('control-help', '/usr/bin/s7ctl', '--help')
        output = run('runtime-default', '/usr/bin/s7runtime', '--state-dir', cwd / 'default-state', '--serve-seconds', '1')
        expected_plugins = 1 + (3 if args.hardware else 0) + (2 if args.analysis else 0)
        if '/usr/bin/../share/s7/runtime/examples/schema_minimal.json' not in output:
            raise RuntimeError('The runtime did not select its installed default schema')
        if f'runtime.plugin_count={expected_plugins}\n' not in output:
            raise RuntimeError(f'Expected {expected_plugins} installed element plugins; see runtime-default.log')
        noise = cwd / 'noise.json'
        noise.write_text(json.dumps({'name': 'installed-noise', 'elements': [
            {'id': 'noise', 'type': 'WhiteNoiseGenerator', 'params': {'amplitude': 1., 'seed': 42}}],
            'connections': [], 'execution': {'step_rate_hz': 1000}}))
        run('noise-element', '/usr/bin/s7runtime', '--schema', noise,
            '--state-dir', cwd / 'noise-state', '--serve-seconds', '1')
        if args.hardware:
            run('hardware-help', '/usr/bin/s7hw', '--help')
            for example in ('hardware/audio_capture.json', 'hardware/audio_playback.json',
                            'sdr/sdr_receive.json', 'sdr/fm_receiver.s7diagram'):
                run('hardware-' + Path(example).stem, '/usr/bin/s7hw', '--check',
                    f'/usr/share/s7/examples/{example}')
        if args.analysis:
            for example in ('fourier', 'wavelet'):
                run('analysis-' + example, '/usr/bin/s7runtime', '--schema',
                    f'/usr/share/s7/runtime/examples/schema_{example}_coefficient.json',
                    '--state-dir', cwd / (example + '-state'), '--serve-seconds', '1')
        if args.studio:
            env.setdefault('QT_QPA_PLATFORM', 'offscreen')
            env.setdefault('QSG_RHI_BACKEND', 'null')
            run('studio-demo', '/usr/bin/s7studio', '--bundled-demo-check')
            model = '/usr/share/s7/examples/linear/rc_charge_euler.s7diagram'
            run('studio-save', '/usr/bin/s7studio', '--normalize', model, cwd / 'saved.s7diagram')
            run('studio-reopen', '/usr/bin/s7studio', '--normalize', cwd / 'saved.s7diagram', cwd / 'reopened.s7diagram')
            if json.loads((cwd / 'saved.s7diagram').read_text()) != json.loads((cwd / 'reopened.s7diagram').read_text()):
                raise RuntimeError('Installed Studio changed the reopened model')
            for mode in ('scope-run', 'phase-portrait', 'phase-trajectory-3d', 'live-spectrum'):
                run('studio-' + mode, '/usr/bin/s7studio', '--' + mode + '-check')
        if args.sdk_source:
            source = cwd / 'sdk-consumer'
            source.mkdir()
            (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(installed_sdk LANGUAGES CXX)
enable_testing()
add_subdirectory("${S7_SDK_SOURCE}" fixture)
find_package(s7hardware 0.1 CONFIG REQUIRED)
add_executable(backends backends.cpp)
target_link_libraries(backends PRIVATE s7::s7audio s7::s7sdr)
add_test(NAME compiled_backends COMMAND backends)
''')
            (source / 'backends.cpp').write_text('''#include <s7/audio.hpp>
#include <s7/sdr.hpp>
#include <algorithm>
int main() {
    auto a = s7::audio::backends();
    auto s = s7::sdr::backends();
    return a.size() != 2 || s.size() != 1 || s[0] != "soapy"
        || std::find(a.begin(), a.end(), "portaudio") == a.end()
        || std::find(a.begin(), a.end(), "pipewire") == a.end();
}
''')
            run('sdk-configure', 'cmake', '-S', source, '-B', cwd / 'sdk-build',
                '-DS7_SDK_SOURCE=' + str(args.sdk_source.resolve()), '-DCMAKE_BUILD_TYPE=Release')
            run('sdk-build', 'cmake', '--build', cwd / 'sdk-build', '--parallel', '2')
            run('sdk-run', 'ctest', '--test-dir', cwd / 'sdk-build', '--output-on-failure')
    (artifacts / 'summary.json').write_text(json.dumps({'checks': results, 'passed': True,
        'studio_rendering_qualified': False, 'physical_hardware_opened': False,
        'teaching_assets': teaching}, indent=2) + '\n')
    print(f'{len(results)} native installed-package checks passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('artifacts', type=Path)
    parser.add_argument('--hardware', action='store_true')
    parser.add_argument('--analysis', action='store_true')
    parser.add_argument('--studio', action='store_true')
    parser.add_argument('--sdk-source', type=Path)
    check(parser.parse_args())
