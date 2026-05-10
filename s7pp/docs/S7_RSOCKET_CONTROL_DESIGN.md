# S7 RSocket Control Plane Design

**Date:** 2026-05-10  
**Status:** Design Phase (Implementation planning)  
**Scope:** Network server startup + RSocket responder for control channel + JSON-RPC dispatch

---

## Architecture Overview

### Layer Stack (per connection)

```
┌─────────────────────────────────┐
│  Remote Client (CLI/GUI)        │
├─────────────────────────────────┤
│  RSocket Client (yarpl::Single) │ ← requestResponse
│  RSocket Client (yarpl::Flowable)├ ← requestStream (events)
├─────────────────────────────────┤
│  TcpConnectionFactory           │
├─────────────────────────────────┤
│  [TCP/IPv4/IPv6 Network]        │ ← Network Boundary
├─────────────────────────────────┤
│  TcpConnectionAcceptor          │
├─────────────────────────────────┤
│  RSocketServer                  │ (multiplexer, framing)
├─────────────────────────────────┤
│  S7NodeResponder                │ (RSocket protocol handler)
│    ├─ handleRequestResponse     │ ← JSON-RPC request dispatch
│    └─ handleRequestStream       │ ← events.subscribe
├─────────────────────────────────┤
│  JsonRpcDispatcher              │ (method router)
│    ├─ runtime.hello             │
│    ├─ schema.load               │
│    ├─ schema.run                │
│    ├─ schema.pause              │
│    ├─ schema.stop               │
│    └─ ... (all v0 methods)      │
├─────────────────────────────────┤
│  NodeRuntimeService             │ (control logic)
│    ├─ handleLoadSchema()        │
│    ├─ handleRunSchema()         │
│    ├─ handlePauseRun()          │
│    ├─ getStatus()               │
│    └─ eventBus_                 │
├─────────────────────────────────┤
│  Schema (live runtime)          │ (execution)
├─────────────────────────────────┤
│  Element Registry (singleton)   │
└─────────────────────────────────┘
```

### Key Components to Implement

#### 1. **NodeRuntimeService**
Location: `s7core/include/s7runtime.hpp` + `s7core/src/s7runtime.cpp`

**Responsibilities:**
- Manage schema handles (schema_id → Schema mapping)
- Track active runs (run_id → run state)
- Enforce single-active-run per node policy
- Publish events (run started, paused, stopped, etc.)
- Expose methods matching v0 protocol
- Thread-safe handle table + run table

**Public API:**
```cpp
class NodeRuntimeService {
public:
  // Schema lifecycle
  struct LoadResult {
    std::string schema_handle;
    std::string schema_id;
    int num_elements;
    int num_connections;
  };
  
  LoadResult loadSchema(const std::string& schema_json);
  LoadResult loadSchemaFromFile(const std::string& path);
  
  void validateSchema(const std::string& schema_handle);
  
  struct RunResult {
    std::string run_id;
    bool accepted;
  };
  
  RunResult runSchema(const std::string& schema_handle, 
                      long ticks);  // or RunMode with seconds
  
  void pauseRun(const std::string& run_id);
  void resumeRun(const std::string& run_id);
  void stopRun(const std::string& run_id);
  void resetSchema(const std::string& schema_handle);
  void unloadSchema(const std::string& schema_handle);
  
  // Query
  struct RuntimeStatus {
    long uptime_ms;
    std::vector<std::string> active_schema_handles;
    std::vector<std::string> active_runs;
    std::string health_state;
  };
  
  RuntimeStatus getStatus() const;
  
  // Events
  using EventCallback = std::function<void(const Event&)>;
  void subscribeToEvents(const std::vector<std::string>& topics,
                         EventCallback callback);
  
private:
  std::unordered_map<std::string, SchemaPtr> schemas_;
  std::unordered_map<std::string, RunState> runs_;
  std::mutex state_mutex_;
  EventBus event_bus_;
};
```

#### 2. **Event System**
Location: `s7core/include/s7events.hpp` + `s7core/src/s7events.cpp`

**Responsibilities:**
- Publish events (run.started, schema.loaded, etc.)
- Manage subscriptions
- Thread-safe event delivery

**API:**
```cpp
struct Event {
  std::string event_name;
  long timestamp_ms;
  nlohmann::json payload;
};

class EventBus {
public:
  void publish(const Event& evt);
  
  using Subscriber = std::function<void(const Event&)>;
  
  uint64_t subscribe(const std::vector<std::string>& topics,
                     Subscriber callback);
  void unsubscribe(uint64_t subscription_id);
  
private:
  std::map<std::vector<std::string>, std::vector<Subscriber>> subscriptions_;
  std::mutex subscribers_mutex_;
};
```

#### 3. **S7NodeResponder**
Location: `s7main/include/s7node_responder.hpp` + `s7main/src/s7node_responder.cpp`

**Responsibilities:**
- Implement RSocket `RSocketResponder` interface
- Decode JSON-RPC from Payload
- Route to JsonRpcDispatcher
- Encode JSON-RPC response back to Payload
- Handle streaming for events.subscribe

**API:**
```cpp
class S7NodeResponder : public rsocket::RSocketResponder {
public:
  explicit S7NodeResponder(std::shared_ptr<NodeRuntimeService> service);
  
  // Request/Response: JSON-RPC methods
  std::shared_ptr<yarpl::single::Single<rsocket::Payload>> 
  handleRequestResponse(rsocket::Payload request,
                        rsocket::StreamId) override;
  
  // Stream: events.subscribe returns continuous event stream
  std::shared_ptr<yarpl::flowable::Flowable<rsocket::Payload>>
  handleRequestStream(rsocket::Payload request,
                      rsocket::StreamId) override;
                      
private:
  std::shared_ptr<NodeRuntimeService> service_;
  std::shared_ptr<JsonRpcDispatcher> dispatcher_;
};
```

#### 4. **JsonRpcDispatcher**
Location: `s7main/include/s7json_rpc.hpp` + `s7main/src/s7json_rpc.cpp`

**Responsibilities:**
- Parse JSON-RPC 2.0 envelope
- Route by method name
- Call appropriate NodeRuntimeService method
- Serialize response/error

**API:**
```cpp
class JsonRpcDispatcher {
public:
  explicit JsonRpcDispatcher(std::shared_ptr<NodeRuntimeService> service);
  
  // Dispatch a parsed JSON-RPC request
  nlohmann::json dispatch(const std::string& method,
                          const nlohmann::json& params,
                          const std::string& id);
  
private:
  std::shared_ptr<NodeRuntimeService> service_;
  
  // Handler methods
  nlohmann::json handle_runtime_hello(const nlohmann::json& params,
                                       const std::string& id);
  nlohmann::json handle_schema_load(const nlohmann::json& params,
                                     const std::string& id);
  nlohmann::json handle_schema_run(const nlohmann::json& params,
                                    const std::string& id);
  // ... etc for all v0 methods
};
```

#### 5. **RSocket Server Startup**
Location: `s7main/src/main.cpp` (modifications)

**Changes:**
- Add `--server [port]` CLI flag (e.g., `--server 5577`)
- Start RSocket TCP server in background thread
- Register all libraries before server startup
- Graceful shutdown on SIGTERM

**Logic:**
```cpp
if (options.run_server) {
  auto node_service = std::make_shared<NodeRuntimeService>();
  
  TcpConnectionAcceptor::Options opts;
  opts.address = folly::SocketAddress("::", options.server_port);
  opts.threads = 4;
  
  auto server = RSocket::createServer(
      std::make_unique<TcpConnectionAcceptor>(opts));
  
  auto serverThread = std::thread([=] {
    server->startAndPark([node_service](const SetupParameters&) {
      return std::make_shared<S7NodeResponder>(node_service);
    });
  });
  
  std::cout << "S7 Control Server listening on port " << options.server_port << "\n";
  
  // Wait for SIGTERM or stdin
  std::getchar();
  
  server->unpark();
  serverThread.join();
}
```

---

## Design Decisions

### Port Selection
- **Default port:** 5577 (unassigned in IANA registry, easy to remember: "5-5-7-7")
- Non-standard (avoids conflicts with HTTP, SSH, etc.)
- Can be configured via CLI flag or config file

### Single-Active-Run Policy
- **Enforced in NodeRuntimeService**: returns error code `-32012` if run already active
- GUI can display "run in progress" indicator
- Future V2: support parallel runs per schema

### Schema Handle Lifetime
- **Survives disconnect**: per v0 spec decision
- Handles kept in memory until explicitly unloaded
- Useful for long-lived CLI sessions or GUI that disconnects periodically

### Event Subscription Model
- Client sends `events.subscribe` via requestResponse → gets subscription ID
- Server opens requestStream for continuous event delivery
- Events pushed as `{ "event": "...", "ts_unix_ms": ..., "payload": {...} }`
- Client can `cancel()` stream to unsubscribe

### JSON over RSocket (not binary RSocket)
- **Simplicity**: no separate binary serialization layer
- **Debugging**: human-readable wire format
- **Compatibility**: JSON-RPC is industry standard
- **Future data plane**: RSocket-based FileSink/FileSource will use binary for samples

---

## Threading Model

### Main Thread
- Accepts CLI arguments
- Initializes element registry
- Launches server thread
- Blocks on input (until user hits enter or SIGTERM)

### Server Thread
- Owns RSocket::RSocketServer
- Accepts incoming TCP connections
- For each connection: creates new S7NodeResponder instance
- Runs folly::EventBase for managing async I/O

### Per-Schema Thread (Future, V1)
- When schema is running: execute tick() loop in background
- Allows pause/resume via signal/flag
- Not V0 scope — V0 blocks on schema.run until complete

---

## Error Handling

### Protocol-Level Errors
JSON-RPC 2.0 error codes (from v0 spec):
- `-32000` generic runtime error
- `-32010` schema validation
- `-32011` schema handle not found
- `-32012` run already active
- etc. (see S7_CONTROL_PROTOCOL_V0_DRAFT.md)

### Network-Level Errors
- RSocket connection drop: Flowable<> unsubscribes client automatically
- Server shutdown: all active connections close gracefully

---

## Lifecycle Summary

### Server Startup
1. `s7main --server 5577`
2. Register element libraries
3. Create NodeRuntimeService
4. Create RSocketServer with TcpConnectionAcceptor
5. Start server thread → listens on port 5577
6. Block on main thread until SIGTERM or user input

### Client Connection (e.g., CLI)
1. Connect to `localhost:5577`
2. Send `events.subscribe` via requestStream
3. Start sending RPC requests (requestResponse)
4. Receive events continuously on subscription stream

### Schema Execution
1. Client sends `schema.load` (inline JSON or filesystem)
2. Server parses, validates, creates Schema object
3. Returns schema_handle
4. Client sends `schema.run` with run parameters
5. Server runs schema synchronously (blocks on tick loop)
6. Publishes events: run.started, run.completed
7. Client receives events via subscription stream

### Graceful Shutdown
1. SIGTERM received → main thread detected (e.g., getchar() unblocked by other signal handler)
2. Call `server->unpark()`
3. Server stops accepting new connections
4. Active connections drain (RSocket protocol handles graceful close)
5. All threads join
6. Exit

---

## Phase Schedule

### Phase 1 (This Session)
- [x] Analyze rsocket-cpp (DONE)
- [ ] Create NodeRuntimeService (interface + stub implementation)
- [ ] Create EventBus (interface + stub implementation)
- [ ] Create S7NodeResponder (RSocket handler)
- [ ] Create JsonRpcDispatcher (method router)
- [ ] Modify s7main/main.cpp for server mode
- [ ] Build and verify compilation

### Phase 2 (Integration)
- Implement full NodeRuntimeService with schema state management
- Implement EventBus with real subscriptions
- Full JSON-RPC dispatch for all v0 methods
- Test with example CLI client

### Phase 3 (Production Hardening)
- TLS/X.509 support
- Metrics and observability
- Connection lifecycle management
- Comprehensive test suite

---

## Dependency Changes

### New External Dependency
- **Folly** (Facebook C++ utility library) — required by RSocket

### Potential Installation (Linux)
```bash
# Ubuntu/Debian
sudo apt-get install libfolly-dev

# Or build from source
# https://github.com/facebook/folly
```

### CMakeLists.txt Updates
- Find and link RSocket library
- Find and link Folly library
- Add new source files

---

## References

- S7 Control Protocol v0: `docs/S7_CONTROL_PROTOCOL_V0_DRAFT.md`
- RSocket Analysis: `docs/S7_RSOCKET_ANALYSIS.md`
- RSocket C++ Examples: `external/rsocket/rsocket/examples/`
- RSocket Protocol Spec: https://rsocket.io/docs/protocol


