# Distributed Element Addressing and Network Topology

## Overview

S7 supports distributed computation across multiple computational nodes. Elements and nodes require unique, network-wide addressing to support:
- Cross-node element connections
- Distributed execution coordination
- Network-based element introspection
- Remote element parameter configuration

## UUID System for Elements

### Element UUID

Each element instance in the entire distributed system has a globally unique identifier (UUID).

```cpp
struct ElementAddress {
    std::string node_id;        // Computational node identifier
    std::string element_uuid;   // Unique within node
    std::string element_id;     // Human-readable local name
};

// Full network address
std::string full_address() const {
    return node_id + "/" + element_uuid;
}
```

### UUID Generation

UUIDs are generated using UUID v4 (random) or UUID v5 (deterministic from node_id + element_id):

```cpp
// Deterministic: same input = same UUID
UUID element_uuid = uuid_v5(
    namespace_uuid,
    node_id + ":" + element_id
);

// Random: unique per instantiation
UUID element_uuid = uuid_v4();
```

**Recommendation**: Use deterministic UUIDs for reproducibility, random for dynamic element creation.

## Computational Node Addressing

### Node UUID

Each s7runtime instance (computational node) has a unique node identifier:

```cpp
struct ComputationalNodeAddress {
    std::string node_id;           // e.g., "compute-01", "pi-raspberry-1"
    std::string hostname;          // DNS name or IP
    uint16_t port;                 // Control port (WebSocket, etc.)
    std::string node_uuid;         // UUID v5(namespace, node_id)
};
```

### Full Network Path

Elements are addressed as:
```
node_uuid / element_uuid

Example:
550e8400-e29b-41d4-a716-446655440000 / 6ba7b810-9dad-11d1-80b4-00c04fd430c8
```

Or human-readable:
```
compute-01 / oscillator_1
```

## Network Topology

### Schema Distribution

Schemas can span multiple nodes:

```json
{
  "name": "distributed_filter",
  "elements": [
    {
      "id": "oscillator",
      "type": "AnalogOscillator",
      "node": "compute-01"
    },
    {
      "id": "filter",
      "type": "IirFilter",
      "node": "compute-02"
    }
  ],
  "connections": [
    {
      "from": "compute-01/oscillator:out",
      "to": "compute-02/filter:in"
    }
  ]
}
```

### Network Boundaries

Elements that communicate across nodes are marked as network boundaries:

```cpp
struct NetworkBoundary {
    std::string source_node;
    std::string source_element;
    uint16_t source_port;
    
    std::string dest_node;
    std::string dest_element;
    uint16_t dest_port;
    
    std::string protocol;          // "websocket", "tcp", "grpc"
    std::string topic;             // Publish/subscribe topic (optional)
};
```

## Element UUID Management

### In ExecutableGraph

```cpp
struct ExecutableElement {
    std::string element_id;        // Local: "gain_1"
    std::string element_uuid;      // Global: UUID
    std::string node_id;           // Host node
    std::unique_ptr<Element> impl;
    ElementMetadata metadata;
};

struct ExecutableGraph {
    std::string schema_name;
    std::string node_id;
    std::string node_uuid;
    
    // All elements on this node
    std::unordered_map<std::string, ExecutableElement> elements;
    
    // UUID index for lookup
    std::unordered_map<std::string, std::string> uuid_to_id;
    
    std::vector<std::string> causal_order;
    std::vector<std::string> memory_order;
};
```

### UUID Lookup Performance

```cpp
class ExecutableGraph {
public:
    // Fast: O(1) lookup
    const Element* find_element_by_uuid(const std::string& uuid) const {
        auto it = uuid_to_id.find(uuid);
        if (it == uuid_to_id.end()) return nullptr;
        return elements.at(it->second).impl.get();
    }
    
    // Fast: O(1) lookup
    std::string get_uuid(const std::string& element_id) const {
        return elements.at(element_id).element_uuid;
    }
};
```

## Network Element Proxy

### Cross-Node Connections

Elements on different nodes don't connect directly. Instead, network proxies bridge them:

```cpp
class RemoteElementProxy : public Element {
public:
    RemoteElementProxy(const std::string& remote_node_id,
                      const std::string& remote_element_uuid)
        : Element("RemoteElementProxy"),
          remote_node_id_(remote_node_id),
          remote_element_uuid_(remote_element_uuid)
    {
        // Register input for receiving remote values
        register_output(0, "out", signal_type_);
    }
    
    void step() override {
        // Fetch latest value from remote node
        // via network (WebSocket, gRPC, etc.)
        output_value_ = fetch_remote_output();
    }

private:
    std::string remote_node_id_;
    std::string remote_element_uuid_;
    SignalType signal_type_;
    double output_value_;
};
```

### Network Send Element

```cpp
class RemoteElementSink : public Element {
public:
    RemoteElementSink(const std::string& remote_node_id,
                     const std::string& remote_element_id)
        : Element("RemoteElementSink"),
          remote_node_id_(remote_node_id),
          remote_element_id_(remote_element_id)
    {
        register_input(0, "in", signal_type_);
    }
    
    void step() override {
        // Send local value to remote node
        send_to_remote(read_input<double>(0));
    }

private:
    std::string remote_node_id_;
    std::string remote_element_id_;
};
```

## Runtime Network Interface

### ComputationalNode API

```cpp
class ComputationalNode {
public:
    // Addressing
    [[nodiscard]] const std::string& node_id() const;
    [[nodiscard]] const std::string& node_uuid() const;
    [[nodiscard]] uint16_t control_port() const;
    
    // Element management
    void execute_step();
    
    // Element discovery
    [[nodiscard]] const Element* find_element_by_uuid(
        const std::string& uuid) const;
    [[nodiscard]] std::string get_element_uuid(
        const std::string& local_id) const;
    
    // Remote access
    [[nodiscard]] double read_remote_output(
        const std::string& element_uuid, 
        size_t port) const;
    void write_remote_input(
        const std::string& element_uuid,
        size_t port,
        double value);

private:
    std::string node_id_;
    std::string node_uuid_;
    ExecutableGraph graph_;
    std::unordered_map<std::string, ComputationalNode*> peer_nodes_;
};
```

### Network Bootstrap

```cpp
class NetworkTopology {
public:
    void add_node(const ComputationalNodeAddress& addr);
    void add_cross_node_connection(
        const std::string& from_node_uuid,
        const std::string& from_element_uuid,
        uint16_t from_port,
        const std::string& to_node_uuid,
        const std::string& to_element_uuid,
        uint16_t to_port
    );
    
    [[nodiscard]] ComputationalNode* find_node_by_uuid(
        const std::string& uuid) const;
    [[nodiscard]] const std::vector<ComputationalNodeAddress>& nodes() const;
    
private:
    std::unordered_map<std::string, ComputationalNode> nodes_;
    std::vector<NetworkBoundary> boundaries_;
};
```

## Schema JSON Format with UUIDs

```json
{
  "name": "distributed_system",
  "node_id": "compute-01",
  "node_uuid": "550e8400-e29b-41d4-a716-446655440000",
  
  "elements": [
    {
      "id": "source",
      "type": "AnalogOscillator",
      "uuid": "6ba7b810-9dad-11d1-80b4-00c04fd430c8",
      "params": {"frequency_hz": 1000},
      "local": true
    },
    {
      "id": "remote_filter",
      "type": "RemoteElementProxy",
      "remote_node_uuid": "550e8400-e29b-41d4-a716-446655440001",
      "remote_element_uuid": "9d84b7b0-3c21-5e2b-c3a9-7f21e8d92c4d",
      "local": false
    }
  ],
  
  "connections": [
    {
      "from_element": "source",
      "from_port": 0,
      "to_element": "remote_filter",
      "to_port": 0,
      "type": "network"
    }
  ],
  
  "network_topology": [
    {
      "node_id": "compute-01",
      "node_uuid": "550e8400-e29b-41d4-a716-446655440000",
      "hostname": "localhost",
      "port": 8001
    },
    {
      "node_id": "compute-02",
      "node_uuid": "550e8400-e29b-41d4-a716-446655440001",
      "hostname": "192.168.1.10",
      "port": 8002
    }
  ]
}
```

## Element Discovery via UUID

### Catalog Query

Each computational node can expose its element catalog:

```cpp
struct ElementCatalogEntry {
    std::string element_id;
    std::string element_uuid;
    ElementMetadata metadata;
};

class ElementCatalogService {
public:
    [[nodiscard]] std::vector<ElementCatalogEntry> all_elements() const;
    [[nodiscard]] const ElementCatalogEntry* find_by_uuid(
        const std::string& uuid) const;
    [[nodiscard]] const ElementCatalogEntry* find_by_id(
        const std::string& id) const;
    
    boost::json::value to_json() const;  // For network export
};
```

### Remote Catalog Discovery

```cpp
// On compute-01, query what's available on compute-02
auto catalog = network_topology.get_node("compute-02")->catalog();

for (const auto& entry : catalog.all_elements()) {
    std::cout << entry.element_uuid << ": " 
              << entry.metadata.type_name << "\n";
}
```

## State Serialization with UUIDs

### Runtime State Snapshot

```cpp
struct RuntimeStateSnapshot {
    std::string node_uuid;
    std::string timestamp;
    
    struct ElementState {
        std::string element_uuid;
        boost::json::object internal_state;
        std::vector<double> output_values;
    };
    
    std::vector<ElementState> element_states;
};

// Serialize for network transmission
boost::json::value serialize_snapshot(const RuntimeStateSnapshot& snap);
```

## Implementation Notes

### UUID Library

Use `boost::uuid` for generation and serialization:

```cpp
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

// Generate v5 UUID (deterministic)
boost::uuids::uuid element_uuid = 
    boost::uuids::name_generator_v5(
        boost::uuids::ns::dns())(
        node_id + ":" + element_id
    );

// Generate v4 UUID (random)
boost::uuids::uuid random_uuid = 
    boost::uuids::random_generator()();

// Convert to string
std::string uuid_str = boost::uuids::to_string(element_uuid);
```

### Network Communication

UUID addresses enable uniform element access:
- Local: Direct memory access, O(1) lookup
- Remote: Network proxy, O(network latency) lookup
- Transparent to element code

### Backward Compatibility

Existing local schemas work without UUIDs. UUID generation is automatic:
```cpp
// If no UUID provided in schema
std::string uuid = uuid_v5(node_uuid, element_id);
```

## Performance Characteristics

| Operation | Time | Notes |
|-----------|------|-------|
| Local element lookup by UUID | O(1) | Hash table |
| Generate UUID v5 | ~1 μs | Hash-based |
| Generate UUID v4 | ~5 μs | Random |
| Network element access | ~1 ms | Network latency |
| Catalog query (100 elements) | ~100 μs | In-memory |

## Summary

UUID addressing enables:
- ✅ Unique identification of elements across distributed system
- ✅ Network-wide element discovery
- ✅ Transparent local/remote element access
- ✅ Distributed state snapshots
- ✅ Runtime element catalog queries
- ✅ Backward compatibility with local schemas

