#pragma once

#include <boost/json.hpp>

#include <optional>
#include <string>
#include <vector>

namespace s7::io::dto {

struct WsFrameRequest {
    std::string op;
    std::vector<double> sample;

    static auto fromJson(const boost::json::value& payload) -> WsFrameRequest;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct WsFrameResponse {
    bool ok = true;
    std::optional<std::string> error;
    std::optional<bool> dropped;
    std::optional<bool> empty;
    std::optional<std::vector<double>> sample;

    static auto errorResponse(const std::string& message) -> WsFrameResponse;
    static auto fromJson(const boost::json::value& payload, const std::string& fallbackError) -> WsFrameResponse;
    [[nodiscard]] auto toJson() const -> boost::json::value;
};

struct FileSidecarHeader {
    std::string format = "s7.io.file.v1";
    std::string domain;
    std::string scalar_encoding;
    uint32_t num_channels = 0;
    double sample_rate_hz = 0.0;
    double timestamp_unix = 0.0;
    uint64_t num_samples = 0;
    std::optional<boost::json::value> metadata;
    std::optional<std::string> metadata_raw;

    [[nodiscard]] auto toJson() const -> boost::json::value;
    static auto fromJson(const boost::json::value& payload) -> FileSidecarHeader;
};

} // namespace s7::io::dto

