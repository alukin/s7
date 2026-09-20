#ifndef S7RUNTIME_INCLUDE_S7_CONTROL_PLANE_MVP_HPP
#define S7RUNTIME_INCLUDE_S7_CONTROL_PLANE_MVP_HPP

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/json.hpp>

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

class ControlPlaneMvp {
public:
    explicit ControlPlaneMvp(ControlNodeDescriptor descriptor, std::filesystem::path storage_dir = {});

    void add_seed_peer(const std::string& endpoint);
    [[nodiscard]] auto handle_request_json(const std::string& request_json) -> std::string;

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
        std::int64_t revision{};
        std::string prepare_token;
        std::string prepared_at;
        std::string expires_at;
    };

    [[nodiscard]] auto handle_request_object(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_hello(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_get_node_info(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_get_peers(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_peer_announce(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_list_deployments(const boost::json::object& request) const -> boost::json::object;
    [[nodiscard]] auto handle_prepare_deployment(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_commit_deployment(const boost::json::object& request) -> boost::json::object;
    [[nodiscard]] auto handle_abort_deployment(const boost::json::object& request) -> boost::json::object;

    void load_peers_from_disk();
    void persist_peers_to_disk() const;

    ControlNodeDescriptor descriptor_;
    std::filesystem::path storage_dir_;
    mutable std::mutex mutex_;
    std::unordered_map<std::string, PeerEntry> peers_;
    std::unordered_map<std::string, PreparedDeployment> prepared_;
    std::string active_deployment_id_;
};

} // namespace s7

#endif // S7RUNTIME_INCLUDE_S7_CONTROL_PLANE_MVP_HPP

