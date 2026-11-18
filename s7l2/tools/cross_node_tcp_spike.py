#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""One node of an opt-in three-host scheduling fixture (not S7 wire/control).

Literature/assumptions: docs/CROSS_NODE_CAUSALITY_RESEARCH.md, L1/L3/L5/L7/T2.
One scalar token per step, consumption credits, fixed window; no device access.
"""
import argparse
from collections import deque
import hashlib
import json
import math
from pathlib import Path
import platform
import socket
import sys
import threading
import time


class Link:
    def __init__(self, incoming, outgoing, window, run_id, role):
        self.incoming, self.outgoing = incoming, outgoing
        self.window, self.run_id, self.role = window, run_id, role
        self.condition = threading.Condition()
        self.data_lock, self.ack_lock = threading.Lock(), threading.Lock()
        self.queue = deque()
        self.sent = self.consumed = self.acked = self.received = 0
        self.high_water = self.max_outstanding = 0
        self.peer_done = False
        self.error = None
        self.closed = False
        self.workers = [threading.Thread(target=self.read_data, daemon=True),
                        threading.Thread(target=self.read_acks, daemon=True)]
        for worker in self.workers:
            worker.start()

    @staticmethod
    def send(sock, lock, value):
        with lock:
            sock.sendall((json.dumps(value, allow_nan=False) + "\n").encode())

    def fail(self, error):
        with self.condition:
            if not self.closed:
                self.error = str(error)
            self.condition.notify_all()

    @staticmethod
    def records(sock):
        with sock.makefile("rb") as reader:
            while True:
                line = reader.readline(4097)
                if not line:
                    return
                if len(line) > 4096 or not line.endswith(b"\n"):
                    raise RuntimeError("oversized record")
                yield json.loads(line)

    def read_data(self):
        try:
            for record in self.records(self.incoming):
                with self.condition:
                    if record == ["done"]:
                        self.peer_done = True
                    elif (len(record) == 3 and record[0] == "data"
                          and type(record[1]) is int and record[1] == self.received
                          and type(record[2]) in (int, float) and math.isfinite(record[2])):
                        if len(self.queue) == self.window:
                            raise RuntimeError("receive window exceeded")
                        self.queue.append((record[1], record[2]))
                        self.received += 1
                        self.high_water = max(self.high_water, len(self.queue))
                    else:
                        raise RuntimeError("bad data sequence/record")
                    self.condition.notify_all()
            with self.condition:
                if not self.peer_done:
                    raise RuntimeError("peer closed before completion")
        except Exception as error:
            self.fail(error)

    def read_acks(self):
        try:
            for record in self.records(self.outgoing):
                with self.condition:
                    if not (len(record) == 2 and record[0] == "ack" and type(record[1]) is int
                            and self.acked < record[1] <= self.sent):
                        raise RuntimeError("bad consumption credit")
                    self.acked = record[1]
                    self.condition.notify_all()
            with self.condition:
                if self.acked != self.sent:
                    raise RuntimeError("peer closed with unacknowledged data")
        except Exception as error:
            self.fail(error)

    def close(self):
        with self.condition:
            self.closed = True
        for sock in (self.incoming, self.outgoing):
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()
        for worker in self.workers:
            worker.join(timeout=2)


def run(args):
    role_index = "ABC".index(args.role)
    previous, following = "ABC"[(role_index - 1) % 3], "ABC"[(role_index + 1) % 3]
    # All listeners are loopback. The orchestrator uses dedicated SSH forwards.
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("127.0.0.1", args.listen))
        listener.listen(1)
        listener.settimeout(10)
        print(json.dumps({"ready": args.role}), flush=True)
        if args.wait_start and sys.stdin.readline().strip() != "start":
            raise RuntimeError("missing orchestration start")
        outgoing = None
        until = time.monotonic() + 10
        while outgoing is None:
            try:
                outgoing = socket.create_connection(("127.0.0.1", args.next), timeout=0.5)
            except OSError:
                if time.monotonic() >= until:
                    raise
                time.sleep(0.05)
        incoming, _ = listener.accept()
    for sock in (incoming, outgoing):
        sock.settimeout(None)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    outgoing.sendall((json.dumps(["hello", args.run_id, args.role, following]) + "\n").encode())
    # Read the short handshake without read-ahead into the first data record.
    incoming.settimeout(10)
    hello = bytearray()
    while not hello.endswith(b"\n") and len(hello) <= 256:
        byte = incoming.recv(1)
        if not byte:
            raise RuntimeError("EOF in handshake")
        hello.extend(byte)
    if json.loads(hello) != ["hello", args.run_id, previous, args.role]:
        raise RuntimeError("wrong run/role handshake")
    incoming.settimeout(None)
    link = Link(incoming, outgoing, args.window, args.run_id, args.role)
    samples, events = [], []
    state = 0.0
    error = None
    readiness_checks = 0
    blocked_reads = blocked_writes = 0
    started = time.monotonic()
    try:
        for step in range(args.steps):
            if args.slow_ms:
                time.sleep(args.slow_ms / 1000)
            received = None
            pending = ["receive", "send"]
            if args.approach == "projected" and (args.topology == "independent" or args.role == "A"):
                pending.reverse()
            deadline = time.monotonic() + 2
            while pending:
                with link.condition:
                    if link.error:
                        raise RuntimeError(link.error)
                    ready = None
                    candidates = pending if args.approach == "ready" else pending[:1]
                    for task in candidates:
                        readiness_checks += 1
                        if task == "receive":
                            enabled = bool(link.queue)
                            blocked_reads += not enabled
                        else:
                            has_value = args.topology == "independent" or args.role == "A" or received is not None
                            enabled = has_value and link.sent - link.acked < args.window
                            blocked_writes += has_value and not enabled
                        if enabled:
                            ready = task
                            break
                    if ready is None:
                        remaining = deadline - time.monotonic()
                        if remaining <= 0:
                            raise TimeoutError("no progress: " + ",".join(pending))
                        link.condition.wait(remaining)
                        continue
                    if ready == "receive":
                        index, received = link.queue.popleft()
                        if index != step:
                            raise RuntimeError("logical step mismatch")
                        link.consumed += 1
                        ack = link.consumed
                    else:
                        if args.topology == "independent":
                            value = (2.0, 3.0, 5.0)[role_index]
                        else:
                            value = state if args.role == "A" else received + (1 if args.role == "B" else 0)
                        link.sent += 1
                        link.max_outstanding = max(link.max_outstanding, link.sent - link.acked)
                if ready == "receive":
                    Link.send(incoming, link.ack_lock, ["ack", ack])
                else:
                    Link.send(outgoing, link.data_lock, ["data", step, value])
                events.append([step, ready])
                pending.remove(ready)
            expected = ((5.0, 2.0, 3.0)[role_index] if args.topology == "independent"
                        else float(step if args.role == "B" else step + 1))
            if received != expected:
                raise RuntimeError(f"reference mismatch at step {step}: {received} vs {expected}")
            samples.append(received)
            if args.role == "A" and args.topology == "feedback":
                state = received
        Link.send(outgoing, link.data_lock, ["done"])
        deadline = time.monotonic() + 2
        with link.condition:
            while not (link.peer_done and link.acked == link.sent):
                if link.error:
                    raise RuntimeError(link.error)
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError("completion/credit timeout")
                link.condition.wait(remaining)
    except Exception as failure:
        error = str(failure)
    finally:
        elapsed = time.monotonic() - started
        link.close()
    return {"engine": "three_process_ssh_tcp_fixture", "role": args.role,
            "run_id": args.run_id, "topology": args.topology, "approach": args.approach,
            "window": args.window, "slow_ms": args.slow_ms, "steps": len(samples),
            "completed": error is None, "error": error, "samples": samples,
            "first_events": events[:8], "max_receive_queue": link.high_water,
            "max_unacknowledged": link.max_outstanding, "readiness_checks": readiness_checks,
            "blocked_read_checks": blocked_reads, "blocked_write_checks": blocked_writes,
            "elapsed_seconds": elapsed, "platform": platform.platform(),
            "hostname": socket.gethostname(), "python": platform.python_version(),
            "runner_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--role", choices=list("ABC"), required=True)
    p.add_argument("--listen", type=int, required=True)
    p.add_argument("--next", type=int, required=True)
    p.add_argument("--approach", choices=("fixed", "projected", "ready"), required=True)
    p.add_argument("--topology", choices=("feedback", "independent"), required=True)
    p.add_argument("--window", type=int, choices=(1, 8), required=True)
    p.add_argument("--steps", type=int, default=32)
    p.add_argument("--slow-ms", type=int, default=0)
    p.add_argument("--run-id", required=True)
    p.add_argument("--wait-start", action="store_true")
    args = p.parse_args()
    if not 1 <= args.steps <= 10000 or not 0 <= args.slow_ms <= 100:
        p.error("invalid steps/slow-ms")
    result = run(args)
    print(json.dumps(result, allow_nan=False), flush=True)
    return 0 if result["completed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
