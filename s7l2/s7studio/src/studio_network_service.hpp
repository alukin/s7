// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <s7/deployment_client.hpp>

#include <atomic>
#include <functional>
#include <memory>

namespace s7studio::ui {

struct DeployedDiagram {
    bool found = false;
    boost::json::value diagram;
    std::uint64_t revision = 0;
    std::string message;
};

// Typed administration boundary used by Studio. The UI does not construct
// protocol requests or own a transport connection.
class StudioNetworkService {
public:
    virtual ~StudioNetworkService() = default;
    [[nodiscard]] virtual auto deploy(boost::json::object diagram, const s7::deployment::OutputTypeLookup& types,
        const s7::transport::TlsConfig& tls, const std::atomic_bool& cancelled) -> s7::deployment::DeployResult = 0;
    [[nodiscard]] virtual auto fetchDeployedDiagram(const s7::deployment::RuntimeNode& node,
        const s7::transport::TlsConfig& tls, const std::atomic_bool& cancelled) -> DeployedDiagram = 0;
};

using StudioControlRequest = std::function<boost::json::object(const s7::deployment::RuntimeNode&,
    const boost::json::object&, const s7::transport::TlsConfig&, const std::atomic_bool&)>;

// An injected requester makes protocol handling testable without a network.
[[nodiscard]] auto makeStudioNetworkService(StudioControlRequest request = {}) -> std::shared_ptr<StudioNetworkService>;

} // namespace s7studio::ui
