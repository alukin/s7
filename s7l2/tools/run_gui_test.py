#!/usr/bin/env python3
"""Run one GUI suite in its own Xvfb, retaining logs/XML/screenshots on failure."""
import argparse
import os
from pathlib import Path
import signal
import select
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET


def skip(directory, reason):
    print('SKIP: ' + reason, flush=True)
    root = ET.Element('testsuite', name='gui-environment', tests='1', skipped='1')
    case = ET.SubElement(root, 'testcase', name='environment')
    ET.SubElement(case, 'skipped', message=reason)
    ET.ElementTree(root).write(directory / 'results.xml', encoding='utf-8', xml_declaration=True)
    (directory / 'skip.txt').write_text(reason + '\n')
    return 77


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--artifacts', required=True, type=Path)
    parser.add_argument('--capture', required=True)
    parser.add_argument('--graphics', action='store_true')
    parser.add_argument('--inner', action='store_true', help=argparse.SUPPRESS)
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    directory = args.artifacts.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    if not args.inner:
        if not shutil.which('Xvfb'):
            return skip(directory, 'Xvfb is required for Linux GUI tests')
        # Let the X server allocate its display atomically. xvfb-run -a probes
        # display numbers in userspace and can collide during parallel CTest runs.
        read_fd, write_fd = os.pipe()
        with (directory / 'xvfb.log').open('w') as server_log:
            server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd), '-screen', '0',
                                       '1600x1000x24', '+extension', 'GLX', '+render',
                                       '-noreset', '-nolisten', 'tcp', '-ac'],
                                      pass_fds=(write_fd,), stdout=server_log, stderr=server_log)
            os.close(write_fd)
            try:
                if not select.select([read_fd], [], [], 15)[0]:
                    raise RuntimeError('Xvfb did not announce a display within 15 seconds')
                number = os.read(read_fd, 64).decode().strip()
                if not number.isdecimal():
                    raise RuntimeError('Xvfb failed to allocate a display; see xvfb.log')
                environment = os.environ.copy()
                environment['DISPLAY'] = ':' + number
                command = [sys.executable, str(Path(__file__).resolve()), '--inner', *sys.argv[1:]]
                return subprocess.call(command, env=environment)
            finally:
                os.close(read_fd)
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('test executable is required after --')
    env = os.environ.copy()
    env.update(QT_QPA_PLATFORM='xcb', QSG_RHI_BACKEND='opengl',
               LIBGL_ALWAYS_SOFTWARE=env.get('LIBGL_ALWAYS_SOFTWARE', '1'),
               S7_GUI_ARTIFACT_DIR=str(directory), QSG_INFO='1')
    env.pop('QT_QUICK_BACKEND', None)
    env.pop('WAYLAND_DISPLAY', None)
    # Keep settings, caches and temporary recordings out of the user's profile.
    for key, folder in [('XDG_CONFIG_HOME', 'config'), ('XDG_CACHE_HOME', 'cache'),
                        ('XDG_RUNTIME_DIR', 'runtime')]:
        path = directory / folder
        path.mkdir(exist_ok=True, mode=0o700)
        env[key] = str(path)
    (directory / 'results.xml').unlink(missing_ok=True)
    with (directory / 'diagnostics.log').open('w') as log:
        log.write('Command: ' + repr(command) + '\n')
        log.write('Display: ' + env.get('DISPLAY', '') + '\n')
        log.flush()
        if args.graphics:
            probe = subprocess.run([args.capture, '--check-graphics'], env=env,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=20)
            log.write(probe.stdout)
            log.flush()
            print(probe.stdout, end='', flush=True)
            if probe.returncode == 77:
                return skip(directory, 'OpenGL unavailable; real Qt Quick 3D rendering was not tested')
            if probe.returncode:
                return probe.returncode  # crashes/setup errors are failures, not skips
        command += ['-o', str(directory / 'results.xml') + ',junitxml', '-o', '-,txt']
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                                   start_new_session=True)
        try:
            result = process.wait(timeout=args.timeout)
        except subprocess.TimeoutExpired:
            # Capture before killing, while windows still exist.
            subprocess.run([args.capture, str(directory / 'timeout-screen.png')], env=env, timeout=15)
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            log.write('\nGUI TEST TIMEOUT\n')
            result = 124
        if result:
            subprocess.run([args.capture, str(directory / 'failure-screen.png')], env=env, timeout=15)
    if result and not (directory / 'results.xml').exists():
        root = ET.Element('testsuite', name='gui-process', tests='1', failures='1')
        case = ET.SubElement(root, 'testcase', name='process')
        ET.SubElement(case, 'failure', message=f'Process exited with {result}; see diagnostics.log')
        ET.ElementTree(root).write(directory / 'results.xml', encoding='utf-8', xml_declaration=True)
    print((directory / 'diagnostics.log').read_text(), end='', flush=True)
    return result if result >= 0 else 128 - result


if __name__ == '__main__':
    sys.exit(main())
