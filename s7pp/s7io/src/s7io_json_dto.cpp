#include "s7io_json_dto.hpp"

#include <stdexcept>

namespace s7::io::dto {

auto WsFrameRequest::fromJson(const boost::json::value& payload) -> WsFrameRequest
{
    if (!payload.is_object()) {
        throw std::runtime_error("request must be an object");
    }

    const auto& obj = payload.as_object();
    WsFrameRequest request;
    if (const auto* op = obj.if_contains("op"); op && op->is_string()) {
        request.op = boost::json::value_to<std::string>(*op);
    }
    if (const auto* sample = obj.if_contains("sample")) {
        if (!sample->is_array()) {
            throw std::runtime_error("missing array field 'sample'");
        }
        request.sample = boost::json::value_to<std::vector<double>>(*sample);
    }
    return request;
}

auto WsFrameRequest::toJson() const -> boost::json::value
{
    boost::json::object obj;
    obj["op"] = op;
    if (!sample.empty()) {
        obj["sample"] = boost::json::value_from(sample);
    }
    return obj;
}

auto WsFrameResponse::errorResponse(const std::string& message) -> WsFrameResponse
{
    WsFrameResponse response;
    response.ok = false;
    response.error = message;
    return response;
}

auto WsFrameResponse::fromJson(const boost::json::value& payload, const std::string& fallbackError) -> WsFrameResponse
{
    if (!payload.is_object()) {
        return errorResponse(fallbackError);
    }

    const auto& obj = payload.as_object();
    WsFrameResponse response;
    if (const auto* okVal = obj.if_contains("ok"); okVal && okVal->is_bool()) {
        response.ok = okVal->as_bool();
    }
    if (const auto* err = obj.if_contains("error"); err && err->is_string()) {
        response.error = boost::json::value_to<std::string>(*err);
    }
    if (const auto* droppedVal = obj.if_contains("dropped"); droppedVal && droppedVal->is_bool()) {
        response.dropped = droppedVal->as_bool();
    }
    if (const auto* emptyVal = obj.if_contains("empty"); emptyVal && emptyVal->is_bool()) {
        response.empty = emptyVal->as_bool();
    }
    if (const auto* sampleVal = obj.if_contains("sample"); sampleVal && sampleVal->is_array()) {
        response.sample = boost::json::value_to<std::vector<double>>(*sampleVal);
    }
    if (!response.ok && !response.error.has_value()) {
        response.error = fallbackError;
    }
    return response;
}

auto WsFrameResponse::toJson() const -> boost::json::value
{
    boost::json::object obj;
    obj["ok"] = ok;
    if (error.has_value()) {
        obj["error"] = *error;
    }
    if (dropped.has_value()) {
        obj["dropped"] = *dropped;
    }
    if (empty.has_value()) {
        obj["empty"] = *empty;
    }
    if (sample.has_value()) {
        obj["sample"] = boost::json::value_from(*sample);
    }
    return obj;
}

auto FileSidecarHeader::toJson() const -> boost::json::value
{
    boost::json::object obj;
    obj["format"] = format;
    obj["domain"] = domain;
    obj["scalar_encoding"] = scalar_encoding;
    obj["num_channels"] = num_channels;
    obj["sample_rate_hz"] = sample_rate_hz;
    obj["timestamp_unix"] = timestamp_unix;
    obj["num_samples"] = num_samples;
    if (metadata.has_value()) {
        obj["metadata"] = *metadata;
    }
    if (metadata_raw.has_value()) {
        obj["metadata_raw"] = *metadata_raw;
    }
    return obj;
}

auto FileSidecarHeader::fromJson(const boost::json::value& payload) -> FileSidecarHeader
{
    FileSidecarHeader out;
    if (!payload.is_object()) {
        return out;
    }

    const auto& obj = payload.as_object();
    if (const auto* v = obj.if_contains("format"); v && v->is_string()) {
        out.format = boost::json::value_to<std::string>(*v);
    }
    if (const auto* v = obj.if_contains("domain"); v && v->is_string()) {
        out.domain = boost::json::value_to<std::string>(*v);
    }
    if (const auto* v = obj.if_contains("scalar_encoding"); v && v->is_string()) {
        out.scalar_encoding = boost::json::value_to<std::string>(*v);
    }
    if (const auto* v = obj.if_contains("num_channels"); v && (v->is_int64() || v->is_uint64())) {
        out.num_channels = boost::json::value_to<uint32_t>(*v);
    }
    if (const auto* v = obj.if_contains("sample_rate_hz"); v && v->is_number()) {
        out.sample_rate_hz = boost::json::value_to<double>(*v);
    }
    if (const auto* v = obj.if_contains("timestamp_unix"); v && v->is_number()) {
        out.timestamp_unix = boost::json::value_to<double>(*v);
    }
    if (const auto* v = obj.if_contains("num_samples"); v && (v->is_int64() || v->is_uint64())) {
        out.num_samples = boost::json::value_to<uint64_t>(*v);
    }
    if (const auto* v = obj.if_contains("metadata")) {
        out.metadata = *v;
    }
    if (const auto* v = obj.if_contains("metadata_raw"); v && v->is_string()) {
        out.metadata_raw = boost::json::value_to<std::string>(*v);
    }
    return out;
}

} // namespace s7::io::dto

