# S7 Net Element Contract

## Status

Draft v1 contract for `s7net` element abstractions and runtime integration.

---

## Purpose

Define `s7net` as an element library with transport-agnostic base classes and runtime-service integration points.

Design rule:

- `s7net` elements do not own transport backends directly.
- `s7net` elements consume runtime network service abstractions.
- `s7net` may depend on transport contracts exposed by `s7transport`, while backend implementations remain outside `s7net`.

---

## Base Abstractions

## `NetSink`

Intent:

- abstract output boundary element that publishes element outputs to a network/service target

Constraints:

- transport-agnostic
- no direct socket/session ownership in base class
- no assumptions specific to inter-runtime protocol

## `NetSource`

Intent:

- abstract input boundary element that receives network/service inputs into graph ports

Constraints:

- transport-agnostic
- no direct socket/session ownership in base class
- no assumptions specific to inter-runtime protocol

---

## Recommended Class Layering

- `NetSink` / `NetSource` (abstract bases, generic semantics)
- `RuntimeNetSink` / `RuntimeNetSource` (runtime-service-backed inter-runtime elements)
- external adapters (system/protocol-specific):
  - examples: `PipeWireSinkElement`, `MqttSourceElement` (names illustrative)

This preserves reuse of the base abstractions across multiple integration domains.

External/system adapters remain in `s7net` for v1.

---

## Runtime Service Dependency

`RuntimeNetSink`/`RuntimeNetSource` should depend on a narrow runtime-facing service interface.

Suggested element-facing view:

```cpp
class IRuntimeNetEndpoint {
public:
    virtual ~IRuntimeNetEndpoint() = default;

    virtual EndpointHandle open_endpoint(const EndpointOpenRequest&) = 0;
    virtual void close_endpoint(const EndpointHandle&) = 0;

    virtual SendResult publish(const EndpointHandle&, const DataPacket&) = 0;
    virtual ReceiveResult receive(const EndpointHandle&, DataPacket&) = 0;
};
```

`IRuntimeNetEndpoint` is provided by `s7runtime`, implemented via `s7transport`.

---

## Element-Side Semantics

## `NetSink` behavior contract

- consumes one or more input channels
- serializes/batches according to configured format profile
- delegates delivery to runtime service
- respects runtime backpressure and window updates
- does not silently drop samples

## `NetSource` behavior contract

- obtains packets from runtime service
- reconstructs typed output values
- enforces expected ordering and timestamp constraints
- emits explicit fault state on protocol violations

---

## Configuration Surface

Keep base config generic:

- endpoint logical name
- stream/channel identifiers
- type profile
- timestamp mode
- buffering hints

Keep transport-specific options in runtime or specialized derived elements, not in abstract bases.

---

## Error Model

Element-level errors should map to runtime/network faults consistently.

Categories:

- `AUTHORIZATION_ERROR`
- `TYPE_MISMATCH`
- `RESOURCE_EXHAUSTED`
- `ORDERING_VIOLATION`
- `TRANSPORT_UNAVAILABLE`

Elements should expose fault status without mutating unrelated graph semantics.

Priority policy alignment:

- control traffic > element-connection data > observation traffic
- observation-related element flows must be treated as lower priority than functional graph data flows

---

## Integration with Existing Architecture

This contract aligns with:

- explicit network boundary principle (`S7_Architecture_Overview.md`)
- distributed addressing and routing (`DISTRIBUTED_ADDRESSING.md`)
- runtime orchestration (`S7_Runtime_Architecture.md`)

---

## Open Questions

No unresolved contract questions remain for v1 baseline in this document.

