// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <s7/transport.hpp>

#include <atomic>
#include <chrono>
#include <optional>
#include <string>

namespace s7studio::ui {

struct StudioControlReply {
    std::string text;
    std::optional<s7::transport::PeerCertificate> certificate;
};

// One bounded control exchange. Cancellation closes the resolver or socket;
// the caller owns the flag for the duration of the call. When an expected node
// is supplied, its TLS certificate is checked before sending the request.
[[nodiscard]] auto requestStudioControl(const std::string& endpoint, const std::string& requestJson,
    const s7::transport::TlsConfig& tls, const std::atomic_bool& cancelled,
    std::chrono::milliseconds timeout, std::string expectedNodeUuid = {}) -> StudioControlReply;

} // namespace s7studio::ui
