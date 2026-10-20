#!/usr/bin/env python3
"""Verify a deliberately failing Qt Test produces actionable artifacts."""
import os
from pathlib import Path
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

wrapper, capture, executable, output = sys.argv[1:]
directory = Path(output)
environment = os.environ.copy()
environment['S7_GUI_INJECT_FAILURE'] = '1'
started = time.time()
result = subprocess.run([sys.executable, wrapper, '--artifacts', output,
                         '--capture', capture, '--', executable, 'failureArtifactProbe'], env=environment)
if result.returncode == 77:
    sys.exit(77)
if result.returncode != 1:
    sys.exit('Expected exactly one injected Qt Test failure')
report = ET.parse(directory / 'results.xml').getroot()
if not report.findall('.//failure'):
    sys.exit('Missing injected failure in JUnit report')
for pattern in ['failureArtifactProbe-*-studio.png', 'failure-screen.png', 'diagnostics.log', '*-document.json', '*-objects.txt']:
    if not any(path.stat().st_size > 0 and path.stat().st_mtime >= started for path in directory.glob(pattern)):
        sys.exit('Missing or empty failure artifact: ' + pattern)
print('Failure artifact self-check passed: screenshot, document, diagnostics and JUnit failure retained')

# Explicitly verify the unavailable-display path reports a skip and creates a
# skipped JUnit result rather than accidentally passing the underlying test.
skip_directory = directory / 'missing-xvfb'
skip_environment = os.environ.copy()
skip_environment['PATH'] = ''
result = subprocess.run([sys.executable, wrapper, '--artifacts', str(skip_directory),
                         '--capture', capture, '--', executable], env=skip_environment)
if result.returncode != 77 or not ET.parse(skip_directory / 'results.xml').findall('.//skipped'):
    sys.exit('Unavailable Xvfb must report an explicit skip')
print('Unavailable-display self-check passed: exit 77 and skipped JUnit result')

# A blocked child exercises the watchdog without depending on Studio timing.
# signal.pause() intentionally never finishes until the wrapper terminates it.
timeout_directory = directory / 'timeout'
result = subprocess.run([sys.executable, wrapper, '--artifacts', str(timeout_directory),
                         '--capture', capture, '--timeout', '1', '--', sys.executable,
                         '-c', 'import signal; signal.pause()'])
if result.returncode != 124:
    sys.exit('Watchdog did not report the blocked child as a timeout')
if not (timeout_directory / 'timeout-screen.png').is_file():
    sys.exit('Watchdog did not capture the screen before terminating the child')
if 'GUI TEST TIMEOUT' not in (timeout_directory / 'diagnostics.log').read_text():
    sys.exit('Watchdog did not retain timeout diagnostics')
if not ET.parse(timeout_directory / 'results.xml').findall('.//failure'):
    sys.exit('Watchdog did not create a failing JUnit process result')
print('Watchdog self-check passed: blocked process terminated, screenshot/log/JUnit retained')
