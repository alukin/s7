# S7

S7 is a platform for building, running and observing distributed causal systems: signal processing, software-defined radio, distributed sensing, simulation of dynamic systems. A system is a schema of elements and connections. It is validated, compiled into an executable graph and stepped by headless runtime nodes, which may sit on different machines and exchange typed data streams. Studio is an optional GUI for drawing diagrams, deploying them to nodes and watching them run.

Causality comes before clock time: the result of a schema is defined by its graph, not by the timing of the machines that run it. Network boundaries are explicit elements, and nodes trust each other through X.509 identities over mutual TLS.

The project is in pre-MVP development. APIs, file formats and protocols may still change without backward compatibility.

## Repository

| Directory | Content |
|---|---|
| [`doc/`](doc/README.md) | Language-neutral documentation: vision, formal model, architecture, terminology, architectural decisions, and the control and data-stream protocols that every implementation speaks |
| [`s7l2/`](s7l2/README.md) | C++26 reference implementation: core model and compiler, headless runtime `s7runtime`, transport and lossless data streams, `s7ctl` command-line tool, element plugins, and the Qt6 Studio. Builds on Linux, and cross-builds for Windows on Linux |
| [`s7py/`](s7py/README.md) | Minimal headless node in Python (standard library only): the control protocol, deployments over mTLS, and execution of the built-in F64 elements. It has no data streams yet |

The implementations interoperate through the protocols in `doc/`. Their conformance data lives in `s7l2/s7runtime/tests/fixtures/` (control protocol) and `s7l2/s7transport/tests/fixtures/` (wire format). For example, Studio or `s7ctl` can deploy a diagram to a Python node.

## Where to start

- What S7 is and why: [`doc/S7_VISION.md`](doc/S7_VISION.md), then [`doc/S7_Formal_Model.md`](doc/S7_Formal_Model.md).
- Build and run the C++ runtime and Studio: [`s7l2/README.md`](s7l2/README.md), with its docs index in [`s7l2/docs/INDEX.md`](s7l2/docs/INDEX.md).
- Run a Python node: [`s7py/README.md`](s7py/README.md).
- Write another implementation: [`doc/S7_Control_Protocol_MVP.md`](doc/S7_Control_Protocol_MVP.md) and [`doc/S7_Network_Wire_Protocol_v1.md`](doc/S7_Network_Wire_Protocol_v1.md).

## License

GNU General Public License, version 2 ([`LICENSE`](LICENSE)).

## Development with AI tools

This project was developed with the help of Claude, GitHub Copilot and Codex. The authors will continue to use AI tools to develop faster and to make the software better.
