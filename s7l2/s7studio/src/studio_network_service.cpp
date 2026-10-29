// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "studio_network_service.hpp"
#include "studio_control_client.hpp"

#include <s7/canonical_json.hpp>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace s7studio::ui {
namespace {

auto realRequest(const s7::deployment::RuntimeNode& node, const boost::json::object& request,
                 const s7::transport::TlsConfig& tls, const std::atomic_bool& cancelled) -> boost::json::object
{
    if (!s7::transport::parse_ws_url(node.endpoint).secure)
        throw std::runtime_error("Studio administration requires a wss:// control endpoint");
    const auto reply = requestStudioControl(node.endpoint, boost::json::serialize(request), tls, cancelled,
                                            std::chrono::seconds(10), node.node_uuid);
    return s7::parse_json_precise(reply.text).as_object();
}

class DefaultStudioNetworkService final : public StudioNetworkService {
public:
    explicit DefaultStudioNetworkService(StudioControlRequest request) : request_(std::move(request)) {}

    auto deploy(boost::json::object diagram, const s7::deployment::OutputTypeLookup& types,
                const s7::transport::TlsConfig& tls, const std::atomic_bool& cancelled) -> s7::deployment::DeployResult override
    {
        bool committing = false;
        const std::atomic_bool finishTransaction = false;
        auto result = s7::deployment::deploy_diagram(std::move(diagram), types, [&](const auto& node, const auto& message) {
            const auto type = message.at("type").as_string();
            const bool aborting = type == "ABORT_DEPLOYMENT";
            if (cancelled.load() && !committing && !aborting) throw std::runtime_error("Deployment cancelled");
            if (type == "COMMIT_DEPLOYMENT") committing = true;
            // Abort a prepared transaction even during shutdown. Once commit
            // begins, finish the sequence instead of cancelling halfway.
            return request_(node, message, tls, committing || aborting ? finishTransaction : cancelled);
        });
        if (cancelled.load()) throw std::runtime_error("Deployment cancelled");
        return result;
    }

    auto fetchDeployedDiagram(const s7::deployment::RuntimeNode& node, const s7::transport::TlsConfig& tls,
                              const std::atomic_bool& cancelled) -> DeployedDiagram override
    {
        if (cancelled.load()) throw std::runtime_error("Control request cancelled");
        const auto response = request_(node,
            {{"type", "GET_NETWORK_CONFIGURATION"}, {"request_id", "studio-config"}, {"protocol_version", 1}}, tls, cancelled);
        if (cancelled.load()) throw std::runtime_error("Control request cancelled");
        const auto* type = response.if_contains("type");
        if (!type || !type->is_string() || type->as_string() != "NETWORK_CONFIGURATION") {
            const auto* message = response.if_contains("message");
            return {false, {}, 0, message && message->is_string() ? std::string(message->as_string()) : std::string{}};
        }
        const auto* diagram = response.if_contains("diagram");
        const auto* revision = response.if_contains("revision");
        if (!diagram || !revision || (!revision->is_uint64() && !revision->is_int64()))
            throw std::runtime_error("Malformed NETWORK_CONFIGURATION response");
        return {true, *diagram, revision->to_number<std::uint64_t>(), {}};
    }

private:
    StudioControlRequest request_;
};

} // namespace

auto makeStudioNetworkService(StudioControlRequest request) -> std::shared_ptr<StudioNetworkService>
{
    if (!request) request = realRequest;
    return std::make_shared<DefaultStudioNetworkService>(std::move(request));
}

} // namespace s7studio::ui
