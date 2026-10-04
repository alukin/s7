# s7transport

`s7transport` provides transport-side abstractions for S7 runtime networking.

## Current Scope (prototype)

- Runtime endpoint interface: `IRuntimeNetEndpoint`
- Basic packet model: `DataPacket`
- In-memory backend for local integration and smoke testing: `InMemoryRuntimeNetEndpoint`

The current Beast packet codec is a prototype and is not conformant to [the candidate inter-node wire protocol](../docs/S7_Network_Wire_Protocol_v1.md). Its public codec functions are named `encode_packet_prototype` and `decode_packet_prototype` to avoid implying v1 interoperability. Do not use this layout for cross-language clients.

## Build and Test

```bash
cmake -S . -B build
cmake --build build --target s7transport s7transport_smoke
ctest --test-dir build -R s7transport_smoke --output-on-failure
```

