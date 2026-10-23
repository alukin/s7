#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Independent Python WebSocket peer checking C++ split-listener isolation."""

import base64
import hashlib
import json
import secrets
import socket
import ssl
import struct
import sys
from pathlib import Path
from urllib.parse import urlparse


def exact(connection, count):
    data = bytearray()
    while len(data) < count:
        part = connection.recv(count - len(data))
        if not part:
            raise EOFError('WebSocket closed')
        data.extend(part)
    return bytes(data)


def connect(url, context, path, node_uuid):
    target = urlparse(url)
    connection = socket.create_connection((target.hostname, target.port), timeout=3)
    try:
        if target.scheme == 'wss':
            connection = context.wrap_socket(connection, server_hostname=target.hostname)
            identities = [value for kind, value in connection.getpeercert().get('subjectAltName', ())
                          if kind == 'URI' and value.startswith('spiffe://s7/node/')]
            assert identities == ['spiffe://s7/node/' + node_uuid], identities
        key = base64.b64encode(secrets.token_bytes(16)).decode()
        connection.sendall((f'GET {path} HTTP/1.1\r\nHost: {target.netloc}\r\nUpgrade: websocket\r\n'
                            f'Connection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\n\r\n').encode())
        head = bytearray()
        while not head.endswith(b'\r\n\r\n'):
            head.extend(exact(connection, 1))
            assert len(head) < 16384, 'Oversized HTTP response'
        lines = head.decode().split('\r\n')
        assert lines[0].split()[1] == '101', lines[0]
        headers = {name.lower(): value.strip() for name, value in
                   (line.split(':', 1) for line in lines[1:] if ':' in line)}
        expected = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        assert headers.get('sec-websocket-accept') == expected, 'Invalid upgrade response'
        return connection
    except BaseException:
        connection.close()
        raise


def send_frame(connection, opcode, data):
    assert len(data) < 126
    mask = secrets.token_bytes(4)
    connection.sendall(bytes((0x80 | opcode, 0x80 | len(data))) + mask + bytes(byte ^ mask[i % 4] for i, byte in enumerate(data)))


def send(connection, value):
    send_frame(connection, 1, json.dumps(value).encode())


def receive(connection):
    message = bytearray()
    started = False
    while True:
        flags, length = exact(connection, 2)
        opcode = flags & 15
        assert not length & 0x80, 'Server frames must be unmasked'
        if length == 126:
            length = struct.unpack('!H', exact(connection, 2))[0]
        elif length == 127:
            length = struct.unpack('!Q', exact(connection, 8))[0]
        assert len(message) + length <= 4 * 1024 * 1024
        payload = exact(connection, length)
        if opcode == 8:
            raise EOFError('WebSocket closed by server')
        if opcode == 9:
            send_frame(connection, 10, payload)
            continue
        if opcode == 10:
            continue
        assert opcode == (0 if started else 1), 'Expected text/continuation response'
        started = True
        message.extend(payload)
        if flags & 0x80:
            return json.loads(message)


def main():
    control_url, cert_dir, node_uuid = sys.argv[1:]
    certs = Path(cert_dir)
    # Match the runtime/s7py chain-verification policy. The development CA
    # predates Python 3.13's additional VERIFY_X509_STRICT defaults.
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.load_verify_locations(str(certs / 's7_root_ca.cert.pem'))
    # S7 identity is the verified SAN URI, checked above, rather than a DNS SAN.
    context.check_hostname = False
    context.load_cert_chain(str(certs / 's7_studio.cert.pem'), str(certs / 's7_studio.key.pem'))
    request = {'type': 'GET_NODE_INFO', 'request_id': 'python-split', 'protocol_version': 1}
    with connect(control_url, context, '/control', node_uuid) as connection:
        send(connection, request)
        info = receive(connection)
        assert info['type'] == 'NODE_INFO', info
    network = info['network']
    data_url, security = network['data_listen_url'], network['data_security']
    assert security in ('mtls', 'trusted_plaintext'), security
    assert network['separate_data_listener'] and data_url != control_url
    assert urlparse(data_url).scheme == ('ws' if security == 'trusted_plaintext' else 'wss')
    # Establish the data connection using its selected transport, independently
    # of the control connection. JSON control requests on /data must be refused.
    with connect(data_url, context, '/data', node_uuid) as connection:
        try:
            send(connection, request)
            receive(connection)
        except (EOFError, ConnectionError, ssl.SSLError):
            pass
        else:
            raise AssertionError('Data listener processed a control request')
    for url, path in ((data_url, '/control'), (control_url, '/data'), (control_url.replace('wss://', 'ws://'), '/control')):
        try:
            with connect(url, context, path, node_uuid) as connection:
                send(connection, request)
                receive(connection)
        except (EOFError, ConnectionError, ssl.SSLError):
            pass
        else:
            raise AssertionError(f'Wrong channel/transport accepted: {url}{path}')
    print('Python peer: mTLS control, selected data transport and listener isolation passed')


if __name__ == '__main__':
    main()
