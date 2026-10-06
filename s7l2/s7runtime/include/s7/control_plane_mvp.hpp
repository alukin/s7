#ifndef S7RUNTIME_INCLUDE_S7_CONTROL_PLANE_MVP_HPP
#define S7RUNTIME_INCLUDE_S7_CONTROL_PLANE_MVP_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/json.hpp>

#include "s7/transport.hpp"

namespace s7 {

struct ControlLibraryInfo {
    std::string library_id;
    std::string kind{"builtin"};
    std::string version{"0.0.0"};
    std::string target_triple;
    std::string origin{"builtin"};
    std::string status{"ready"};
    std::string status_reason;
    std::vector<std::string> elements;
};

struct ControlNodeDescriptor {
    std::string node_uuid;
    std::string node_name;
    std::string display_name;
    std::string runtime_version{"0.1.0"};
    std::uint64_t protocol_version{1};
    std::string instance_id;
    std::string started_at;
    std::string identity_certificate_file;
    std::string identity_certificate_common_name;
    std::string identity_certificate_fingerprint_sha256;
    std::string listen_url;
    std::string os_family;
    std::string os_version;
    std::string architecture;
    std::string target_triple;
    std::vector<ControlLibraryInfo> libraries;
    std::vector<std::string> element_types;
    std::vector<std::string> hardware_capabilities;
};

// The single Studio allowed to change this node's configuration: its client
// certificate must carry the control-node key purpose (Extended Key Usage)
// and match the pinned fingerprint. Unconfigured means no peer may change configuration.
struct ControlAuthorization {
    std::string studio_key_purpose_oid;
    std::string studio_certificate_sha256;

    [[nodiscard]] auto configured() const -> bool
    {
        return !studio_key_purpose_oid.empty() && !studio_certificate_sha256.empty();
    }
};

// A node's assigned graph, validated and compiled by PREPARE_DEPLOYMENT but not
// yet running.
class StagedConfiguration {
public:
    virtual ~StagedConfiguration() = default;
};

// This node's part of a deployed diagram.
struct NodePart {
    boost::json::object schema;                  // deployment::extract_node_schema
    std::vector<transport::DataPeer> data_peers; // the nodes producing the streams it consumes
    std::optional<std::int64_t> model_time_step_ns; // the diagram's `dt`: one step of model time
};

// Runs deployed configurations on behalf of the control plane.
class DeploymentHost {
public:
    virtual ~DeploymentHost() = default;

    // Validates and compiles this node's part of the deployed diagram without
    // activating it. Throws std::exception describing why the configuration
    // is invalid.
    [[nodiscard]] virtual auto stage(const NodePart& part) -> std::unique_ptr<StagedConfiguration> = 0;

    // Makes the staged configuration the running graph. On failure throws and
    // leaves the previously active graph running.
    virtual void activate(StagedConfiguration& staged) = 0;

    // Whether the active graph is being stepped: "stepping" or "not_stepping".
    [[nodiscard]] virtual auto execution_state() const -> std::string = 0;
};

struct ControlPlaneOptions {
    std::filesystem::path storage_dir; // peers.json; empty: not persisted
    ControlAuthorization authorization;
    std::shared_ptr<DeploymentHost> deployment_host; // null: deployments are refused
    std::function<void(const std::string&)> log;     // authorization decisions
    std::chrono::seconds prepare_ttl{std::chrono::minutes(5)};
    transport::RuntimeNetEndpointPtr network; // streams for GET_STREAM_STATUS / RECOVER_STREAM
    std::function<void()> on_stream_recovered; // after a successful RECOVER_STREAM
};

class ControlPlaneMvp {
public:
    explicit ControlPlaneMvp(ControlNodeDescriptor descriptor, ControlPlaneOptions options = {});

    void add_seed_peer(const std::string& endpoint);
    [[nodiscard]] auto handle_request_json(const transport::ControlPeer& peer, const std::string& request_json)
        -> std::string;

private:
    struct PeerEntry {
        std::string endpoint;
        std::optional<std::string> node_uuid;
        std::optional<std::string> node_name;
        std::optional<std::string> identity_certificate_common_name;
        std::optional<std::string> identity_certificate_fingerprint_sha256;
        std::string trust_state{"unverified"};
        std::string reachability{"offline"};
        std::optional<std::string> last_seen_at;
        std::optional<std::string> last_failure_at;
        std::optional<std::string> os_family;
        std::optional<std::string> architecture;
    };

    struct PreparedDeployment {
        std::string deployment_id;
        std::uint64_t revision{};
        std::uint64_t base_revision{};
        std::string project_revision;
        std::string configuration_digest;
        std::string prepare_token;
        std::string prepared_at;
        std::string expires_at;
        std::chrono::steady_clock::time_point expires;
        std::shared_ptr<StagedConfiguration> staged;
        boost::json::object diagram;     // as deployed
        boost::json::object node_schema; // this node's part of it
    };

    struct ActiveDeployment {
        std::string deployment_id;
        std::uint64_t revision{};
        std::string project_revision;
        std::string configuration_digest;
        std::string committed_at;
        std::string prepare_token; // makes a repeated commit idempotent
        boost::json::object diagram;
        boost::json::object node_schema;
    };

    struct DeploymentOutcome {
        std::string deployment_id;
        std::uint64_t revision{};
        std::string outcome; // committed, aborted, expired, failed, superseded
    };

    [[nodiscard]] auto handle_request_object(const transport::ControlPeer& peer, const boost::json::object& request)
        -> boost::json::object;
    [[nodiscard]] auto authorize_mutation(const transport::ControlPeer& peer,
                                          const std::string& request_id,
                                          const std::string& request_type) const -> std::optional<boost::json::object>;
    void expire_prepared_locked(std::chrono::steady_clock::time_point now);
    void record_outcome_locked(const PreparedDeployment& prepared, const std::string& outcome);
    [[nodiscard]] auto state_name_locked() const -> std::string;
    [[nodiscard]] auto handle_hello(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_get_node_info(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_get_peers(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_peer_announce(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_list_deployments(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_prepare_deployment(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_commit_deployment(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_abort_deployment(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_get_stream_status(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_get_network_configuration(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_recover_stream(const boost::json::object& request) -> boost::json::object;

    void load_peers_from_disk();
    void persist_peers_to_disk() const;

    ControlNodeDescriptor descriptor_;
    ControlPlaneOptions options_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PeerEntry> peers_;
    std::unordered_map<std::string, PreparedDeployment> prepared_; // key: deployment_id#revision
    std::optional<ActiveDeployment> active_;
    std::vector<DeploymentOutcome> history_; // bounded, newest last
};

} // namespace s7

#endif // S7RUNTIME_INCLUDE_S7_CONTROL_PLANE_MVP_HPP

