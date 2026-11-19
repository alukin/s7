# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""The network boundary: data streams to other nodes (s7net net.hpp)."""
import struct
import time

from s7core.element import Element
from s7core.errors import SchemaError
from s7core.params import _integer
from s7core.spec import GENERIC, Param, Port, T


class NetContext:
    """The node's stream table (transport.StreamTable); None: a local run without peers, where
    NetSink drops its input and NetSource outputs nothing new."""

    table = None


NET = NetContext()
_INT64_MAX = 2**63 - 1
# The stream of a stream end, read by _net_config; an omitted count takes the stream contract's
# default (stream.StreamConfig).
NET_PARAMETERS = GENERIC + (
    Param("stream_id", "string", None, "Stream UUID", required=True),
    Param("max_elements_per_packet", "int", None, "Values per DATA packet at most", 1, _INT64_MAX),
    Param("max_batch_latency_ms", "int", None, "Longest wait to fill a packet, ms", 0, _INT64_MAX),
    Param("send_log_max_packets", "int", None, "Unacknowledged packets kept for replay at most", 1, _INT64_MAX),
    Param("max_block_ms", "int", None, "Longest wait for room in the send log, ms", 0, _INT64_MAX),
    Param("max_wait_ms", "int", None, "Longest wait for the next value, ms", 0, _INT64_MAX),
    Param("producer_node", "string", None, "UUID of the producer node"),
    Param("consumer_nodes", "object", None, "UUIDs of the consumer nodes"),
    Param("max_latency_ms", "int", None, "End-to-end latency budget, ms", -_INT64_MAX - 1, _INT64_MAX),
    Param("sample_rate", "object", None, "Stream sample rate [numerator, denominator]"),
    Param("timestamps", "object", None, "Timestamp roles carried with the values"))
NET_PARAMS = tuple(p.name for p in NET_PARAMETERS)


def _net_config(params: dict, type_name: str):
    """config_from_params (net.cpp): the stream of a NetSink / NetSource."""
    from . import stream, wire

    if "stream_id" not in params:
        raise SchemaError(f"{type_name} needs a 'stream_id' (UUID).")
    if not isinstance(params["stream_id"], str):
        raise SchemaError("parameter 'stream_id' must be a string")
    try:
        config = stream.StreamConfig(wire.parse_stream_id(params["stream_id"]), wire.wire_type("F64"))
    except ValueError as error:
        raise SchemaError(str(error)) from None

    def count(name: str, fallback: int) -> int:
        value = _integer(params, name, fallback, -_INT64_MAX - 1, _INT64_MAX)
        zero_allowed = name in ("max_batch_latency_ms", "max_block_ms", "max_wait_ms")
        if value < 0 or (value == 0 and not zero_allowed):
            raise SchemaError(f"{type_name} parameter '{name}' is out of range.")
        return value

    config.max_elements_per_packet = count("max_elements_per_packet", config.max_elements_per_packet)
    config.max_batch_latency_ms = count("max_batch_latency_ms", config.max_batch_latency_ms)
    config.send_log_max_packets = count("send_log_max_packets", config.send_log_max_packets)
    config.max_block_ms = count("max_block_ms", config.max_block_ms)
    config.max_wait_ms = count("max_wait_ms", config.max_wait_ms)
    for name in ("producer_node",):
        if name in params and not isinstance(params[name], str):
            raise SchemaError(f"parameter '{name}' must be a string")
    config.producer_node = params.get("producer_node", "")
    if "max_latency_ms" in params:
        config.max_latency_ms = _integer(params, "max_latency_ms", 0, -_INT64_MAX - 1, _INT64_MAX)
    for name in ("sample_rate", "timestamps", "consumer_nodes"):
        if name in params and not isinstance(params[name], list):
            raise SchemaError(f"parameter '{name}' must be an array")
    if "sample_rate" in params:
        pair = params["sample_rate"]
        if len(pair) != 2 or not all(isinstance(v, int) and not isinstance(v, bool) and v > 0 for v in pair):
            raise SchemaError(f"{type_name} parameter 'sample_rate' must be [numerator, denominator], both positive.")
        config.sample_rate = (pair[0], pair[1])
    if "timestamps" in params:
        roles = []
        for role in params["timestamps"]:
            if not isinstance(role, str):
                raise SchemaError(f"{type_name} parameter 'timestamps' must list role names.")
            if role not in wire.ROLES:
                raise SchemaError(f"Unknown timestamp role '{role}' (MODEL_TIME, SOURCE_TIME or PROCESSING_TIME).")
            roles.append(wire.ROLES[role])
        if len(set(roles)) != len(roles):
            raise SchemaError(f"{type_name} parameter 'timestamps' repeats a role.")
        config.timestamp_roles = sorted(roles)
    if "consumer_nodes" in params:
        if not all(isinstance(node, str) for node in params["consumer_nodes"]):
            raise SchemaError(f"{type_name} parameter 'consumer_nodes' must list node UUIDs.")
        config.consumer_nodes = list(params["consumer_nodes"])
    return config


class NetStreamElement(Element):
    """A stream end. Opens its stream on the first step and closes it with the graph; a redeployed
    graph reopens it and continues it. A stream fault makes the step raise."""

    category = "network"
    type_variables = (T,)
    parameters = NET_PARAMETERS
    stateful = True
    role = ""

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.config = _net_config(params, type(self).__name__)
        self.handle = None
        self.table = None

    def ensure_stream(self) -> bool:
        if self.handle is not None:
            return True
        if NET.table is None:
            return False
        self.table = NET.table
        self.handle = self.table.open(self.config, self.role)
        return True

    def close(self) -> None:
        if self.handle is not None:
            self.table.close(self.handle)
            self.handle = None

    def raise_fault(self, code: str):
        from . import wire
        raise RuntimeError(f"{type(self).__name__} stream {wire.stream_id_text(self.config.stream_id)} faulted: {code}.")


class NetSink(NetStreamElement):
    """Publishes its input to the stream. Without a sample rate it stamps PROCESSING_TIME (UTC)."""

    description = "Publishes its input to a data stream to other nodes (added by the node, not drawn)"
    input_ports = (Port("in", "Input", type_rule="T"),)
    output_ports = ()
    role = "producer"

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        from . import wire
        if self.config.explicit_timestamps and any(role != wire.PROCESSING_TIME for role in self.config.timestamp_roles):
            role = next(r for r in self.config.timestamp_roles if r != wire.PROCESSING_TIME)
            raise SchemaError(f"NetSink stream {wire.stream_id_text(self.config.stream_id)}: {wire.ROLE_NAMES[role]} without "
                              "a sample_rate is not supported (a NetSink stamps PROCESSING_TIME only).")

    def step(self, inputs):
        if not self.ensure_stream():
            return  # no network endpoint: a local run without peers
        bits = int.from_bytes(struct.pack("<d", inputs[0]), "little")
        times = []
        if self.config.explicit_timestamps:
            now = time.time_ns()
            times = [(role, now) for role in self.config.timestamp_roles]
        fault = self.table.publish(self.handle, [(bits,)], times)
        if fault:
            self.raise_fault(fault)


class NetSource(NetStreamElement):
    """Outputs the next value of the stream, waiting for it (up to max_wait_ms)."""

    description = "Outputs the next value of a data stream from another node (added by the node, not drawn)"
    output_ports = (Port("out", "Output", type_rule="T"),)
    role = "consumer"

    def __init__(self, element_id, params):
        super().__init__(element_id, params)
        self.last_timestamps = []  # the times of the value output by the last step (empty: unknown)

    def step(self, inputs):
        if not self.ensure_stream():
            return
        elements, fault, times = self.table.receive(self.handle, 1)
        if elements:
            self.outputs[0] = struct.unpack("<d", elements[0][0].to_bytes(8, "little"))[0]
        if fault:
            self.raise_fault(fault)
        self.last_timestamps = times
