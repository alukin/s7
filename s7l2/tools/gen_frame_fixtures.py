#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent frame-profile fixtures; optional codec packages are needed to regenerate."""
import copy
import json
from pathlib import Path
import random
import struct
import sys
import zlib
import lz4.block
import zstandard

TYPES = {"I8": (1, 1), "U8": (2, 1), "I16": (5, 2), "U16": (6, 2), "I32": (7, 4), "U32": (8, 4),
         "I64": (9, 8), "U64": (10, 8), "I128": (11, 16), "U128": (12, 16), "F16": (13, 2), "F32": (14, 4),
         "F64": (15, 8), "F128": (16, 16), "C16": (0x101, 4), "C32": (0x102, 8), "C64": (0x103, 16),
         "C128": (0x104, 32), "CI8": (0x201, 2), "CU8": (0x202, 2), "CI16": (0x203, 4), "CI32": (0x204, 8),
         "CI64": (0x205, 16), "CI128": (0x206, 32), "CI12": (0x207, 3)}
SEGMENT = "01234567-89ab-4def-8123-456789abcdef"


def packet(c, unit=None):
    i = c['info']; raw = bytes.fromhex(c['payload_hex']); events = i['events']
    segment = bytes.fromhex(i['segment'].replace('-', ''))
    value = struct.pack('<HHI16sQQQII', 1, int(i['segment_start']) | int(i['fault_only']) << 1, i['frames'], segment,
                        int(i['first_frame']), int(c['configuration_revision']), int(c['timing_revision']), len(events), len(events) * 24)
    for e in events:
        value += struct.pack('<IHH', e['offset'], e['type'], 16) + bytes.fromhex(e['value_hex'])
    tlvs = struct.pack('<HH', 0x8002, len(value)) + value
    body = raw
    if c['encoding'] and not i['fault_only']:
        candidate = lz4.block.compress(raw, store_size=False) if c['encoding'] == 1 else zstandard.ZstdCompressor(level=1).compress(raw)
        chosen = c['encoding'] if len(candidate) < len(raw) else 0
        if unit is not None:
            chosen = unit
        body = candidate if chosen else raw
        tlvs += struct.pack('<HHHHII', 0x8003, 12, 1, chosen, len(raw), zlib.crc32(raw))
    area = struct.pack('<H', len(tlvs)) + tlvs; area += bytes(-len(area) % 8)
    return struct.pack('<BBHIQ', 1, 1, TYPES[c['type']][0], c['alias'], int(c['sequence'])) + area + body


def case(name, encoding=0, typ='F32', frames=8, payload=None, events=()):
    if payload is None:
        payload = random.Random(817).randbytes(frames * 2 * TYPES[typ][1])
    c = dict(name=name, encoding=encoding, type=typ, channels=['L', 'R'], max_frames=max(frames, 8), window_packets=4,
             window_frames=max(frames, 8) * 4, configuration_revision='0', timing_revision='0', alias=1, sequence='0',
             info=dict(frames=frames, segment=SEGMENT, first_frame='0', segment_start=True, fault_only=False, events=list(events)),
             payload_hex=payload.hex())
    c['message_hex'] = packet(c).hex()
    return c


def generate():
    valid = [case('raw_' + typ, typ=typ) for typ in TYPES]
    anchor = dict(offset=3, type=0x8002, value_hex=struct.pack('<qQ', -123, (1 << 64) - 1).hex())
    for enc in range(3):
        for typ in ['I16', 'F32', 'CI12']:
            valid.append(case(f'zero_{enc}_{typ}', enc, typ, 128, bytes(128 * 2 * TYPES[typ][1]), [anchor]))
        valid.append(case(f'noise_fallback_{enc}', enc, 'I16', 512))
        for code, lost in [(1, (1 << 64) - 1), (3, None)]:
            c = case(f'fault_{enc}_{code}', 0)
            c['encoding'] = enc; c['info'].update(frames=0, fault_only=True,
                events=[dict(offset=0, type=0x8001, value_hex=struct.pack('<HHIQ', code, int(lost is not None), 0, lost or 0).hex())])
            c['payload_hex'] = ''; c['message_hex'] = packet(c).hex(); valid.append(c)
    high = case('unsigned_fields', frames=8)
    high.update(alias=0xf1234567, sequence=str((1 << 63) + 3), configuration_revision=str((1 << 64) - 1), timing_revision=str((1 << 63) + 7))
    high['info'].update(first_frame=str((1 << 64) - 9), segment_start=False)
    high['message_hex'] = packet(high).hex(); valid.append(high)
    nan = case('float_special_bits', frames=2, payload=struct.pack('<IIII', 0x80000000, 0x7fc00123, 0x7fa01234, 0x7f800000))
    valid.append(nan)
    optional = case('unknown_optional_header')
    b = bytes.fromhex(optional['message_hex'])
    extra = struct.pack('<HH', 3, 3) + b'abc' + b[18:78]
    area = struct.pack('<H', len(extra)) + extra; area += bytes(-len(area) % 8)
    optional['message_hex'] = (b[:16] + area + b[80:]).hex(); valid.append(optional)
    invalid = []
    def bad(name, original, mutate):
        c = copy.deepcopy(original); c['name'] = name
        b = bytearray.fromhex(c['message_hex']); mutate(b); c['message_hex'] = b.hex(); invalid.append(c)
    base = valid[0]
    for n in [0, 1, 15, 16, 17, 18, 21, 77, len(bytes.fromhex(base['message_hex'])) - 1]:
        bad('truncated_' + str(n), base, lambda b, n=n: b.__delitem__(slice(n, None)))
    for name, offset, value in [('version', 0, 2), ('flags', 1, 3), ('type', 2, 255), ('alias_zero', 4, 0),
                                ('extension_critical', 18, 4), ('profile_version', 22, 2), ('profile_flags', 24, 4),
                                ('count', 26, 9), ('revision', 54, 1), ('event_count', 70, 33), ('padding', 78, 1)]:
        bad(name, base, lambda b, o=offset, v=value: b.__setitem__(o, v))
    bad('empty_segment', base, lambda b: b.__setitem__(slice(30, 46), bytes(16)))
    bad('sequence_max', base, lambda b: b.__setitem__(slice(8, 16), bytes([255]) * 8))
    def overflow(b):
        b[24] = 0; b[46:54] = struct.pack('<Q', (1 << 64) - 4)
    bad('frame_index_overflow', base, overflow)
    def duplicate(b):
        tlvs = b[18:78] * 2; area = struct.pack('<H', len(tlvs)) + tlvs; area += bytes(-len(area) % 8)
        b[:] = b[:16] + area + b[80:]
    bad('duplicate_frame_extension', base, duplicate)
    anchored = next(c for c in valid if c['name'] == 'zero_0_I16')
    bad('unknown_critical_event', anchored, lambda b: b.__setitem__(82, 4))
    bad('event_length', anchored, lambda b: b.__setitem__(84, 15))
    bad('event_offset', anchored, lambda b: b.__setitem__(78, 128))
    for enc in [1, 2]:
        c = next(c for c in valid if c['name'] == f'zero_{enc}_I16')
        bad(f'compressed_trailer_{enc}', c, lambda b: b.append(0))
        bad(f'compressed_corrupt_{enc}', c, lambda b: b.__setitem__(-1, b[-1] ^ 1))
        # With one event, the encoding TLV starts at byte 102.
        bad(f'codec_{enc}', c, lambda b: b.__setitem__(108, 3))
        bad(f'decoded_size_{enc}', c, lambda b: b.__setitem__(110, 255))
        bad(f'checksum_{enc}', c, lambda b: b.__setitem__(114, b[114] ^ 1))
    terminal = next(c for c in valid if c['name'] == 'fault_0_1')
    bad('fault_reserved_code', terminal, lambda b: b.__setitem__(86, 2))
    bad('fault_reserved_flags', terminal, lambda b: b.__setitem__(88, 2))
    bad('fault_reserved_word', terminal, lambda b: b.__setitem__(90, 1))
    bad('unknown_loss_nonzero_count', terminal, lambda b: b.__setitem__(88, 0))
    bad('terminal_payload', terminal, lambda b: b.append(0))
    return {'format': 's7-frame-fixtures-v1', 'unsigned_fields': 'decimal strings', 'valid': valid, 'invalid': invalid}


if __name__ == '__main__':
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 's7transport/tests/fixtures/frames_v1.json'
    target.write_text(json.dumps(generate(), indent=2) + '\n')
