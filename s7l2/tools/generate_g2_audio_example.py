#!/usr/bin/env python3
# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate examples/audio/two_node_equalizer.s7diagram, the G2 two-node chain.

Node "source" (A) generates a deterministic 48 kHz test programme:
  0-10 s   logarithmic sine sweep 20 Hz -> 20 kHz, amplitude 0.25
  10 s on  five steady tones (100, 400, 1600, 6000, 12000 Hz), 0.08 each
Node "equalizer" (B) runs the bundled AudioEqualizer on the stream and
records the stream as received and the equalizer output (compact binary).
The endpoints are placeholders: tools/run_g2_audio_demo.py and Studio's
Deploy fill in the real nodes. Studio's local Run executes the same graph
in one process (the equivalent local chain).

  python3 tools/generate_g2_audio_example.py           # write the diagram
  python3 tools/generate_g2_audio_example.py --check   # fail if it is stale
"""
import argparse
import copy
import json
from pathlib import Path
import sys

from diagram_layout import compact, fit
from example_runs import with_run
from g2_audio import NODE_A, NODE_B

ROOT = Path(__file__).resolve().parents[1]
PATH = ROOT / "examples" / "audio" / "two_node_equalizer.s7diagram"
RATE = 48000
CHIRP = {"start_hz": 20, "end_hz": 20000, "sweep_time": 10.0, "amplitude": 0.25}
TONE_START = 10.0
TONES = (100, 400, 1600, 6000, 12000)
TONE_AMPLITUDE = 0.08
GAINS = (("low_db", 6.0), ("low_mid_db", 0.0), ("mid_db", -9.0), ("high_mid_db", 3.0), ("high_db", -6.0))


def document():
    library = json.loads((ROOT / "s7core" / "resources" / "schema_elements.s7library").read_text())
    equalizer = copy.deepcopy(next(e["schema"] for e in library["elements"] if e["type_name"] == "AudioEqualizer"))
    elements, connections, nodes, wires, probes = [], [], [], [], []

    def element(ident, kind, node, params, x, y, title=None, schema=None):
        entry = {"id": ident, "type": kind, "params": params, "node": node}
        if schema is not None:
            entry["schema"] = schema
        elements.append(entry)
        nodes.append({"id": ident, "position": [x, y], "title": title or ident})

    def wire(source, target, input_=0, observer=False):
        if not observer:
            connections.append({"from_element": source, "from_port": 0, "to_element": target, "to_port": input_})
        wires.append({"id": f"w{len(wires) + 1}", "from": {"port": [source, "out", 0]}, "to": {"port": [target, "in", input_]},
                      "bends": []})

    # Node A: the programme.
    element("sweep", "ChirpGenerator", NODE_A, {"type": "F64", **CHIRP, "sweep": "logarithmic", "end_behavior": "hold"},
            4, 8, "Sweep 20 Hz-20 kHz, 10 s")
    element("sweep_gate", "StepGenerator", NODE_A, {"initial_value": 1, "final_value": 0, "start_time": TONE_START}, 4, 18,
            "Sweep off at 10 s")
    element("sweep_part", "Multiply", NODE_A, {}, 18, 12)
    wire("sweep", "sweep_part", 0)
    wire("sweep_gate", "sweep_part", 1)
    for i, frequency in enumerate(TONES):
        element(f"tone_{frequency}", "AnalogOscillator", NODE_A, {"type": "F64", "amplitude": TONE_AMPLITUDE, "frequency_hz": frequency},
                4, 28 + 8 * i, f"{frequency} Hz")
        wire(f"tone_{frequency}", "tones", i)
    element("tones", "Adder", NODE_A, {"weights": [1.0] * len(TONES)}, 18, 40, "Five tones")
    element("tone_gate", "StepGenerator", NODE_A, {"initial_value": 0, "final_value": 1, "start_time": TONE_START}, 18, 60,
            "Tones on at 10 s")
    element("tone_part", "Multiply", NODE_A, {}, 32, 46)
    wire("tones", "tone_part", 0)
    wire("tone_gate", "tone_part", 1)
    element("programme", "Adder", NODE_A, {"weights": [1.0, 1.0]}, 46, 28, "Programme (node A)")
    wire("sweep_part", "programme", 0)
    wire("tone_part", "programme", 1)

    # Node B: the equalizer and the recordings.
    element("equalizer", "AudioEqualizer", NODE_B, {}, 66, 28, "AudioEqualizer (node B)", schema=equalizer)
    wire("programme", "equalizer", 0)
    for i, (port, gain) in enumerate(GAINS, start=1):
        element(port, "Constant", NODE_B, {"type": "F64", "value": gain}, 52, 42 + 8 * i, f"{port} = {gain:g} dB")
        wire(port, "equalizer", i)
    for ident, source, description in (("record_in", "programme", "G2 programme as received by node B"),
                                       ("record_out", "equalizer", "G2 AudioEqualizer output")):
        element(ident, "RecordingSink", NODE_B, {"type": "F64", "file_path": f"{ident}.s7b", "sample_rate_hz": RATE,
                                                  "format": "compact-binary", "description": description},
                86, 20 if ident == "record_in" else 36)
        wire(source, ident)
    # Studio observation of a local Run (not of a deployment).
    nodes.append({"id": "scope", "position": [86, 52], "observer": True, "type": "Scope",
                  "params": {"channel_count": 2, "channel_names": ["programme", "equalized"], "channel_units": ["", ""]}})
    for channel, (label, source) in enumerate((("programme", "programme"), ("equalized", "equalizer"))):
        probes.append({"name": label, "element": source, "port": 0, "unit": ""})
        wire(source, "scope", channel, observer=True)

    note = ("G2 TWO-NODE AUDIO EQUALIZER (48 kHz)\n"
            "Node A (source): log sweep 20 Hz-20 kHz for 10 s, then tones at 100, 400, 1600, 6000 and 12000 Hz.\n"
            "Node B (equalizer): bundled AudioEqualizer, gains +6/0/-9/+3/-6 dB; records input and output.\n"
            "Deploy: tools/run_g2_audio_demo.py (two nodes) or Studio Deploy. Run in Studio = the same graph locally;\n"
            "it writes record_in.s7b and record_out.s7b into Studio's working directory. examples/audio/two_node_equalizer.md")
    doc = {
        "id": "two_node_equalizer", "dt": 1 / RATE, "document_kind": "s7.diagram",
        "description": "G2: deterministic audio programme on node A, AudioEqualizer and recordings on node B.",
        "sheet": {"orientation": "landscape", "width_mm": 1000, "height_mm": 450},
        "schema": {"name": "two_node_equalizer", "dt": 1 / RATE, "elements": elements, "connections": connections},
        "probes": probes,
        "diagram": {"grid": {"step": 20, "mm_per_step": 5.291666666666667}, "nodes": nodes, "wires": wires,
                    "text_frames": [{"id": "notes", "position": [2, 2], "params": {"text": note, "width": 2600, "height": 200,
                                                                                   "font_size": 10}}]},
        "runtime_nodes": [{"node_uuid": NODE_A, "name": "source", "endpoint": "wss://127.0.0.1:9201"},
                          {"node_uuid": NODE_B, "name": "equalizer", "endpoint": "wss://127.0.0.1:9202"}],
    }
    fit(compact(doc))
    return with_run(doc, PATH)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--check", action="store_true", help="exit 1 if the saved diagram differs from the generated one")
    args = p.parse_args()
    text = json.dumps(document(), indent=2) + "\n"
    if args.check:
        if not PATH.exists() or PATH.read_text() != text:
            print(f"{PATH} is missing or stale; run {Path(__file__).name}", file=sys.stderr)
            return 1
        print(f"{PATH.name} is current")
        return 0
    PATH.write_text(text)
    print(f"wrote {PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
