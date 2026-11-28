# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: CC0-1.0
"""Compact layout for generated diagrams.

Generators place elements on a coarse hand-chosen grid. compact() scales
those positions down, then pushes apart only nodes whose boxes (plus a
margin) collide, so the relative arrangement is kept and nothing overlaps.
fit() sizes the note frame to its text and the sheet to the content, so a
diagram is readable at the zoom that fits it in the window.

Node sizes follow Studio (s7studio/src/diagram/items.hpp): 9 grid steps
wide, max(5, 2*ports + 1) tall, ports counted on the busier side.
"""

import math

GRID_MM = 5.291666666666667  # one 20 px grid step at 96 px per inch
GRID_PX = 20


def _port_counts(wires):
    counts = {}
    for wire in wires:
        for end in ("from", "to"):
            port = wire.get(end, {}).get("port")
            if port:
                node, side, index = port
                entry = counts.setdefault(node, {"in": 0, "out": 0})
                entry[side] = max(entry[side], index + 1)
    return counts


def node_size(node, counts):
    if "size" in node:
        return tuple(node["size"])
    entry = counts.get(node["id"], {"in": 0, "out": 0})
    ports = max(entry["in"], entry["out"], 1)
    if node.get("observer"):
        ports = max(ports, node.get("params", {}).get("channel_count", 1) + 1)  # plus the "+" input
    return 9, max(5, 2 * ports + 1)


def compact(document, scale_x=0.6, scale_y=0.65, gap_x=4, gap_y=2, top=None):
    """Scales node positions and resolves collisions by pushing nodes right."""
    diagram = document["diagram"]
    nodes = diagram["nodes"]
    counts = _port_counts(diagram.get("wires", []))
    for node in nodes:
        x, y = node["position"]
        node["position"] = [round(x * scale_x), round(y * scale_y)]
    if top is not None and nodes:
        shift = top - min(n["position"][1] for n in nodes)
        for node in nodes:
            node["position"][1] += shift
    placed = []
    for node in sorted(nodes, key=lambda n: (n["position"][0], n["position"][1])):
        width, height = node_size(node, counts)
        x, y = node["position"]
        moved = True
        while moved:
            moved = False
            for px, py, pw, ph in placed:
                if x < px + pw + gap_x and px < x + width + gap_x and y < py + ph + gap_y and py < y + height + gap_y:
                    x = px + pw + gap_x
                    moved = True
        node["position"] = [x, y]
        placed.append((x, y, width, height))
    return document


def content_bounds(document):
    diagram = document["diagram"]
    counts = _port_counts(diagram.get("wires", []))
    right = bottom = 0
    for node in diagram["nodes"]:
        width, height = node_size(node, counts)
        right = max(right, node["position"][0] + width)
        bottom = max(bottom, node["position"][1] + height)
    return right, bottom


def note_height_px(text, font_size):
    # font_size is in points (96/72 px each); 1.5 line spacing plus frame padding.
    lines = max(1, text.count("\n") + 1)
    return int(math.ceil(lines * font_size * 96 / 72 * 1.5 + 32))


def fit(document, note_id="notes", font_size=14, margin=4, minimum_mm=(297, 210)):
    """Sizes the note frame to its text and content width, keeps the nodes below
    it, and sizes the sheet to the content."""
    diagram = document["diagram"]
    right, bottom = content_bounds(document)
    for frame in diagram.get("text_frames", []):
        if frame.get("id") != note_id:
            continue
        params = frame["params"]
        params["font_size"] = font_size
        params["width"] = max(400, (right - frame["position"][0]) * GRID_PX)
        params["height"] = note_height_px(params["text"], font_size)
        note_bottom = frame["position"][1] + math.ceil(params["height"] / GRID_PX)
        nodes = diagram["nodes"]
        shift = note_bottom + 2 - min((n["position"][1] for n in nodes), default=note_bottom + 2)
        if shift > 0:
            for node in nodes:
                node["position"][1] += shift
            right, bottom = content_bounds(document)
        bottom = max(bottom, note_bottom)
    width_mm = max(minimum_mm[0], math.ceil((right + margin) * GRID_MM / 10) * 10)
    height_mm = max(minimum_mm[1], math.ceil((bottom + margin) * GRID_MM / 10) * 10)
    document["sheet"] = {"orientation": "landscape" if width_mm >= height_mm else "portrait",
                         "width_mm": float(width_mm), "height_mm": float(height_mm)}
    return document
