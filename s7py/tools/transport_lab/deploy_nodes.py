#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Deploys a diagram (with its streams already assigned, e.g. by `s7ctl check --write`) to the given
nodes over mTLS as the Studio certificate: PREPARE_DEPLOYMENT on each, then COMMIT_DEPLOYMENT with
one activation time, which it prints. For runs where some nodes have no control plane (the s7java
data node), which `s7ctl deploy` would also try to prepare.

Usage: deploy_nodes.py <diagram.json> <wss://node> [<wss://node> ...]
"""
import asyncio
import json
import sys
import time
import uuid
from pathlib import Path
from urllib.parse import urlparse

S7PY = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(S7PY))
from s7node.jcs import configuration_digest  # noqa: E402
from s7node.tls import client_context  # noqa: E402
from s7node.websocket import connect  # noqa: E402

CERTS = S7PY.parent / "s7l2" / "s7runtime" / "examples" / "certs"


async def request(ws, message: dict) -> dict:
    await ws.send(json.dumps(message))
    return json.loads(await ws.recv())


async def main():
    diagram = json.loads(Path(sys.argv[1]).read_text())
    configuration = {"project_revision": "lab:1", "diagram": diagram}
    configuration["configuration_digest"] = configuration_digest(configuration)
    deployment_id = f"lab-{uuid.uuid4().hex[:8]}"
    context = client_context(str(CERTS / "s7_root_ca.cert.pem"), str(CERTS / "s7_studio.cert.pem"), str(CERTS / "s7_studio.key.pem"))
    sockets, tokens = [], []
    for url in sys.argv[2:]:
        u = urlparse(url)
        ws = await connect(u.hostname, u.port, "/control", ssl=context)
        response = await request(ws, {"type": "PREPARE_DEPLOYMENT", "request_id": "p", "protocol_version": 1,
                                      "deployment": {"deployment_id": deployment_id, "revision": 1, "base_revision": 0,
                                                     "configuration": configuration}})
        if response.get("prepare_status") != "READY":
            raise SystemExit(f"{url}: not ready: {json.dumps(response)[:600]}")
        sockets.append(ws)
        tokens.append(response["prepare_token"])
    activate_at = time.time_ns() + 1_000_000_000  # the same start on every node
    for url, ws, token in zip(sys.argv[2:], sockets, tokens):
        response = await request(ws, {"type": "COMMIT_DEPLOYMENT", "request_id": "c", "protocol_version": 1,
                                      "deployment_id": deployment_id, "revision": 1, "prepare_token": token,
                                      "activate_at_ns": activate_at})
        if not response.get("committed"):
            raise SystemExit(f"{url}: not committed: {json.dumps(response)[:600]}")
        await ws.close()
    print(f"deployed {deployment_id} to {len(sockets)} node(s), activate_at_ns={activate_at}")


if __name__ == "__main__":
    asyncio.run(main())
