# SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
# SPDX-License-Identifier: GPL-3.0-or-later

"""Runs a schema locally, without a node or network, and prints element outputs step by step.

    python3 -m s7node.trace SCHEMA [--lib LIB]... [--steps N] [--rate HZ] [--watch ID[:PORT]]... [--order]
    python3 -m s7node.trace --list | --describe TYPE [--lib LIB]...

SCHEMA is a schema (elements, connections, optional dt) or an .s7diagram, whose dt is the model-time
step every element must agree with, as when it is deployed; its elements run here in one graph,
whatever node each is placed on, and stream ends run without peers. The output is CSV: one row per
step with the outputs after that step, written with repr(), so equal text means equal bits.
"""
import argparse
import csv
import json
import sys
from pathlib import Path

from s7core.errors import SchemaError
from s7core.rate import rate_from_seconds, rate_text, schema_rate

from .elements import NODE_ELEMENT_TYPES
from .library import LibraryError, load_libraries
from .runtime import Executor


def load_schema(path: str, rate_hz: float):
    """(schema, rate, model_time): an .s7diagram's schema runs at its dt, which every element must match."""
    document = json.loads(Path(path).read_text())
    if not isinstance(document, dict):
        raise SchemaError("a schema must be a JSON object")
    if isinstance(document.get("schema"), dict):
        if "dt" not in document:
            raise SchemaError("a diagram needs its model-time step 'dt'")
        return document["schema"], schema_rate(document), True
    return document, schema_rate(document, rate_hz), False


def columns(graph, watches):
    """(header, element, port) for each watched output; all outputs by default."""
    if not watches:
        watches = [f"{e.id}:{p}" if e.output_count > 1 else e.id
                   for e in graph.elements.values() for p in range(e.output_count)]
    out = []
    for watch in watches:
        element_id, _, port = watch.partition(":")
        element = graph.elements.get(element_id)
        if element is None:
            raise SchemaError(f"--watch {watch}: no element '{element_id}'")
        index = int(port) if port.isdigit() else 0
        if index >= element.output_count:
            raise SchemaError(f"--watch {watch}: '{element_id}' has {element.output_count} outputs")
        out.append((watch, element, index))
    return out


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(prog="s7node.trace", description=__doc__.split("\n\n")[0])
    parser.add_argument("schema", nargs="?", help="schema or .s7diagram file")
    parser.add_argument("--lib", action="append", default=[], metavar="LIBRARY",
                        help="element library: a .py file or a module name (repeatable)")
    parser.add_argument("--steps", type=int, default=10, help="number of steps (default 10)")
    parser.add_argument("--rate", type=float, default=0.0, metavar="HZ",
                        help="rate of a schema without dt whose elements pin none")
    parser.add_argument("--watch", action="append", default=[], metavar="ID[:PORT]",
                        help="output to print (repeatable)")
    parser.add_argument("--order", action="store_true", help="print the causal order and causal memory first")
    parser.add_argument("--list", action="store_true", help="list the element types and exit")
    parser.add_argument("--describe", metavar="TYPE", help="print a type's catalog entry (JSON) and exit")
    args = parser.parse_args(argv)
    out = sys.stdout
    try:
        types = load_libraries(args.lib, NODE_ELEMENT_TYPES)
        if args.list:
            for name in types:
                out.write(f"{name}\n")
            return 0
        if args.describe:
            if args.describe not in types:
                raise SchemaError(f"unknown element type '{args.describe}'")
            json.dump(types[args.describe].describe(args.describe), out, indent=2)
            out.write("\n")
            return 0
        if not args.schema:
            parser.error("a schema file is needed")
        schema, rate, model_time = load_schema(args.schema, args.rate)
        graph = Executor(types=types).build(schema, rate)
        if model_time:
            graph.require_model_time_rate(rate)
        watched = columns(graph, args.watch)
    except (OSError, ValueError, SchemaError, LibraryError) as error:  # ValueError: bad JSON too
        print(f"error: {error}", file=sys.stderr)
        return 1
    if args.order:
        out.write(f"# rate: {rate_text(rate) if rate else 'pinned by elements'}\n")
        out.write(f"# causal order: {' '.join(e.id for e in graph.causal_order) or '-'}\n")
        out.write(f"# causal memory: {' '.join(e.id for e in graph.memory_order) or '-'}\n")
        if graph.trapezoids:
            out.write(f"# trapezoid stage: {' '.join(e.id for e in graph.trapezoids)}; "
                      f"reevaluated: {' '.join(e.id for e in graph.trapezoid_rhs) or '-'}\n")
    period = 1 / rate if rate else None
    writer = csv.writer(out, lineterminator="\n")
    writer.writerow(["step"] + (["t"] if period else []) + [name for name, _, _ in watched])
    for n in range(args.steps):
        try:
            graph.step()
        except Exception as error:  # an execution error: the rows so far stand
            out.flush()
            print(f"error at step {n}: {type(error).__name__}: {error}", file=sys.stderr)
            return 1
        time = [repr(float(n * period))] if period else []
        writer.writerow([n] + time + [repr(element.outputs[port]) for _, element, port in watched])
    graph.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
