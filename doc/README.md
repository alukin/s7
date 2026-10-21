# S7 Documentation

Language-neutral documentation of S7: what it is, its computational model and architecture, and the protocols every implementation speaks. Implementation documentation lives with each implementation (`../s7l2/docs/INDEX.md` for C++ and Studio, `../s7py/README.md` for Python).

Paths such as `s7l2/s7runtime/...` in these documents are relative to the repository root.

## Concepts and architecture

| Document | Content |
|---|---|
| [S7_VISION.md](S7_VISION.md) | Why S7 exists, goals and non-goals |
| [S7_Formal_Model.md](S7_Formal_Model.md) | Computational model: Element, Schema, Connection, State, causal memory |
| [S7_Architecture_Overview.md](S7_Architecture_Overview.md) | Core principles and architectural invariants |
| [S7_Terminology.md](S7_Terminology.md) | Official glossary |
| [S7_Architectural_Decisions.md](S7_Architectural_Decisions.md) | ADR log (ADR-001 … ADR-019) |
| [S7_Runtime_Architecture.md](S7_Runtime_Architecture.md) | Runtime pipeline: validate → compile → execute → observe |
| [S7_Observation_Architecture.md](S7_Observation_Architecture.md) | Read-only observation subsystem, probes, Studio as observation client |
| [S7_Sample_Rate_Model.md](S7_Sample_Rate_Model.md) | Proposed design: rate/`dt` as an inferred port property, mirroring the type system (not implemented) |

## Distributed runtime and protocols

| Document | Content |
|---|---|
| [S7_Control_Protocol_MVP.md](S7_Control_Protocol_MVP.md) | Control protocol: discovery, node info, prepare/commit deployment |
| [S7_Network_Wire_Protocol_v1.md](S7_Network_Wire_Protocol_v1.md) | Binary inter-node data-stream wire format |
| [S7_Net_Element_Contract.md](S7_Net_Element_Contract.md) | `NetSink` / `NetSource` element contract |
| [REMOTE_NODE_INFO_AND_DEPLOYMENT.md](REMOTE_NODE_INFO_AND_DEPLOYMENT.md) | Node capability descriptor, discovery/membership, deployment model, message JSON examples |
| [IDENTITY_AND_CREDENTIAL_PROVISIONING.md](IDENTITY_AND_CREDENTIAL_PROVISIONING.md) | X.509 node identity, Studio authorization, credential lifecycle |
| [DISTRIBUTED_ADDRESSING.md](DISTRIBUTED_ADDRESSING.md) | Node, runtime, schema-instance, and element-instance identifiers |

## Conformance data

Shared by all implementations: `s7l2/s7runtime/tests/fixtures/control_v1.json` (control protocol conversation, see its `README.md`) and `s7l2/s7transport/tests/fixtures/wire_v1.json` (wire format and stream descriptors).
