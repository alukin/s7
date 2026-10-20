#!/usr/bin/env python3
"""Summarize CTest GUI outcomes and reject skipped mandatory workflow tests."""
import os
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

report = Path(sys.argv[1])
if not report.is_file():
    sys.exit('GUI report is missing; GUI validation did not execute')
cases = ET.parse(report).getroot().findall('.//testcase')
required = {'s7studio_gui_workflows', 's7studio_gui_lineplot', 's7studio_gui_failure_artifacts'}
seen = set()
failed = False
lines = ['## Studio GUI regression results', '', '| Test | Result |', '|---|---|']
for case in cases:
    name = case.get('name', '(unnamed)')
    seen.add(name)
    if case.find('skipped') is not None:
        outcome = 'SKIPPED (not tested)'
        failed |= name in required
    elif case.find('failure') is not None or case.find('error') is not None:
        outcome = 'FAILED'
        failed = True
    else:
        outcome = 'PASSED'
    lines.append(f'| {name} | {outcome} |')
expected = required | {'s7studio_gui_trajectory'}
missing = expected - seen
if missing:
    lines += ['', 'Missing GUI tests: ' + ', '.join(sorted(missing))]
    failed = True
# CTest sees a passing executable even if one optional data row was skipped.
# Surface those framework-level skips too, rather than burying them in artifacts.
for framework in (report.parent / 's7studio' / 'gui-artifacts').glob('*/results.xml'):
    if framework.parent.name == 'injected-failure':
        continue
    for case in ET.parse(framework).getroot().findall('.//testcase'):
        if case.find('skipped') is not None:
            lines.append(f"| {framework.parent.name}: {case.get('name')} | SKIPPED (not tested) |")
text = '\n'.join(lines) + '\n'
print(text)
if os.environ.get('GITHUB_STEP_SUMMARY'):
    with open(os.environ['GITHUB_STEP_SUMMARY'], 'a') as summary:
        summary.write(text)
sys.exit(1 if failed else 0)
