// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#ifndef S7NET_INCLUDE_S7_DIAGRAM_DEPLOYMENT_HPP
#define S7NET_INCLUDE_S7_DIAGRAM_DEPLOYMENT_HPP

// Deploying one diagram across nodes (S7_Network_Wire_Protocol_v1.md,
// section 12.1, items 1 and 6).
//
// Every element of the diagram's schema carries a `node` attribute (a
// node_uuid listed in the diagram's `runtime_nodes`). A connection between
// elements on different nodes is carried by a stream; the diagram's
// `streams` array describes each one. Studio assigns the streams before
// deploying (assign_streams); every node receives the whole diagram and runs
// its own part of it (extract_node_schema), with a NetSink / NetSource in
// place of each stream end.
//
//   "runtime_nodes": [ { "node_uuid": "...", "name": "node-a", "endpoint": "ws://host:9002" } ],
//   "streams": [ {
//       "stream_id": "<uuid>",
//       "producer": { "node_uuid": "...", "element": "osc", "port": 0 },
//       "consumers": [ { "node_uuid": "...", "element": "gain", "port": 0 } ],
//       "data_type": "F64",
//       "max_elements_per_packet": 1024, "max_batch_latency_ms": 10,
//       "send_log": { "max_packets": 4096, "max_block_ms": 2000 },
//       "max_wait_ms": 2000,
//       "sample_rate": { "numerator": 1000, "denominator": 1 },
//       "timestamps": [ { "role": "MODEL_TIME", "clock_domain": "model" } ],
//       "max_latency_ms": 50                       (optional)
//   } ]
//
// Timing is derived from the diagram: with a model-time step (`dt`, seconds)
// a stream is uniformly sampled at 1 / dt in MODEL_TIME (clock domain
// "model"), one element per step; without one it carries the producer's
// PROCESSING_TIME (clock domain "utc": the nodes' NTP-synchronized UTC
// clocks) on every packet.

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/json.hpp>

#include "s7/rate.hpp"
#include "s7/signal_types.hpp"
#include "s7/stream_protocol.hpp"
#include "s7/transport.hpp"

namespace s7::deployment {

struct RuntimeNode {
    std::string node_uuid;
    std::string name;
    std::string endpoint; // control URL; also data URL in shared-listener mode
    std::string data_endpoint{}; // optional independent data listener
    std::string data_security{}; // with data_endpoint: "mtls" or "trusted_plaintext"
};

struct StreamEndpoint {
    std::string node_uuid;
    std::string element;
    std::size_t port{};

    bool operator==(const StreamEndpoint&) const = default;
};

struct TimestampDeclaration {
    wire::TimestampRole role{wire::TimestampRole::ModelTime};
    std::string clock_domain;

    bool operator==(const TimestampDeclaration&) const = default;
};

struct StreamDescriptor {
    wire::StreamId stream_id{};
    StreamEndpoint producer;
    std::vector<StreamEndpoint> consumers;
    SignalType data_type{SignalType::F64};
    std::uint32_t max_elements_per_packet{1024};
    std::uint32_t max_batch_latency_ms{10};
    std::uint32_t send_log_max_packets{4096};
    std::uint32_t max_block_ms{2000};
    std::uint32_t max_wait_ms{2000};
    std::optional<stream::SampleRate> sample_rate;
    std::vector<TimestampDeclaration> timestamps; // at least one, ascending role
    std::optional<std::uint32_t> max_latency_ms;  // consumers count later packets as LATE
};

// The diagram's model-time step (`dt`, seconds, in ns); nothing if it has
// none. Throws std::invalid_argument if `dt` is not a positive number of at
// least 1 ns.
[[nodiscard]] auto model_time_step_ns(const boost::json::object& diagram) -> std::optional<std::int64_t>;
// The same `dt` as an exact rational rate (Rate::from_seconds), the model-time
// base of every node. The ns step above is rounded and only paces real time.
[[nodiscard]] auto model_time_rate(const boost::json::object& diagram) -> std::optional<Rate>;

// The timing every stream of this diagram gets.
struct StreamTiming {
    std::optional<stream::SampleRate> sample_rate;
    std::vector<TimestampDeclaration> timestamps;

    bool operator==(const StreamTiming&) const = default;
};
[[nodiscard]] auto stream_timing(const boost::json::object& diagram) -> StreamTiming;

[[nodiscard]] auto parse_runtime_nodes(const boost::json::object& diagram) -> std::vector<RuntimeNode>;
// Throws std::invalid_argument on a malformed descriptor.
[[nodiscard]] auto parse_streams(const boost::json::object& diagram) -> std::vector<StreamDescriptor>;
[[nodiscard]] auto to_json(const StreamDescriptor& stream) -> boost::json::object;

// The diagram's runtime schema (its `schema` object; a bare schema is
// accepted too).
[[nodiscard]] auto diagram_schema(const boost::json::object& diagram) -> const boost::json::object&;

// The nodes elements are placed on, in order of first appearance.
[[nodiscard]] auto placed_nodes(const boost::json::object& diagram) -> std::vector<std::string>;

// Output port types of an element of the diagram's schema (from type
// inference over the whole schema); nothing if unknown.
using OutputTypeLookup = std::function<std::optional<SignalType>(const std::string& element, std::size_t port)>;

// Studio, before deploying: checks placement (every element on a listed
// node) and rewrites `streams` to cover exactly the connections between
// nodes, one stream per producing output port. A stream keeps its id and
// settings while its producer stays on the same node with the same type,
// and timing (stream_timing), so redeployments continue it; otherwise it gets
// a new random id.
// Returns the problems found (the diagram is unchanged if any).
[[nodiscard]] auto assign_streams(boost::json::object& diagram, const OutputTypeLookup& output_type) -> std::vector<std::string>;

// A node: its part of the diagram as a runtime schema. Elements placed on
// the node and the connections between them are kept; each stream the node
// produces gets a NetSink fed from the producing port, each stream it
// consumes a NetSource feeding the consuming ports, both pinned to the
// stream's type. Throws std::invalid_argument if a connection crosses nodes
// without a stream, or the diagram is otherwise inconsistent.
[[nodiscard]] auto extract_node_schema(const boost::json::object& diagram, std::string_view node_uuid) -> boost::json::object;

// The nodes producing the streams this node consumes (endpoint and expected
// identity): the data peers it connects to.
[[nodiscard]] auto producer_endpoints(const boost::json::object& diagram, std::string_view node_uuid)
    -> std::vector<transport::DataPeer>;

} // namespace s7::deployment

#endif // S7NET_INCLUDE_S7_DIAGRAM_DEPLOYMENT_HPP
