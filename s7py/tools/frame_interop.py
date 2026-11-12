#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""JSON-line fixture bridge for cross-language codec tests, not a network service."""
import json
from pathlib import Path
import sys
import uuid
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from s7node import frames as f, wire


def descriptor(c):
    return f.Descriptor(data_type=wire.TYPES[c['type']], channels=tuple(c['channels']), encoding=f.Encoding(c['encoding']),
        max_frames_per_packet=c['max_frames'], window_packets=c['window_packets'], window_frames=c['window_frames'],
        configuration_revision=int(c['configuration_revision']), timing_revision=int(c['timing_revision']))


def info(c):
    i = c['info']
    return f.Info(i['frames'], uuid.UUID(i['segment']).bytes, int(i['first_frame']), int(c['configuration_revision']), int(c['timing_revision']),
                  i['segment_start'], i['fault_only'], tuple(f.Event(e['offset'], e['type'], bytes.fromhex(e['value_hex'])) for e in i['events']))


def run(request):
    c = request['case']; d = descriptor(c)
    if not f.encoding_available(d.encoding):
        return {'status': 'unavailable'}
    codec = f.Codec(d)
    if request['op'] == 'encode':
        return {'status': 'ok', 'message_hex': codec.encode(c['alias'], int(c['sequence']), info(c), bytes.fromhex(c['payload_hex'])).hex()}
    pool = f.BufferPool(1, wire.MAX_PAYLOAD_BYTES)
    packet = codec.decode(bytes.fromhex(c['message_hex']), pool)
    try:
        payload = packet.payload.bytes() if packet.payload else b''
        f.require(packet.alias == c['alias'] and packet.sequence == int(c['sequence']) and packet.info == info(c)
                  and payload.hex() == c['payload_hex'], 'fixture fields differ')
        return {'status': 'ok'}
    finally:
        packet.close(); pool.close()


if __name__ == '__main__':
    for line in sys.stdin:
        try:
            result = run(json.loads(line))
        except (ValueError, BufferError) as error:
            result = {'status': 'rejected', 'error': str(error)}
        print(json.dumps(result), flush=True)
