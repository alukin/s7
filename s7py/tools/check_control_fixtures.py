#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Runs the S7 control-protocol conformance script against a node.

The script is s7l2/s7runtime/tests/fixtures/control_v1.json (see its README):
start a node as its "node" member describes, then

  tools/check_control_fixtures.py wss://127.0.0.1:9002 --ca <root CA> \\
      --studio-cert <studio cert> --studio-key <studio key> [--skip-streams]

Requests from the "anonymous" peer go over TLS without a client certificate,
those from "studio" with the Studio certificate. --skip-streams leaves out the
steps that need the node's faulted data stream (a node without a data plane).
"""
import argparse
import asyncio
import json
import re
import ssl
import sys
from pathlib import Path
from urllib.parse import urlparse

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from s7node.tls import client_context  # noqa: E402
from s7node.websocket import connect  # noqa: E402

FIXTURE = Path(__file__).resolve().parents[2] / "s7l2" / "s7runtime" / "tests" / "fixtures" / "control_v1.json"

# Steps that need a consumer stream faulted with UNDERRUN, and NODE_INFO's data_protocol.
STREAM_STEPS = {"stream_status", "recover", "recover_repeated", "status_after_recovery"}
STREAM_EQUALS = {"data_protocol.packet_versions", "data_protocol.extensions"}

_TYPES = {
    "string": lambda v: isinstance(v, str),
    "integer": lambda v: isinstance(v, int) and not isinstance(v, bool),
    "boolean": lambda v: isinstance(v, bool),
    "object": lambda v: isinstance(v, dict),
    "array": lambda v: isinstance(v, list),
    "null": lambda v: v is None,
}
_MISSING = object()


def member(value, path: str):
    for name, index in re.findall(r"([^.\[\]]+)|\[(\d+)\]", path):
        if name:
            value = value.get(name, _MISSING) if isinstance(value, dict) else _MISSING
        else:
            value = value[int(index)] if isinstance(value, list) and int(index) < len(value) else _MISSING
        if value is _MISSING:
            break
    return value


def substitute(value, captured: dict):
    if isinstance(value, str) and (m := re.fullmatch(r"\$\{(\w+)\}", value)):
        return captured[m.group(1)]
    if isinstance(value, dict):
        return {k: substitute(v, captured) for k, v in value.items()}
    if isinstance(value, list):
        return [substitute(v, captured) for v in value]
    return value


def check(step: dict, response: dict, captured: dict, skip_streams: bool, tls: bool) -> list[str]:
    expected = substitute(step["response"], captured)
    problems = []
    if response.get("type") != expected["type"]:
        problems.append(f"type {response.get('type')!r} (code {response.get('code')!r}: {response.get('message')!r}), "
                        f"expected {expected['type']!r}")
        return problems
    for path, types in expected.get("fields", {}).items():
        value = member(response, path)
        if value is _MISSING:
            problems.append(f"{path} missing")
        elif not any(_TYPES[t](value) for t in types.split("|")):
            problems.append(f"{path} is {json.dumps(value)[:60]}, expected {types}")
    for path, value in expected.get("equals", {}).items():
        if skip_streams and path in STREAM_EQUALS:
            continue
        # The script's anonymous peer has no TLS session (UNAUTHORIZED); over TLS without a
        # client certificate the answer is CERTIFICATE_REQUIRED (control protocol 10.1).
        if tls and step["peer"] == "anonymous" and path == "code" and value == "UNAUTHORIZED" \
                and response.get("code") == "CERTIFICATE_REQUIRED":
            continue
        if member(response, path) != value:
            problems.append(f"{path} is {json.dumps(member(response, path))[:80]}, expected {json.dumps(value)[:80]}")
    return problems


async def run(url: str, contexts: dict[str, ssl.SSLContext | None], skip_streams: bool, fixture: Path = FIXTURE) -> int:
    script = json.loads(fixture.read_text())
    target = urlparse(url)
    connections, captured, failures = {}, {}, 0
    for step in script["steps"]:
        if skip_streams and step["name"] in STREAM_STEPS:
            print(f"skip  {step['name']}")
            continue
        if step["peer"] not in connections:
            connections[step["peer"]] = await connect(target.hostname, target.port, "/control", ssl=contexts[step["peer"]])
        ws = connections[step["peer"]]
        await ws.send(json.dumps(substitute(step["request"], captured)))
        response = json.loads(await ws.recv())
        problems = check(step, response, captured, skip_streams, target.scheme == "wss")
        for name, path in step.get("capture", {}).items():
            captured[name] = response.get(path)
        print(f"{'ok' if not problems else 'FAIL':5} {step['name']}" + "".join(f"\n      {p}" for p in problems))
        failures += bool(problems)
    for ws in connections.values():
        await ws.close()
    print(f"{failures} of {len(script['steps'])} steps failed" if failures else "all steps passed")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("url", help="the node's control endpoint, e.g. wss://127.0.0.1:9002")
    parser.add_argument("--ca", required=True)
    parser.add_argument("--studio-cert", required=True)
    parser.add_argument("--studio-key", required=True)
    parser.add_argument("--skip-streams", action="store_true")
    args = parser.parse_args()
    contexts = {"anonymous": client_context(args.ca), "studio": client_context(args.ca, args.studio_cert, args.studio_key)}
    return 1 if asyncio.run(run(args.url, contexts, args.skip_streams)) else 0


if __name__ == "__main__":
    sys.exit(main())
