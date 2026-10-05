#ifndef S7NET_INCLUDE_S7_DEPLOYMENT_CLIENT_HPP
#define S7NET_INCLUDE_S7_DEPLOYMENT_CLIENT_HPP

// Deploying a diagram to the nodes its elements are placed on, as Studio
// does (S7_Control_Protocol_MVP.md, PREPARE / COMMIT / ABORT): assign the
// streams, prepare every node with the whole diagram, and commit only if
// every node is ready (otherwise abort the prepared ones).

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <boost/json.hpp>

#include "s7/diagram_deployment.hpp"
#include "s7/element_registry.hpp"
#include "s7/transport.hpp"

namespace s7::deployment {

// Output port types from type inference over the diagram's schema, built
// with the registered element types. Throws std::invalid_argument if the
// schema cannot be built or its types do not resolve.
[[nodiscard]] auto infer_output_types(const boost::json::object& diagram,
                                      const ElementRegistry& registry = ElementRegistry::instance()) -> OutputTypeLookup;

// Sends one control request to a node and returns its response; throws if
// the node cannot be reached.
using ControlRequester = std::function<boost::json::object(const RuntimeNode& node, const boost::json::object& request)>;

// One mTLS (or plain) control connection per node endpoint, opened on first
// use. Over TLS the server certificate's node identity must equal the node's
// node_uuid (when given).
class BeastControlRequester {
public:
    explicit BeastControlRequester(transport::TlsConfig tls) : tls_(std::move(tls)) {}

    auto operator()(const RuntimeNode& node, const boost::json::object& request) -> boost::json::object;

private:
    transport::TlsConfig tls_;
    std::map<std::string, std::shared_ptr<transport::BeastRuntimeNetEndpoint>> clients_;
};

struct DeployOptions {
    std::string deployment_id;    // empty: generated
    std::string project_revision; // empty: derived from the diagram's digest
};

struct NodeOutcome {
    std::string node_uuid;
    std::string endpoint;
    // "committed", "ready" (prepared, then aborted), "not_ready", "error"
    std::string status;
    std::string message;
};

struct DeployResult {
    bool committed{false};
    std::string deployment_id;
    std::uint64_t revision{};
    std::string configuration_digest;
    boost::json::object diagram; // with the streams as deployed
    std::vector<NodeOutcome> nodes;
    std::vector<std::string> errors;
};

[[nodiscard]] auto deploy_diagram(boost::json::object diagram,
                                  const OutputTypeLookup& output_types,
                                  const ControlRequester& request,
                                  const DeployOptions& options = {}) -> DeployResult;

} // namespace s7::deployment

#endif // S7NET_INCLUDE_S7_DEPLOYMENT_CLIENT_HPP
