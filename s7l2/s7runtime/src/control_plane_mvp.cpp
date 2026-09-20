#include "s7/control_plane_mvp.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

namespace s7 {
namespace {

[[nodiscard]] auto now_utc_iso8601() -> std::string
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now_time);
#else
    gmtime_r(&now_time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

[[nodiscard]] auto plus_minutes_iso8601(const int minutes) -> std::string
{
    const auto then = std::chrono::system_clock::now() + std::chrono::minutes(minutes);
    const std::time_t then_time = std::chrono::system_clock::to_time_t(then);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &then_time);
#else
    gmtime_r(&then_time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

[[nodiscard]] auto as_string(const boost::json::object& object, const std::string& key) -> std::optional<std::string>
{
    const auto it = object.find(key);
    if (it == object.end() || !it->value().is_string()) {
        return std::nullopt;
    }
    return std::string(it->value().as_string().c_str());
}

[[nodiscard]] auto compare_versions(const std::string& left, const std::string& right) -> int
{
    auto parse = [](const std::string& value) {
        std::vector<int> parts;
        std::stringstream stream(value);
        std::string token;
        while (std::getline(stream, token, '.')) {
            try {
                parts.push_back(std::stoi(token));
            } catch (...) {
                parts.push_back(0);
            }
        }
        return parts;
    };

    auto left_parts = parse(left);
    auto right_parts = parse(right);
    const auto count = std::max(left_parts.size(), right_parts.size());
    left_parts.resize(count, 0);
    right_parts.resize(count, 0);

    for (std::size_t i = 0; i < count; ++i) {
        if (left_parts[i] < right_parts[i]) {
            return -1;
        }
        if (left_parts[i] > right_parts[i]) {
            return 1;
        }
    }
    return 0;
}

[[nodiscard]] auto make_prepare_token() -> std::string
{
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::uint64_t> dist;
    std::ostringstream out;
    out << "ptok_" << std::hex << dist(gen);
    return out.str();
}

[[nodiscard]] auto has_string(const std::vector<std::string>& values, const std::string& needle) -> bool
{
    return std::find(values.begin(), values.end(), needle) != values.end();
}

struct CertificateInfo {
    std::optional<std::string> common_name;
    std::optional<std::string> fingerprint_sha256;
};

[[nodiscard]] auto certificate_object(const std::optional<std::string>& common_name,
                                      const std::optional<std::string>& fingerprint_sha256) -> boost::json::object
{
    boost::json::object certificate;
    if (common_name && !common_name->empty()) {
        certificate["common_name"] = *common_name;
    }
    if (fingerprint_sha256 && !fingerprint_sha256->empty()) {
        certificate["fingerprint_sha256"] = *fingerprint_sha256;
    }
    return certificate;
}

[[nodiscard]] auto read_certificate_info(const boost::json::object& object) -> CertificateInfo
{
    CertificateInfo info;

    if (const auto certificate_it = object.find("certificate"); certificate_it != object.end() && certificate_it->value().is_object()) {
        const auto& certificate = certificate_it->value().as_object();
        info.common_name = as_string(certificate, "common_name");
        info.fingerprint_sha256 = as_string(certificate, "fingerprint_sha256");
    }

    if (!info.common_name) {
        info.common_name = as_string(object, "certificate_common_name");
    }
    if (!info.fingerprint_sha256) {
        info.fingerprint_sha256 = as_string(object, "certificate_fingerprint_sha256");
    }

    return info;
}

[[nodiscard]] auto make_error_response(const std::string& request_id,
                                       const std::string& code,
                                       const std::string& message) -> boost::json::object
{
    boost::json::object out;
    out["type"] = "ERROR";
    out["request_id"] = request_id;
    out["protocol_version"] = 1;
    out["code"] = code;
    out["message"] = message;
    out["details"] = boost::json::object{};
    return out;
}

} // namespace

ControlPlaneMvp::ControlPlaneMvp(ControlNodeDescriptor descriptor, std::filesystem::path storage_dir)
    : descriptor_(std::move(descriptor))
    , storage_dir_(std::move(storage_dir))
    , mutex_()
    , peers_()
    , prepared_()
    , active_deployment_id_()
{
    if (!storage_dir_.empty()) {
        std::filesystem::create_directories(storage_dir_);
        load_peers_from_disk();
    }
}

void ControlPlaneMvp::add_seed_peer(const std::string& endpoint)
{
    if (endpoint.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    auto& peer = peers_[endpoint];
    peer.endpoint = endpoint;
    if (peer.trust_state.empty()) {
        peer.trust_state = "unverified";
    }
    if (peer.reachability.empty()) {
        peer.reachability = "offline";
    }
    persist_peers_to_disk();
}

auto ControlPlaneMvp::handle_request_json(const std::string& request_json) -> std::string
{
    try {
        const auto parsed = boost::json::parse(request_json);
        if (!parsed.is_object()) {
            return boost::json::serialize(make_error_response({}, "INVALID_REQUEST", "Request must be a JSON object."));
        }
        return boost::json::serialize(handle_request_object(parsed.as_object()));
    } catch (const std::exception& ex) {
        return boost::json::serialize(make_error_response({}, "INVALID_REQUEST", ex.what()));
    }
}

auto ControlPlaneMvp::handle_request_object(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto request_type = as_string(request, "type").value_or("");
    const auto protocol_version_value = request.find("protocol_version");
    if (protocol_version_value == request.end() ||
        (!protocol_version_value->value().is_uint64() && !protocol_version_value->value().is_int64())) {
        return make_error_response(request_id, "INVALID_REQUEST", "Missing numeric protocol_version.");
    }

    const auto protocol_version = protocol_version_value->value().is_uint64()
        ? static_cast<std::int64_t>(protocol_version_value->value().as_uint64())
        : protocol_version_value->value().as_int64();
    if (protocol_version != 1) {
        return make_error_response(request_id, "UNSUPPORTED_PROTOCOL_VERSION", "Only protocol_version=1 is supported.");
    }

    if (request_type == "HELLO") {
        return handle_hello(request);
    }
    if (request_type == "GET_NODE_INFO") {
        return handle_get_node_info(request);
    }
    if (request_type == "GET_PEERS") {
        return handle_get_peers(request);
    }
    if (request_type == "PEER_ANNOUNCE") {
        return handle_peer_announce(request);
    }
    if (request_type == "LIST_DEPLOYMENTS") {
        return handle_list_deployments(request);
    }
    if (request_type == "PREPARE_DEPLOYMENT") {
        return handle_prepare_deployment(request);
    }
    if (request_type == "COMMIT_DEPLOYMENT") {
        return handle_commit_deployment(request);
    }
    if (request_type == "ABORT_DEPLOYMENT") {
        return handle_abort_deployment(request);
    }

    return make_error_response(request_id, "INVALID_REQUEST", "Unsupported control message type: '" + request_type + "'.");
}

auto ControlPlaneMvp::handle_hello(const boost::json::object& request) const -> boost::json::object
{
    std::string active_deployment_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_deployment_id = active_deployment_id_;
    }

    boost::json::object out;
    out["type"] = "HELLO_OK";
    out["request_id"] = as_string(request, "request_id").value_or("");
    out["protocol_version"] = descriptor_.protocol_version;
    out["node_uuid"] = descriptor_.node_uuid;
    out["node_name"] = descriptor_.node_name;
    out["runtime_version"] = descriptor_.runtime_version;
    out["status"] = active_deployment_id.empty() ? "idle" : "running";
    out["certificate"] = certificate_object(descriptor_.identity_certificate_common_name.empty()
                                                  ? std::optional<std::string>{}
                                                  : std::optional<std::string>{descriptor_.identity_certificate_common_name},
                                              descriptor_.identity_certificate_fingerprint_sha256.empty()
                                                  ? std::optional<std::string>{}
                                                  : std::optional<std::string>{descriptor_.identity_certificate_fingerprint_sha256});
    return out;
}

auto ControlPlaneMvp::handle_get_node_info(const boost::json::object& request) const -> boost::json::object
{
    std::string active_deployment_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_deployment_id = active_deployment_id_;
    }

    boost::json::object out;
    out["type"] = "NODE_INFO";
    out["request_id"] = as_string(request, "request_id").value_or("");
    out["protocol_version"] = descriptor_.protocol_version;

    boost::json::object node;
    node["node_uuid"] = descriptor_.node_uuid;
    node["node_name"] = descriptor_.node_name;
    node["display_name"] = descriptor_.display_name;
    node["runtime_version"] = descriptor_.runtime_version;
    node["protocol_version"] = descriptor_.protocol_version;
    node["instance_id"] = descriptor_.instance_id;
    node["started_at"] = descriptor_.started_at;
    node["certificate"] = certificate_object(descriptor_.identity_certificate_common_name.empty()
                                                  ? std::optional<std::string>{}
                                                  : std::optional<std::string>{descriptor_.identity_certificate_common_name},
                                              descriptor_.identity_certificate_fingerprint_sha256.empty()
                                                  ? std::optional<std::string>{}
                                                  : std::optional<std::string>{descriptor_.identity_certificate_fingerprint_sha256});
    out["node"] = node;

    boost::json::object platform;
    platform["os_family"] = descriptor_.os_family;
    platform["os_version"] = descriptor_.os_version;
    platform["architecture"] = descriptor_.architecture;
    platform["target_triple"] = descriptor_.target_triple;
    out["platform"] = platform;

    boost::json::object network;
    network["listen_url"] = descriptor_.listen_url;
    network["control_path"] = "/control";
    network["data_path"] = "/data";
    out["network"] = network;

    boost::json::array libraries;
    for (const auto& library : descriptor_.libraries) {
        boost::json::object entry;
        entry["library_id"] = library.library_id;
        entry["kind"] = library.kind;
        entry["version"] = library.version;
        entry["target_triple"] = library.target_triple;
        entry["origin"] = library.origin;
        entry["status"] = library.status;
        if (!library.status_reason.empty()) {
            entry["status_reason"] = library.status_reason;
        }
        boost::json::array elements;
        for (const auto& type_name : library.elements) {
            elements.emplace_back(type_name);
        }
        entry["elements"] = std::move(elements);
        libraries.emplace_back(std::move(entry));
    }
    out["libraries"] = std::move(libraries);

    boost::json::object elements_info;
    boost::json::object summary;
    summary["count"] = descriptor_.element_types.size();
    elements_info["summary"] = summary;
    boost::json::array catalog;
    for (const auto& type_name : descriptor_.element_types) {
        boost::json::object item;
        item["type_name"] = type_name;
        item["deployable"] = true;
        item["deployable_reason"] = "available";
        catalog.emplace_back(std::move(item));
    }
    elements_info["catalog"] = std::move(catalog);
    out["elements"] = std::move(elements_info);

    boost::json::object hardware;
    boost::json::array capabilities;
    for (const auto& capability : descriptor_.hardware_capabilities) {
        capabilities.emplace_back(capability);
    }
    hardware["capabilities"] = std::move(capabilities);
    out["hardware"] = std::move(hardware);

    boost::json::object state;
    state["mode"] = active_deployment_id.empty() ? "idle" : "running";
    state["active_deployment"] = active_deployment_id.empty()
        ? boost::json::value(nullptr)
        : boost::json::value(active_deployment_id);
    out["state"] = std::move(state);

    return out;
}

auto ControlPlaneMvp::handle_get_peers(const boost::json::object& request) const -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    bool include_unverified = true;
    if (const auto it = request.find("include_unverified"); it != request.end()) {
        if (!it->value().is_bool()) {
            return make_error_response(request_id, "INVALID_REQUEST", "include_unverified must be boolean.");
        }
        include_unverified = it->value().as_bool();
    }

    std::size_t max_entries = 256;
    if (const auto it = request.find("max_entries"); it != request.end()) {
        if (it->value().is_uint64()) {
            max_entries = static_cast<std::size_t>(it->value().as_uint64());
        } else if (it->value().is_int64() && it->value().as_int64() > 0) {
            max_entries = static_cast<std::size_t>(it->value().as_int64());
        } else {
            return make_error_response(request_id, "INVALID_REQUEST", "max_entries must be a positive integer.");
        }
        if (max_entries > 4096) {
            return make_error_response(request_id, "INVALID_REQUEST", "max_entries exceeds allowed limit.");
        }
    }

    boost::json::array peers_array;
    std::lock_guard<std::mutex> lock(mutex_);
    std::size_t emitted = 0;
    for (const auto& [_, peer] : peers_) {
        if (!include_unverified && peer.trust_state != "verified") {
            continue;
        }
        if (emitted >= max_entries) {
            break;
        }

        const auto node_uuid = peer.node_uuid
            ? *peer.node_uuid
            : (peer.identity_certificate_common_name ? *peer.identity_certificate_common_name : std::string{});

        boost::json::object item;
        item["endpoint"] = peer.endpoint;
        item["node_uuid"] = node_uuid.empty() ? boost::json::value(nullptr) : boost::json::value(node_uuid);
        item["node_name"] = peer.node_name ? boost::json::value(*peer.node_name) : boost::json::value(nullptr);
        if (peer.os_family || peer.architecture) {
            boost::json::object platform;
            platform["os_family"] = peer.os_family ? boost::json::value(*peer.os_family) : boost::json::value(nullptr);
            platform["architecture"] = peer.architecture ? boost::json::value(*peer.architecture) : boost::json::value(nullptr);
            item["platform"] = std::move(platform);
        } else {
            item["platform"] = nullptr;
        }
        item["trust_state"] = peer.trust_state;
        item["reachability"] = peer.reachability;
        item["last_seen_at"] = peer.last_seen_at ? boost::json::value(*peer.last_seen_at) : boost::json::value(nullptr);
        item["last_failure_at"] = peer.last_failure_at ? boost::json::value(*peer.last_failure_at) : boost::json::value(nullptr);
        item["certificate"] = certificate_object(peer.identity_certificate_common_name,
                                                  peer.identity_certificate_fingerprint_sha256);
        peers_array.emplace_back(std::move(item));
        ++emitted;
    }

    boost::json::object out;
    out["type"] = "PEERS";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["generated_at"] = now_utc_iso8601();
    out["peers"] = std::move(peers_array);
    return out;
}

auto ControlPlaneMvp::handle_peer_announce(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto self_it = request.find("self");
    if (self_it == request.end() || !self_it->value().is_object()) {
        return make_error_response(request_id, "INVALID_REQUEST", "PEER_ANNOUNCE requires object field 'self'.");
    }

    const auto& self = self_it->value().as_object();
    const auto endpoint = as_string(self, "endpoint");
    if (!endpoint.has_value() || endpoint->empty()) {
        return make_error_response(request_id, "INVALID_REQUEST", "PEER_ANNOUNCE self.endpoint must be a non-empty string.");
    }

    const auto certificate = read_certificate_info(self);

    std::lock_guard<std::mutex> lock(mutex_);
    auto& peer = peers_[*endpoint];
    peer.endpoint = *endpoint;
    peer.node_uuid = as_string(self, "node_uuid");
    peer.node_name = as_string(self, "node_name");
    peer.identity_certificate_common_name = certificate.common_name;
    peer.identity_certificate_fingerprint_sha256 = certificate.fingerprint_sha256;
    if (!peer.node_uuid && peer.identity_certificate_common_name) {
        peer.node_uuid = peer.identity_certificate_common_name;
    }
    peer.trust_state = "unverified";
    peer.reachability = "online";
    peer.last_seen_at = now_utc_iso8601();
    peer.last_failure_at = std::nullopt;
    if (const auto platform_it = self.find("platform"); platform_it != self.end() && platform_it->value().is_object()) {
        peer.os_family = as_string(platform_it->value().as_object(), "os_family");
        peer.architecture = as_string(platform_it->value().as_object(), "architecture");
    }
    persist_peers_to_disk();

    boost::json::object out;
    out["type"] = "PEER_ANNOUNCE_OK";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["accepted"] = true;
    return out;
}

auto ControlPlaneMvp::handle_list_deployments(const boost::json::object& request) const -> boost::json::object
{
    std::string active_deployment_id;
    std::vector<PreparedDeployment> prepared_snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active_deployment_id = active_deployment_id_;
        prepared_snapshot.reserve(prepared_.size());
        for (const auto& [_, item] : prepared_) {
            prepared_snapshot.push_back(item);
        }
    }

    boost::json::object out;
    out["type"] = "DEPLOYMENTS";
    out["request_id"] = as_string(request, "request_id").value_or("");
    out["protocol_version"] = descriptor_.protocol_version;
    out["node_uuid"] = descriptor_.node_uuid;
    out["state"] = active_deployment_id.empty() ? "idle" : "running";

    out["active_deployment"] = active_deployment_id.empty()
        ? boost::json::value(nullptr)
        : boost::json::value(boost::json::object{{"deployment_id", active_deployment_id}});

    boost::json::array prepared;
    for (const auto& item : prepared_snapshot) {
        boost::json::object entry;
        entry["deployment_id"] = item.deployment_id;
        entry["prepare_token"] = item.prepare_token;
        entry["prepared_at"] = item.prepared_at;
        entry["expires_at"] = item.expires_at;
        prepared.emplace_back(std::move(entry));
    }
    out["prepared_deployments"] = std::move(prepared);
    out["history"] = boost::json::array{};
    return out;
}

auto ControlPlaneMvp::handle_prepare_deployment(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto deployment_it = request.find("deployment");
    if (deployment_it == request.end() || !deployment_it->value().is_object()) {
        return make_error_response(request_id, "INVALID_REQUEST", "PREPARE_DEPLOYMENT requires object field 'deployment'.");
    }

    const auto& deployment = deployment_it->value().as_object();
    const auto deployment_id = as_string(deployment, "deployment_id").value_or("");
    if (deployment_id.empty()) {
        return make_error_response(request_id, "INVALID_REQUEST", "deployment.deployment_id is required.");
    }

    std::int64_t revision = 1;
    if (const auto rev_it = deployment.find("revision"); rev_it != deployment.end()) {
        if (rev_it->value().is_int64()) {
            revision = rev_it->value().as_int64();
        } else if (rev_it->value().is_uint64()) {
            revision = static_cast<std::int64_t>(rev_it->value().as_uint64());
        } else {
            return make_error_response(request_id, "INVALID_REQUEST", "deployment.revision must be an integer.");
        }
    }

    boost::json::array incompatibilities;
    const auto placement_it = deployment.find("placement");
    if (placement_it != deployment.end() && placement_it->value().is_object()) {
        const auto& placement = placement_it->value().as_object();
        const auto requires_it = placement.find("requires");
        if (requires_it != placement.end() && requires_it->value().is_object()) {
            const auto& required_caps = requires_it->value().as_object();

            if (const auto libs_it = required_caps.find("libraries"); libs_it != required_caps.end() && libs_it->value().is_array()) {
                for (const auto& requirement_value : libs_it->value().as_array()) {
                    if (!requirement_value.is_object()) {
                        continue;
                    }
                    const auto& requirement = requirement_value.as_object();
                    const auto required_id = as_string(requirement, "library_id").value_or("");
                    const auto version_rule = as_string(requirement, "version").value_or("");

                    auto found = std::find_if(descriptor_.libraries.begin(), descriptor_.libraries.end(),
                                              [&required_id](const ControlLibraryInfo& library) { return library.library_id == required_id; });
                    if (found == descriptor_.libraries.end()) {
                        boost::json::object issue;
                        issue["code"] = "MISSING_LIBRARY";
                        issue["scope"] = "library";
                        issue["target"] = required_id;
                        issue["message"] = "Required library is not installed.";
                        issue["details"] = boost::json::object{};
                        incompatibilities.emplace_back(std::move(issue));
                        continue;
                    }
                    if (version_rule.rfind(">=", 0) == 0) {
                        const auto min_version = version_rule.substr(2);
                        if (compare_versions(found->version, min_version) < 0) {
                            boost::json::object details;
                            details["installed"] = found->version;
                            details["required"] = version_rule;
                            boost::json::object issue;
                            issue["code"] = "LIBRARY_VERSION_MISMATCH";
                            issue["scope"] = "library";
                            issue["target"] = required_id;
                            issue["message"] = "Installed library version is lower than required.";
                            issue["details"] = std::move(details);
                            incompatibilities.emplace_back(std::move(issue));
                        }
                    }
                }
            }

            if (const auto elements_it = required_caps.find("elements"); elements_it != required_caps.end() && elements_it->value().is_array()) {
                for (const auto& value : elements_it->value().as_array()) {
                    if (!value.is_string()) {
                        continue;
                    }
                    const auto type_name = std::string(value.as_string().c_str());
                    if (!has_string(descriptor_.element_types, type_name)) {
                        boost::json::object issue;
                        issue["code"] = "MISSING_ELEMENT";
                        issue["scope"] = "element";
                        issue["target"] = type_name;
                        issue["message"] = "Required element type is not available.";
                        issue["details"] = boost::json::object{};
                        incompatibilities.emplace_back(std::move(issue));
                    }
                }
            }

            if (const auto hw_it = required_caps.find("hardware"); hw_it != required_caps.end() && hw_it->value().is_array()) {
                for (const auto& value : hw_it->value().as_array()) {
                    if (!value.is_object()) {
                        continue;
                    }
                    const auto capability = as_string(value.as_object(), "capability").value_or("");
                    if (!capability.empty() && !has_string(descriptor_.hardware_capabilities, capability)) {
                        boost::json::object issue;
                        issue["code"] = "HARDWARE_MISSING";
                        issue["scope"] = "hardware";
                        issue["target"] = capability;
                        issue["message"] = "Required hardware capability is missing.";
                        issue["details"] = boost::json::object{};
                        incompatibilities.emplace_back(std::move(issue));
                    }
                }
            }
        }
    }

    boost::json::object out;
    out["type"] = "PREPARE_DEPLOYMENT_RESULT";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["deployment_id"] = deployment_id;
    out["revision"] = revision;

    if (!incompatibilities.empty()) {
        out["prepare_status"] = "NOT_READY";
        out["prepare_token"] = nullptr;
        out["prepared_at"] = nullptr;
        out["expires_at"] = nullptr;
        out["reservations"] = boost::json::object{{"hardware", boost::json::array{}}, {"streams", boost::json::array{}}};
        out["incompatibilities"] = std::move(incompatibilities);
        return out;
    }

    const auto key = deployment_id + "#" + std::to_string(revision);
    auto prepared_token = make_prepare_token();
    auto prepared_at = now_utc_iso8601();
    auto expires_at = plus_minutes_iso8601(5);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& prepared = prepared_[key];
        if (!prepared.prepare_token.empty()) {
            prepared_token = prepared.prepare_token;
            prepared_at = prepared.prepared_at;
            expires_at = prepared.expires_at;
        } else {
            prepared.deployment_id = deployment_id;
            prepared.revision = revision;
            prepared.prepare_token = prepared_token;
            prepared.prepared_at = prepared_at;
            prepared.expires_at = expires_at;
        }
    }

    out["prepare_status"] = "READY";
    out["prepare_token"] = prepared_token;
    out["prepared_at"] = prepared_at;
    out["expires_at"] = expires_at;
    out["reservations"] = boost::json::object{{"hardware", boost::json::array{}}, {"streams", boost::json::array{}}};
    out["incompatibilities"] = boost::json::array{};
    return out;
}

auto ControlPlaneMvp::handle_commit_deployment(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto deployment_id = as_string(request, "deployment_id").value_or("");
    const auto token = as_string(request, "prepare_token").value_or("");
    if (deployment_id.empty() || token.empty()) {
        return make_error_response(request_id, "INVALID_REQUEST", "COMMIT_DEPLOYMENT requires deployment_id and prepare_token.");
    }

    std::int64_t revision = 1;
    if (const auto rev_it = request.find("revision"); rev_it != request.end()) {
        revision = rev_it->value().is_int64()
            ? rev_it->value().as_int64()
            : static_cast<std::int64_t>(rev_it->value().as_uint64());
    }

    const auto key = deployment_id + "#" + std::to_string(revision);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto prepared_it = prepared_.find(key);
        if (prepared_it == prepared_.end() || prepared_it->second.prepare_token != token) {
            return make_error_response(request_id, "INVALID_REQUEST", "Unknown or expired prepare_token.");
        }
        active_deployment_id_ = deployment_id;
        prepared_.erase(prepared_it);
    }

    boost::json::object out;
    out["type"] = "COMMIT_DEPLOYMENT_RESULT";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["deployment_id"] = deployment_id;
    out["revision"] = revision;
    out["committed"] = true;
    out["state"] = "running";
    out["committed_at"] = now_utc_iso8601();
    return out;
}

auto ControlPlaneMvp::handle_abort_deployment(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto deployment_id = as_string(request, "deployment_id").value_or("");
    const auto token = as_string(request, "prepare_token").value_or("");
    if (deployment_id.empty() || token.empty()) {
        return make_error_response(request_id, "INVALID_REQUEST", "ABORT_DEPLOYMENT requires deployment_id and prepare_token.");
    }

    std::int64_t revision = 1;
    if (const auto rev_it = request.find("revision"); rev_it != request.end()) {
        revision = rev_it->value().is_int64()
            ? rev_it->value().as_int64()
            : static_cast<std::int64_t>(rev_it->value().as_uint64());
    }

    const auto key = deployment_id + "#" + std::to_string(revision);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto prepared_it = prepared_.find(key);
        if (prepared_it == prepared_.end() || prepared_it->second.prepare_token != token) {
            return make_error_response(request_id, "INVALID_REQUEST", "Unknown or expired prepare_token.");
        }
        prepared_.erase(prepared_it);
    }

    boost::json::object out;
    out["type"] = "ABORT_DEPLOYMENT_RESULT";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["deployment_id"] = deployment_id;
    out["revision"] = revision;
    out["aborted"] = true;
    out["state"] = active_deployment_id_.empty() ? "idle" : "running";
    out["aborted_at"] = now_utc_iso8601();
    return out;
}

void ControlPlaneMvp::load_peers_from_disk()
{
    const auto path = storage_dir_ / "peers.json";
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::ifstream in(path);
    if (!in) {
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    boost::system::error_code ec;
    const auto parsed = boost::json::parse(buffer.str(), ec);
    if (ec || !parsed.is_array()) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& item : parsed.as_array()) {
        if (!item.is_object()) {
            continue;
        }
        const auto endpoint = as_string(item.as_object(), "endpoint");
        if (!endpoint || endpoint->empty()) {
            continue;
        }
        auto& peer = peers_[*endpoint];
        peer.endpoint = *endpoint;
        peer.node_uuid = as_string(item.as_object(), "node_uuid");
        peer.node_name = as_string(item.as_object(), "node_name");
        const auto certificate = read_certificate_info(item.as_object());
        peer.identity_certificate_common_name = certificate.common_name;
        peer.identity_certificate_fingerprint_sha256 = certificate.fingerprint_sha256;
        if (!peer.node_uuid && peer.identity_certificate_common_name) {
            peer.node_uuid = peer.identity_certificate_common_name;
        }
        peer.trust_state = as_string(item.as_object(), "trust_state").value_or("unverified");
        peer.reachability = as_string(item.as_object(), "reachability").value_or("offline");
        peer.last_seen_at = as_string(item.as_object(), "last_seen_at");
        peer.last_failure_at = as_string(item.as_object(), "last_failure_at");
    }
}

void ControlPlaneMvp::persist_peers_to_disk() const
{
    if (storage_dir_.empty()) {
        return;
    }

    boost::json::array out;
    for (const auto& [_, peer] : peers_) {
        boost::json::object item;
        item["endpoint"] = peer.endpoint;
        item["node_uuid"] = peer.node_uuid ? boost::json::value(*peer.node_uuid) : boost::json::value(nullptr);
        item["node_name"] = peer.node_name ? boost::json::value(*peer.node_name) : boost::json::value(nullptr);
        item["certificate"] = certificate_object(peer.identity_certificate_common_name,
                                                  peer.identity_certificate_fingerprint_sha256);
        item["trust_state"] = peer.trust_state;
        item["reachability"] = peer.reachability;
        item["last_seen_at"] = peer.last_seen_at ? boost::json::value(*peer.last_seen_at) : boost::json::value(nullptr);
        item["last_failure_at"] = peer.last_failure_at ? boost::json::value(*peer.last_failure_at) : boost::json::value(nullptr);
        out.emplace_back(std::move(item));
    }

    const auto path = storage_dir_ / "peers.json";
    std::ofstream stream(path, std::ios::trunc);
    if (stream) {
        stream << boost::json::serialize(out);
    }
}

} // namespace s7


