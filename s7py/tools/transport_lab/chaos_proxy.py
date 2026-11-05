#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""A TCP proxy that drops every connection through it at a fixed period (fault injection).

It forwards bytes unchanged, so TLS stays end to end between the nodes. Every `period`
seconds it resets (RST) all open connections; the consumer reconnects and RESUMEs. SIGUSR1 stops
the drops (the proxy keeps forwarding).

Usage: chaos_proxy.py <listen port> <target port> <period seconds> [log file]
"""
import asyncio
import signal
import socket
import struct
import sys
import time

LISTEN, TARGET, PERIOD = int(sys.argv[1]), int(sys.argv[2]), float(sys.argv[3])
LOG = open(sys.argv[4], "a") if len(sys.argv) > 4 else sys.stdout
open_pairs = set()
stats = {"connections": 0, "drops": 0, "bytes": 0}
chaos_on = True


def log(message):
    print(f"{time.time():.3f} {message}", file=LOG, flush=True)


async def pipe(reader, writer):
    try:
        while data := await reader.read(65536):
            stats["bytes"] += len(data)
            writer.write(data)
            await writer.drain()
    except (ConnectionError, OSError):
        pass
    finally:
        writer.close()


async def handle(client_reader, client_writer):
    try:
        server_reader, server_writer = await asyncio.open_connection("127.0.0.1", TARGET)
    except OSError:
        client_writer.close()
        return
    stats["connections"] += 1
    pair = (client_writer, server_writer)
    open_pairs.add(pair)
    try:
        await asyncio.gather(pipe(client_reader, server_writer), pipe(server_reader, client_writer))
    finally:
        open_pairs.discard(pair)


async def chaos():
    while True:
        await asyncio.sleep(PERIOD)
        if chaos_on and open_pairs:
            stats["drops"] += len(open_pairs)
            log(f"drop {len(open_pairs)} connection(s)")
            for a, b in list(open_pairs):
                for w in (a, b):
                    transport = w.transport
                    if transport is None:
                        continue
                    sock = transport.get_extra_info("socket")
                    if sock is not None:
                        # Linger 0: close sends RST even when nothing is left unread or unsent,
                        # which abort() alone does not guarantee. The data in flight is lost.
                        sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
                    transport.abort()


async def main():
    def calm():
        global chaos_on
        chaos_on = False
        log("drops stopped")

    asyncio.get_running_loop().add_signal_handler(signal.SIGUSR1, calm)
    server = await asyncio.start_server(handle, "127.0.0.1", LISTEN)
    log(f"proxy 127.0.0.1:{LISTEN} -> 127.0.0.1:{TARGET}, drop every {PERIOD} s")
    asyncio.create_task(chaos())
    try:
        async with server:
            await server.serve_forever()
    finally:
        log(f"stats {stats}")


try:
    asyncio.run(main())
except KeyboardInterrupt:
    pass
