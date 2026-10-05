#include "s7/control_plane_mvp.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include "s7/canonical_json.hpp"
#include "s7/diagram_deployment.hpp"

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

[[nodiscard]] auto plus_seconds_iso8601(const std::chrono::seconds seconds) -> std::string
{
    const auto then = std::chrono::system_clock::now() + seconds;
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

// 256 bits from OpenSSL's CSPRNG (seeded from the operating system).
[[nodiscard]] auto make_prepare_token() -> std::string
{
    unsigned char bytes[32];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1) {
        throw std::runtime_error("Could not generate a prepare token.");
    }
    static constexpr char hex[] = "0123456789abcdef";
    std::string token = "ptok_";
    for (const unsigned char byte : bytes) {
        token.push_back(hex[byte >> 4]);
        token.push_back(hex[byte & 0x0F]);
    }
    return token;
}

[[nodiscard]] auto tokens_equal(const std::string& left, const std::string& right) -> bool
{
    return left.size() == right.size() && CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

// Integers in protocol messages must fit the I-JSON safe range.
constexpr std::uint64_t kMaxSafeInteger = (1ULL << 53);

[[nodiscard]] auto read_revision(const boost::json::object& object, const std::string& key, const bool allow_zero)
    -> std::optional<std::uint64_t>
{
    const auto it = object.find(key);
    if (it == object.end()) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    if (it->value().is_uint64()) {
        value = it->value().as_uint64();
    } else if (it->value().is_int64() && it->value().as_int64() >= 0) {
        value = static_cast<std::uint64_t>(it->value().as_int64());
    } else {
        return std::nullopt;
    }
    if (value > kMaxSafeInteger || (!allow_zero && value == 0)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] auto deployment_key(const std::string& deployment_id, const std::uint64_t revision) -> std::string
{
    return deployment_id + "#" + std::to_string(revision);
}

[[nodiscard]] auto make_issue(const std::string& code,
                              const std::string& scope,
                              const std::string& target,
                              const std::string& message,
                              boost::json::object details = {}) -> boost::json::object
{
    boost::json::object issue;
    issue["code"] = code;
    issue["scope"] = scope;
    issue["target"] = target;
    issue["message"] = message;
    issue["details"] = std::move(details);
    return issue;
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

ControlPlaneMvp::ControlPlaneMvp(ControlNodeDescriptor descriptor, ControlPlaneOptions options)
    : descriptor_(std::move(descriptor))
    , options_(std::move(options))
{
    if (!options_.storage_dir.empty()) {
        std::filesystem::create_directories(options_.storage_dir);
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

auto ControlPlaneMvp::handle_request_json(const transport::ControlPeer& peer, const std::string& request_json)
    -> std::string
{
    try {
        const auto parsed = parse_json_precise(request_json);
        if (!parsed.is_object()) {
            return boost::json::serialize(make_error_response({}, "INVALID_REQUEST", "Request must be a JSON object."));
        }
        return boost::json::serialize(handle_request_object(peer, parsed.as_object()));
    } catch (const std::exception& ex) {
        return boost::json::serialize(make_error_response({}, "INVALID_REQUEST", ex.what()));
    }
}

auto ControlPlaneMvp::handle_request_object(const transport::ControlPeer& peer, const boost::json::object& request)
    -> boost::json::object
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
    if (request_type == "GET_NETWORK_CONFIGURATION") {
        return handle_get_network_configuration(request);
    }
    if (request_type == "GET_STREAM_STATUS") {
        return handle_get_stream_status(request);
    }
    if (request_type == "PREPARE_DEPLOYMENT" || request_type == "COMMIT_DEPLOYMENT" || request_type == "ABORT_DEPLOYMENT" ||
        request_type == "RECOVER_STREAM") {
        if (auto denied = authorize_mutation(peer, request_id, request_type)) {
            return std::move(*denied);
        }
        if (request_type == "RECOVER_STREAM") {
            return handle_recover_stream(request);
        }
        if (request_type == "PREPARE_DEPLOYMENT") {
            return handle_prepare_deployment(request);
        }
        if (request_type == "COMMIT_DEPLOYMENT") {
            return handle_commit_deployment(request);
        }
        return handle_abort_deployment(request);
    }

    return make_error_response(request_id, "UNSUPPORTED_MESSAGE_TYPE", "Unsupported control message type: '" + request_type + "'.");
}

// Configuration changes are authorized from the TLS session alone: the peer's
// verified certificate must carry the control-node key purpose and be the
// pinned Studio certificate. Identity claims in the JSON are never consulted.
auto ControlPlaneMvp::authorize_mutation(const transport::ControlPeer& peer,
                                         const std::string& request_id,
                                         const std::string& request_type) const -> std::optional<boost::json::object>
{
    const auto fingerprint = peer.certificate ? peer.certificate->fingerprint_sha256 : std::string{};
    auto deny = [&](const std::string& code, const std::string& message) {
        if (options_.log) {
            options_.log("control.denied type=" + request_type + " code=" + code +
                         " peer_certificate_sha256=" + (fingerprint.empty() ? "none" : fingerprint) +
                         " reason=\"" + message + "\"");
        }
        auto error = make_error_response(request_id, code, message);
        if (!fingerprint.empty()) {
            error["details"] = boost::json::object{{"peer_certificate_sha256", fingerprint}};
        }
        return error;
    };

    if (!peer.tls) {
        return deny("UNAUTHORIZED", "Configuration changes require a mutual-TLS control connection.");
    }
    if (!peer.certificate) {
        return deny("CERTIFICATE_REQUIRED", "Configuration changes require a verified client certificate.");
    }
    const auto& authorization = options_.authorization;
    if (!authorization.configured()) {
        return deny("UNAUTHORIZED", "No authorized Studio is configured on this node.");
    }
    if (!has_string(peer.certificate->key_purpose_oids, authorization.studio_key_purpose_oid)) {
        return deny("UNAUTHORIZED", "Client certificate does not carry the control-node key purpose.");
    }
    if (!tokens_equal(fingerprint, authorization.studio_certificate_sha256)) {
        return deny("UNAUTHORIZED", "Client certificate is not the authorized Studio certificate.");
    }
    if (options_.log) {
        options_.log("control.authorized type=" + request_type + " peer_certificate_sha256=" + fingerprint);
    }
    return std::nullopt;
}

auto ControlPlaneMvp::state_name_locked() const -> std::string
{
    return active_ ? "active" : "idle";
}

void ControlPlaneMvp::expire_prepared_locked(const std::chrono::steady_clock::time_point now)
{
    for (auto it = prepared_.begin(); it != prepared_.end();) {
        if (it->second.expires <= now) {
            record_outcome_locked(it->second, "expired");
            it = prepared_.erase(it);
        } else {
            ++it;
        }
    }
}

void ControlPlaneMvp::record_outcome_locked(const PreparedDeployment& prepared, const std::string& outcome)
{
    constexpr std::size_t max_history = 64;
    history_.push_back(DeploymentOutcome{prepared.deployment_id, prepared.revision, outcome});
    if (history_.size() > max_history) {
        history_.erase(history_.begin());
    }
}

auto ControlPlaneMvp::handle_hello(const boost::json::object& request) const -> boost::json::object
{
    std::string state;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        state = state_name_locked();
    }

    boost::json::object out;
    out["type"] = "HELLO_OK";
    out["request_id"] = as_string(request, "request_id").value_or("");
    out["protocol_version"] = descriptor_.protocol_version;
    out["node_uuid"] = descriptor_.node_uuid;
    out["node_name"] = descriptor_.node_name;
    out["runtime_version"] = descriptor_.runtime_version;
    out["status"] = state;
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
    std::string mode;
    boost::json::value active_deployment = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        mode = state_name_locked();
        if (active_) {
            active_deployment = active_->deployment_id;
        }
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
    state["mode"] = mode;
    state["active_deployment"] = active_deployment;
    state["execution"] = options_.deployment_host ? options_.deployment_host->execution_state() : "not_stepping";
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

        boost::json::object item;
        item["endpoint"] = peer.endpoint;
        item["node_uuid"] = peer.node_uuid ? boost::json::value(*peer.node_uuid) : boost::json::value(nullptr);
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
    boost::json::object out;
    out["type"] = "DEPLOYMENTS";
    out["request_id"] = as_string(request, "request_id").value_or("");
    out["protocol_version"] = descriptor_.protocol_version;

    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    out["state"] = state_name_locked();
    out["execution"] = options_.deployment_host ? options_.deployment_host->execution_state() : "not_stepping";
    out["active_revision"] = active_ ? active_->revision : 0;
    if (active_) {
        boost::json::object active;
        active["deployment_id"] = active_->deployment_id;
        active["revision"] = active_->revision;
        active["project_revision"] = active_->project_revision;
        active["configuration_digest"] = active_->configuration_digest;
        active["committed_at"] = active_->committed_at;
        out["active_deployment"] = std::move(active);
    } else {
        out["active_deployment"] = nullptr;
    }

    // Prepare tokens are bearer secrets: only the PREPARE_DEPLOYMENT_RESULT carries them.
    boost::json::array prepared;
    for (const auto& [_, item] : prepared_) {
        if (item.expires <= now) {
            continue;
        }
        boost::json::object entry;
        entry["deployment_id"] = item.deployment_id;
        entry["revision"] = item.revision;
        entry["base_revision"] = item.base_revision;
        entry["configuration_digest"] = item.configuration_digest;
        entry["prepared_at"] = item.prepared_at;
        entry["expires_at"] = item.expires_at;
        prepared.emplace_back(std::move(entry));
    }
    out["prepared_deployments"] = std::move(prepared);

    boost::json::array history;
    for (const auto& item : history_) {
        history.emplace_back(boost::json::object{
            {"deployment_id", item.deployment_id},
            {"revision", item.revision},
            {"outcome", item.outcome},
        });
    }
    out["history"] = std::move(history);
    return out;
}

auto ControlPlaneMvp::handle_prepare_deployment(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    auto invalid = [&request_id](const std::string& message) {
        return make_error_response(request_id, "INVALID_REQUEST", message);
    };

    const auto deployment_it = request.find("deployment");
    if (deployment_it == request.end() || !deployment_it->value().is_object()) {
        return invalid("PREPARE_DEPLOYMENT requires object field 'deployment'.");
    }
    const auto& deployment = deployment_it->value().as_object();

    const auto deployment_id = as_string(deployment, "deployment_id").value_or("");
    if (deployment_id.empty()) {
        return invalid("deployment.deployment_id must be a non-empty string.");
    }
    const auto revision = read_revision(deployment, "revision", false);
    if (!revision) {
        return invalid("deployment.revision must be an integer in [1, 2^53].");
    }
    const auto base_revision = read_revision(deployment, "base_revision", true);
    if (!base_revision) {
        return invalid("deployment.base_revision must be an integer in [0, 2^53] (0: no active deployment).");
    }

    const auto configuration_it = deployment.find("configuration");
    if (configuration_it == deployment.end() || !configuration_it->value().is_object()) {
        return invalid("deployment.configuration must be an object.");
    }
    const auto& configuration = configuration_it->value().as_object();
    const auto project_revision = as_string(configuration, "project_revision").value_or("");
    if (project_revision.empty()) {
        return invalid("configuration.project_revision must be a non-empty string.");
    }
    const auto claimed_digest = as_string(configuration, "configuration_digest").value_or("");
    if (claimed_digest.empty()) {
        return invalid("configuration.configuration_digest must be a non-empty string.");
    }
    const auto diagram_it = configuration.find("diagram");
    if (diagram_it == configuration.end() || !diagram_it->value().is_object()) {
        return invalid("configuration.diagram must be an object.");
    }
    const auto& diagram = diagram_it->value().as_object();

    // The digest covers the configuration object without its digest member.
    auto digested = configuration;
    digested.erase("configuration_digest");
    std::string digest;
    try {
        digest = configuration_digest(digested);
    } catch (const std::exception& ex) {
        return invalid(std::string{"configuration cannot be canonicalized: "} + ex.what());
    }
    if (digest != claimed_digest) {
        boost::json::object details{{"expected", digest}, {"received", claimed_digest}};
        auto error = invalid("configuration_digest does not match the configuration content.");
        error["details"] = std::move(details);
        return error;
    }

    boost::json::array incompatibilities;
    if (const auto requires_it = deployment.find("requires"); requires_it != deployment.end()) {
        if (!requires_it->value().is_object()) {
            return invalid("deployment.requires must be an object.");
        }
        const auto& required_caps = requires_it->value().as_object();

        if (const auto libs_it = required_caps.find("libraries"); libs_it != required_caps.end()) {
            if (!libs_it->value().is_array()) {
                return invalid("requires.libraries must be an array.");
            }
            for (const auto& requirement_value : libs_it->value().as_array()) {
                const auto* requirement = requirement_value.if_object();
                const auto required_id = requirement ? as_string(*requirement, "library_id").value_or("") : "";
                if (required_id.empty()) {
                    return invalid("requires.libraries entries must be objects with a non-empty library_id.");
                }
                const auto version_rule = as_string(*requirement, "version").value_or("");
                if (!version_rule.empty() && version_rule.rfind(">=", 0) != 0) {
                    return invalid("requires.libraries version rules must have the form '>=X.Y.Z'.");
                }

                const auto found = std::find_if(descriptor_.libraries.begin(), descriptor_.libraries.end(),
                                                [&required_id](const ControlLibraryInfo& library) { return library.library_id == required_id; });
                if (found == descriptor_.libraries.end()) {
                    incompatibilities.emplace_back(make_issue("MISSING_LIBRARY", "library", required_id,
                                                              "Required library is not installed."));
                } else if (!version_rule.empty() && compare_versions(found->version, version_rule.substr(2)) < 0) {
                    incompatibilities.emplace_back(make_issue("LIBRARY_VERSION_MISMATCH", "library", required_id,
                                                              "Installed library version is lower than required.",
                                                              {{"installed", found->version}, {"required", version_rule}}));
                }
            }
        }

        if (const auto elements_it = required_caps.find("elements"); elements_it != required_caps.end()) {
            if (!elements_it->value().is_array()) {
                return invalid("requires.elements must be an array.");
            }
            for (const auto& value : elements_it->value().as_array()) {
                if (!value.is_string() || value.as_string().empty()) {
                    return invalid("requires.elements entries must be non-empty strings.");
                }
                const auto type_name = std::string(value.as_string().c_str());
                if (!has_string(descriptor_.element_types, type_name)) {
                    incompatibilities.emplace_back(make_issue("MISSING_ELEMENT", "element", type_name,
                                                              "Required element type is not available."));
                }
            }
        }

        if (const auto hw_it = required_caps.find("hardware"); hw_it != required_caps.end()) {
            if (!hw_it->value().is_array()) {
                return invalid("requires.hardware must be an array.");
            }
            for (const auto& value : hw_it->value().as_array()) {
                const auto* entry = value.if_object();
                const auto capability = entry ? as_string(*entry, "capability").value_or("") : "";
                if (capability.empty()) {
                    return invalid("requires.hardware entries must be objects with a non-empty capability.");
                }
                if (!has_string(descriptor_.hardware_capabilities, capability)) {
                    incompatibilities.emplace_back(make_issue("HARDWARE_MISSING", "hardware", capability,
                                                              "Required hardware capability is missing."));
                }
            }
        }
    }

    boost::json::object out;
    out["type"] = "PREPARE_DEPLOYMENT_RESULT";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["deployment_id"] = deployment_id;
    out["revision"] = *revision;
    out["configuration_digest"] = digest;
    out["reservations"] = boost::json::object{{"hardware", boost::json::array{}}, {"streams", boost::json::array{}}};

    auto not_ready = [&out](boost::json::array issues) {
        out["prepare_status"] = "NOT_READY";
        out["prepare_token"] = nullptr;
        out["prepared_at"] = nullptr;
        out["expires_at"] = nullptr;
        out["incompatibilities"] = std::move(issues);
        return out;
    };
    auto ready = [&out](const PreparedDeployment& prepared) {
        out["prepare_status"] = "READY";
        out["prepare_token"] = prepared.prepare_token;
        out["prepared_at"] = prepared.prepared_at;
        out["expires_at"] = prepared.expires_at;
        out["incompatibilities"] = boost::json::array{};
        return out;
    };

    const auto key = deployment_key(deployment_id, *revision);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        expire_prepared_locked(std::chrono::steady_clock::now());
        const auto active_revision = active_ ? active_->revision : 0;
        if (*base_revision != active_revision) {
            auto error = make_error_response(request_id, "REVISION_CONFLICT",
                                             "base_revision does not match the node's active revision.");
            error["details"] = boost::json::object{{"active_revision", active_revision}, {"base_revision", *base_revision}};
            return error;
        }
        if (const auto existing = prepared_.find(key); existing != prepared_.end()) {
            if (existing->second.configuration_digest != digest) {
                return invalid("This deployment revision is already prepared with a different configuration.");
            }
            return ready(existing->second);
        }
    }

    if (!incompatibilities.empty()) {
        return not_ready(std::move(incompatibilities));
    }
    if (!options_.deployment_host) {
        return make_error_response(request_id, "CAPABILITY_MISSING", "This node does not accept deployments.");
    }

    // This node runs its own part of the diagram.
    std::shared_ptr<StagedConfiguration> staged;
    boost::json::object node_schema;
    try {
        const auto placed = deployment::placed_nodes(diagram);
        if (std::ranges::find(placed, descriptor_.node_uuid) == placed.end()) {
            throw std::invalid_argument("No element of the diagram is placed on this node (" + descriptor_.node_uuid + ").");
        }
        node_schema = deployment::extract_node_schema(diagram, descriptor_.node_uuid);
        staged = options_.deployment_host->stage(node_schema, deployment::producer_endpoints(diagram, descriptor_.node_uuid));
    } catch (const std::exception& ex) {
        boost::json::array issues;
        issues.emplace_back(make_issue("INVALID_CONFIGURATION", "configuration", deployment_id, ex.what()));
        return not_ready(std::move(issues));
    }

    PreparedDeployment prepared;
    prepared.deployment_id = deployment_id;
    prepared.revision = *revision;
    prepared.base_revision = *base_revision;
    prepared.project_revision = project_revision;
    prepared.configuration_digest = digest;
    prepared.prepare_token = make_prepare_token();
    prepared.prepared_at = now_utc_iso8601();
    prepared.expires = std::chrono::steady_clock::now() + options_.prepare_ttl;
    prepared.expires_at = plus_seconds_iso8601(options_.prepare_ttl);
    prepared.staged = std::move(staged);
    prepared.diagram = diagram;
    prepared.node_schema = std::move(node_schema);

    std::lock_guard<std::mutex> lock(mutex_);
    // A concurrent identical prepare may have won; keep the first token.
    const auto [it, inserted] = prepared_.try_emplace(key, std::move(prepared));
    return ready(it->second);
}

auto ControlPlaneMvp::handle_commit_deployment(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto deployment_id = as_string(request, "deployment_id").value_or("");
    const auto token = as_string(request, "prepare_token").value_or("");
    const auto revision = read_revision(request, "revision", false);
    if (deployment_id.empty() || token.empty() || !revision) {
        return make_error_response(request_id, "INVALID_REQUEST",
                                   "COMMIT_DEPLOYMENT requires deployment_id, revision in [1, 2^53], and prepare_token.");
    }

    auto committed = [&](const ActiveDeployment& active) {
        boost::json::object out;
        out["type"] = "COMMIT_DEPLOYMENT_RESULT";
        out["request_id"] = request_id;
        out["protocol_version"] = descriptor_.protocol_version;
        out["deployment_id"] = active.deployment_id;
        out["revision"] = active.revision;
        out["configuration_digest"] = active.configuration_digest;
        out["committed"] = true;
        out["state"] = "active";
        out["execution"] = options_.deployment_host ? options_.deployment_host->execution_state() : "not_stepping";
        out["committed_at"] = active.committed_at;
        return out;
    };

    const auto key = deployment_key(deployment_id, *revision);
    std::lock_guard<std::mutex> lock(mutex_);

    if (active_ && active_->deployment_id == deployment_id && active_->revision == *revision &&
        tokens_equal(active_->prepare_token, token)) {
        return committed(*active_);
    }

    const auto prepared_it = prepared_.find(key);
    if (prepared_it == prepared_.end() || !tokens_equal(prepared_it->second.prepare_token, token)) {
        const auto expired = std::any_of(history_.begin(), history_.end(), [&](const DeploymentOutcome& item) {
            return item.deployment_id == deployment_id && item.revision == *revision && item.outcome == "expired";
        });
        return expired
            ? make_error_response(request_id, "PREPARE_EXPIRED", "The prepared deployment has expired.")
            : make_error_response(request_id, "PREPARE_TOKEN_INVALID", "Unknown deployment or prepare_token.");
    }

    auto& prepared = prepared_it->second;
    if (prepared.expires <= std::chrono::steady_clock::now()) {
        record_outcome_locked(prepared, "expired");
        prepared_.erase(prepared_it);
        return make_error_response(request_id, "PREPARE_EXPIRED", "The prepared deployment has expired.");
    }
    const auto active_revision = active_ ? active_->revision : 0;
    if (prepared.base_revision != active_revision) {
        record_outcome_locked(prepared, "superseded");
        prepared_.erase(prepared_it);
        return make_error_response(request_id, "REVISION_CONFLICT",
                                   "Another deployment became active after this one was prepared.");
    }

    try {
        options_.deployment_host->activate(*prepared.staged);
    } catch (const std::exception& ex) {
        record_outcome_locked(prepared, "failed");
        prepared_.erase(prepared_it);
        return make_error_response(request_id, "INTERNAL_ERROR",
                                   std::string{"Activation failed; the previous configuration remains active: "} + ex.what());
    }

    active_ = ActiveDeployment{
        .deployment_id = prepared.deployment_id,
        .revision = prepared.revision,
        .project_revision = prepared.project_revision,
        .configuration_digest = prepared.configuration_digest,
        .committed_at = now_utc_iso8601(),
        .prepare_token = prepared.prepare_token,
        .diagram = std::move(prepared.diagram),
        .node_schema = std::move(prepared.node_schema),
    };
    record_outcome_locked(prepared, "committed");
    prepared_.erase(prepared_it);
    return committed(*active_);
}

auto ControlPlaneMvp::handle_abort_deployment(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto deployment_id = as_string(request, "deployment_id").value_or("");
    const auto token = as_string(request, "prepare_token").value_or("");
    const auto revision = read_revision(request, "revision", false);
    if (deployment_id.empty() || token.empty() || !revision) {
        return make_error_response(request_id, "INVALID_REQUEST",
                                   "ABORT_DEPLOYMENT requires deployment_id, revision in [1, 2^53], and prepare_token.");
    }

    const auto key = deployment_key(deployment_id, *revision);
    std::lock_guard<std::mutex> lock(mutex_);

    auto aborted = [&] {
        boost::json::object out;
        out["type"] = "ABORT_DEPLOYMENT_RESULT";
        out["request_id"] = request_id;
        out["protocol_version"] = descriptor_.protocol_version;
        out["deployment_id"] = deployment_id;
        out["revision"] = *revision;
        out["aborted"] = true;
        out["state"] = state_name_locked();
        out["aborted_at"] = now_utc_iso8601();
        return out;
    };

    if (const auto prepared_it = prepared_.find(key); prepared_it != prepared_.end()) {
        if (!tokens_equal(prepared_it->second.prepare_token, token)) {
            return make_error_response(request_id, "PREPARE_TOKEN_INVALID", "Unknown deployment or prepare_token.");
        }
        // Aborting an expired preparation discards it just the same.
        record_outcome_locked(prepared_it->second, "aborted");
        prepared_.erase(prepared_it);
        return aborted();
    }

    if (active_ && active_->deployment_id == deployment_id && active_->revision == *revision) {
        return make_error_response(request_id, "INVALID_REQUEST",
                                   "A committed deployment cannot be aborted; deploy an earlier revision instead.");
    }
    // Abort is idempotent: a preparation that was already discarded stays discarded.
    const auto discarded = std::any_of(history_.begin(), history_.end(), [&](const DeploymentOutcome& item) {
        return item.deployment_id == deployment_id && item.revision == *revision &&
               (item.outcome == "aborted" || item.outcome == "expired" || item.outcome == "superseded" ||
                item.outcome == "failed");
    });
    if (discarded) {
        return aborted();
    }
    return make_error_response(request_id, "PREPARE_TOKEN_INVALID", "Unknown deployment or prepare_token.");
}

auto ControlPlaneMvp::handle_get_stream_status(const boost::json::object& request) const -> boost::json::object
{
    boost::json::object out;
    out["type"] = "STREAM_STATUS";
    out["request_id"] = as_string(request, "request_id").value_or("");
    out["protocol_version"] = descriptor_.protocol_version;
    boost::json::array streams;
    if (options_.network) {
        for (const auto& status : options_.network->stream_statuses()) {
            streams.emplace_back(boost::json::object{
                {"stream_id", wire::to_string(status.stream_id)},
                {"role", status.role == transport::StreamRole::Producer ? "producer" : "consumer"},
                {"data_type", std::string(signal_type_name(status.type))},
                {"state", status.state},
                {"fault", stream::to_string(status.fault)},
                {"next_sequence", status.next_sequence},
                {"packets", status.packets},
                {"elements", status.elements},
                {"replayed", status.replayed},
                {"lost_resumes", status.lost_resumes},
                {"duplicates_discarded", status.duplicates_discarded},
                {"malformed", status.malformed},
                {"send_log_depth", status.send_log_depth},
                {"buffered_elements", status.buffered_elements},
                {"blocked_ms_total", status.blocked_total.count()},
            });
        }
    }
    out["streams"] = std::move(streams);
    return out;
}

auto ControlPlaneMvp::handle_get_network_configuration(const boost::json::object& request) const -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    std::optional<std::uint64_t> requested;
    if (request.contains("revision")) {
        requested = read_revision(request, "revision", false);
        if (!requested) {
            return make_error_response(request_id, "INVALID_REQUEST", "revision must be an integer in [1, 2^53].");
        }
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // Only the active configuration is retained.
    if (!active_ || (requested && *requested != active_->revision)) {
        auto error = make_error_response(request_id, "REVISION_NOT_FOUND",
                                         active_ ? "Only the active revision is retained." : "No configuration has been committed.");
        error["details"] = boost::json::object{{"active_revision", active_ ? active_->revision : 0}};
        return error;
    }
    boost::json::object out;
    out["type"] = "NETWORK_CONFIGURATION";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["node_uuid"] = descriptor_.node_uuid;
    out["deployment_id"] = active_->deployment_id;
    out["revision"] = active_->revision;
    out["project_revision"] = active_->project_revision;
    out["configuration_digest"] = active_->configuration_digest;
    out["committed_at"] = active_->committed_at;
    out["diagram"] = active_->diagram;
    out["node_schema"] = active_->node_schema;
    return out;
}

auto ControlPlaneMvp::handle_recover_stream(const boost::json::object& request) -> boost::json::object
{
    const auto request_id = as_string(request, "request_id").value_or("");
    const auto stream_id = as_string(request, "stream_id").value_or("");
    wire::StreamId id{};
    try {
        id = wire::parse_stream_id(stream_id);
    } catch (const std::exception& ex) {
        return make_error_response(request_id, "INVALID_REQUEST", ex.what());
    }
    const bool recovered = options_.network && options_.network->recover_stream(id);
    if (recovered && options_.on_stream_recovered) {
        options_.on_stream_recovered();
    }
    boost::json::object out;
    out["type"] = "RECOVER_STREAM_RESULT";
    out["request_id"] = request_id;
    out["protocol_version"] = descriptor_.protocol_version;
    out["stream_id"] = stream_id;
    out["recovered"] = recovered;
    return out;
}

void ControlPlaneMvp::load_peers_from_disk()
{
    const auto path = options_.storage_dir / "peers.json";
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
        peer.trust_state = as_string(item.as_object(), "trust_state").value_or("unverified");
        peer.reachability = as_string(item.as_object(), "reachability").value_or("offline");
        peer.last_seen_at = as_string(item.as_object(), "last_seen_at");
        peer.last_failure_at = as_string(item.as_object(), "last_failure_at");
    }
}

void ControlPlaneMvp::persist_peers_to_disk() const
{
    if (options_.storage_dir.empty()) {
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

    const auto path = options_.storage_dir / "peers.json";
    std::ofstream stream(path, std::ios::trunc);
    if (stream) {
        stream << boost::json::serialize(out);
    }
}

} // namespace s7


