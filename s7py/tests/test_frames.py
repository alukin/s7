# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
import concurrent.futures
from dataclasses import replace
import json
from pathlib import Path
import threading
import time
import unittest
import uuid

from s7node import frames as f, wire
from s7node.stream import MemorySequenceStore
from tools.frame_interop import run

FIXTURES = Path(__file__).resolve().parents[2] / 's7l2/s7transport/tests/fixtures/frames_v1.json'


def descriptor(encoding=f.Encoding.RAW, packets=2):
    return f.Descriptor(data_type=wire.TYPES['I16'], channels=('L', 'R'), max_frames_per_packet=8,
                        window_packets=packets, window_frames=packets * 8, encoding=encoding)


def pair(d):
    send = f.BufferPool(d.window_packets, f.MAX_MESSAGE_BYTES)
    receive = f.BufferPool(d.window_packets, wire.MAX_PAYLOAD_BYTES)
    return f.FrameSink(d, MemorySequenceStore(), send), f.FrameSource(d, receive)


def pump(sink, source, peer='a'):
    while (message := sink.next_message(peer)) is not None:
        with message:
            assert source.on_message(message.bytes()), source.status


class FrameTests(unittest.TestCase):
    def test_shared_fixtures(self):
        data = json.loads(FIXTURES.read_text())
        for group in ('valid', 'invalid'):
            for c in data[group]:
                if not f.encoding_available(f.Encoding(c['encoding'])):
                    continue
                with self.subTest(group=group, name=c['name']):
                    if group == 'valid':
                        self.assertEqual(run({'op': 'decode', 'case': c})['status'], 'ok')
                        emitted = run({'op': 'encode', 'case': c})
                        self.assertEqual(run({'op': 'decode', 'case': dict(c, message_hex=emitted['message_hex'])})['status'], 'ok')
                    else:
                        with self.assertRaises((wire.FormatError, BufferError)):
                            run({'op': 'decode', 'case': c})

    def test_consumption_replay_and_events(self):
        for encoding in f.Encoding:
            if not f.encoding_available(encoding):
                continue
            with self.subTest(encoding=encoding):
                sink, source = pair(descriptor(encoding))
                sink.resume('a', 0); sink.resume('b', 0)
                raw = bytes(range(64))
                self.assertEqual(sink.publish_copy(raw, 16, [f.clock_anchor(3, -123), f.clock_anchor(10, 456)]).frames, 16)
                messages = []
                while (message := sink.next_message('a')) is not None:
                    with message:
                        messages.append(message.bytes())
                        self.assertTrue(source.on_message(message.bytes()))
                self.assertEqual((source.next_expected, source.consumed_up_to), (2, 0))
                self.assertFalse(source.on_message(messages[0]))
                sink.detach('a'); sink.resume('a', 2)
                self.assertEqual(len(sink.log), 2)
                preview = source.front_packet()
                self.assertEqual(preview.info.events[0].offset, 3)
                first = source.receive_copy(3)
                self.assertEqual((first.frames, first.info.events, source.consumed_up_to), (3, (), 0))
                rest = source.receive_copy(100)
                self.assertEqual((rest.frames, rest.info.first_frame, rest.info.events[0].offset), (5, 3, 0))
                second = source.receive_copy(100)
                self.assertEqual(second.info.events[0].offset, 2)
                self.assertEqual(first.payload + rest.payload + second.payload, raw)
                sink.acknowledge('a', 2)
                self.assertEqual(len(sink.log), 2)
                sink.detach('b'); sink.resume('b', 0)
                for original in messages:
                    with sink.next_message('b') as replay:
                        self.assertEqual(replay.bytes(), original)
                sink.acknowledge('b', 2)
                self.assertEqual(len(sink.log), 0)
                self.assertEqual(source.pool.leased_slots, 1)
                preview.close()
                self.assertEqual(source.pool.leased_slots, 0)
                sink.close(); source.close()

    def test_fault_reservation_and_recovery(self):
        for encoding in f.Encoding:
            if not f.encoding_available(encoding):
                continue
            with self.subTest(encoding=encoding):
                sink, source = pair(descriptor(encoding, 1)); sink.resume('a', 0)
                r = sink.publish_copy(bytes(64), 16)
                self.assertEqual((r.frames, r.status), (8, f.Status.BACKPRESSURE))
                sink.fault(f.Status.SOURCE_OVERFLOW)
                self.assertEqual(sink.state, 'draining_fault')
                pump(sink, source)
                self.assertEqual(source.status, f.Status.OK)
                r = source.receive_copy(100)
                self.assertEqual((r.frames, r.status, r.lost_frames, source.consumed_up_to), (8, f.Status.SOURCE_OVERFLOW, None, 2))
                sink.acknowledge('a', 2)
                segment = uuid.uuid4().bytes
                sink.recover(segment); source.recover(2, segment)
                self.assertEqual(len(source.history), 1)
                self.assertEqual(sink.publish_copy(bytes(4), 1).frames, 1)
                pump(sink, source)
                r = source.receive_copy(1)
                self.assertEqual((r.info.first_frame, r.info.segment), (0, segment))
                source.sink_underflow(11)
                self.assertEqual(source.receive_copy(1).lost_frames, 11)
                sink.close(); source.close()

    def test_initial_loss_and_shared_publication(self):
        sink, source = pair(descriptor()); sink.resume('a', 0)
        pool = f.BufferPool(1, 32)
        lease = pool.copy(bytes(32))
        self.assertEqual(sink.publish_shared(lease, 8).frames, 8)
        lease.close(); self.assertEqual(pool.leased_slots, 1)
        pump(sink, source); sink.resume('a', 1)
        self.assertEqual(pool.leased_slots, 1)
        source.consume(8); sink.acknowledge('a', 1)
        self.assertEqual(pool.leased_slots, 0)
        sink.close(); source.close()
        sink, source = pair(descriptor()); sink.resume('a', 0)
        sink.fault(f.Status.CONTINUITY_LOST, f.U64); pump(sink, source)
        self.assertEqual(source.consume(0).lost_frames, f.U64)
        sink.close(); source.close()

    def test_pool_lifetimes_cancellation_close(self):
        budget = f.BufferBudget(16)
        pool = f.BufferPool(1, 16, budget)
        held = pool.copy(b'123'); sliced = held.slice(1, 1); held.close()
        self.assertIsNone(pool.copy(b'456'))
        with concurrent.futures.ThreadPoolExecutor(1) as workers:
            waiter = workers.submit(pool.wait_copy, b'456', time.monotonic() + 2)
            sliced.close(); held = waiter.result(2)
            self.assertEqual(held.bytes(), b'456')
            cancel = threading.Event()
            waiter = workers.submit(pool.wait_copy, b'789', time.monotonic() + 2, cancel)
            cancel.set(); self.assertIsNone(waiter.result(2))
            waiter = workers.submit(pool.wait_copy, b'789', time.monotonic() + 2)
            pool.close(); self.assertIsNone(waiter.result(2))
        self.assertEqual(budget.reserved_bytes, 16)
        self.assertEqual(held.bytes(), b'456')
        held.close(); self.assertEqual(budget.reserved_bytes, 0)

    def test_invalid_transitions_and_bounds(self):
        d = descriptor(packets=1)
        sink, source = pair(d); sink.resume('a', 0)
        sink.publish_copy(bytes(32), 8)
        with sink.next_message('a') as message:
            raw = message.bytes(); self.assertTrue(source.on_message(raw))
        preview = source.front_packet(); source.consume(8)
        sink.acknowledge('a', 1)
        sink.publish_copy(bytes(32), 8)
        with sink.next_message('a') as message:
            self.assertFalse(source.on_message(message.bytes()))
        self.assertEqual(source.status, f.Status.MALFORMED)
        self.assertEqual(source.next_expected, 1)
        self.assertEqual(preview.payload.bytes(), bytes(32)); preview.close()
        sink.close(); source.close()
        for update in [dict(channels=('x', 'x')), dict(data_type=wire.TYPES['I12']), dict(max_frames_per_packet=1 << 32), dict(frame_rate=(0, 1))]:
            with self.assertRaises(wire.FormatError):
                replace(d, **update).validate()
        for encoding in f.Encoding:
            if not f.encoding_available(encoding):
                with self.assertRaises(wire.FormatError):
                    f.Codec(descriptor(encoding))

    def test_concurrent_transfer(self):
        sink, source = pair(descriptor()); sink.resume('a', 0)
        deadline = time.monotonic() + 5
        def produce():
            for _ in range(200):
                while sink.publish_copy(bytes(32), 8).frames != 8:
                    if time.monotonic() > deadline:
                        raise TimeoutError('producer stalled')
                    time.sleep(0)
        with concurrent.futures.ThreadPoolExecutor(1) as workers:
            writer = workers.submit(produce)
            for index in range(200):
                message = sink.next_message('a')
                while message is None:
                    if time.monotonic() > deadline:
                        raise TimeoutError('reader stalled')
                    time.sleep(0); message = sink.next_message('a')
                with message:
                    self.assertTrue(source.on_message(message.bytes()))
                r = source.receive_copy(8)
                self.assertEqual((r.frames, r.info.first_frame, r.payload), (8, index * 8, bytes(32)))
                sink.acknowledge('a', source.consumed_up_to)
            writer.result(5)
        sink.close(); source.close()


if __name__ == '__main__':
    unittest.main()
