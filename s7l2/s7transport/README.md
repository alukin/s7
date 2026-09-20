# s7transport

`s7transport` provides transport-side abstractions for S7 runtime networking.

## Current Scope (v1 draft)

- Runtime endpoint interface: `IRuntimeNetEndpoint`
- Basic packet model: `DataPacket`
- In-memory backend for local integration and smoke testing: `InMemoryRuntimeNetEndpoint`

## Build and Test

```bash
cmake -S . -B build
cmake --build build --target s7transport s7transport_smoke
ctest --test-dir build -R s7transport_smoke --output-on-failure
```


