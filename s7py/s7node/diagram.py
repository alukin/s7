# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""A node's part of a deployed diagram (s7net/src/diagram_deployment.cpp).

A diagram's schema places each element on a node ("node": node_uuid). The
part of a node is its elements and the connections between them. A
connection between nodes must be carried by a data stream; s7py has no data
plane yet, so a part with a stream end is refused.
"""
from .graph import SchemaError


def diagram_schema(diagram: dict) -> dict:
    schema = diagram.get("schema", diagram)
    if not isinstance(schema, dict):
        raise SchemaError("'schema' must be an object")
    return schema


def _elements(schema: dict) -> list:
    elements = schema.get("elements", [])
    if not isinstance(elements, list) or not all(isinstance(e, dict) for e in elements):
        raise SchemaError("schema: 'elements' must be an array of objects")
    return elements


def placed_nodes(diagram: dict) -> list[str]:
    nodes = []
    for element in _elements(diagram_schema(diagram)):
        node = element.get("node")
        if isinstance(node, str) and node not in nodes:
            nodes.append(node)
    return nodes


def model_time_step_ns(diagram: dict) -> int | None:
    """The model-time step `dt` (seconds) in whole nanoseconds, or None."""
    dt = diagram.get("dt")
    if dt is None:
        return None
    if isinstance(dt, bool) or not isinstance(dt, (int, float)) or not (1.0 <= dt * 1e9 <= 9e18):
        raise SchemaError("'dt' must be a positive number of seconds (at least 1 ns)")
    return round(dt * 1e9)


def extract_node_schema(diagram: dict, node_uuid: str) -> dict:
    schema = diagram_schema(diagram)
    placement = {e.get("id"): e.get("node") for e in _elements(schema)}

    streams = diagram.get("streams", [])
    if not isinstance(streams, list):
        raise SchemaError("'streams' must be an array")
    for stream in streams:
        ends = [stream.get("producer", {})] + list(stream.get("consumers", [])) if isinstance(stream, dict) else []
        if any(isinstance(end, dict) and end.get("node_uuid") == node_uuid for end in ends):
            raise SchemaError(f"stream {stream.get('stream_id')} has an end on this node; s7py has no data plane yet")

    name = schema.get("name") if isinstance(schema.get("name"), str) else "diagram"
    elements = []
    for element in _elements(schema):
        if placement.get(element.get("id")) == node_uuid:
            elements.append({key: value for key, value in element.items() if key != "node"})
    connections = []
    for c in schema.get("connections", []):
        if not isinstance(c, dict):
            raise SchemaError("schema: a connection must be an object")
        from_here = placement.get(c.get("from_element")) == node_uuid
        to_here = placement.get(c.get("to_element")) == node_uuid
        if from_here and to_here:
            connections.append(dict(c))
        elif from_here or to_here:
            raise SchemaError(
                f"connection {c.get('from_element')}:{c.get('from_port')} -> {c.get('to_element')}:{c.get('to_port')} "
                f"{'leaves' if from_here else 'enters'} this node without a stream"
            )
    return {"name": f"{name}@{node_uuid}", "elements": elements, "connections": connections}
