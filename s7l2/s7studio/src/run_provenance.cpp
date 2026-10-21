// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "run_provenance.hpp"
#include "observation/run_metadata.hpp"
#include "s7studio_build_identity.hpp"
#include <s7/core.hpp>
#include <s7/net.hpp>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QUuid>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace s7studio {
namespace {
namespace json = boost::json;
QString text(const std::string& value) { return QString::fromStdString(value); }
// Sort object keys for identity while preserving JSON number types/precision.
json::value canonical(const json::value& value) {
    if (value.is_array()) {
        json::array out; for (const auto& item : value.as_array()) out.push_back(canonical(item));
        return out;
    }
    if (!value.is_object()) return value;
    std::vector<std::string> keys;
    for (const auto& item : value.as_object()) keys.emplace_back(item.key());
    std::sort(keys.begin(), keys.end());
    json::object out;
    for (const auto& key : keys) out[key] = canonical(value.as_object().at(key));
    return out;
}
QString serialized(const json::value& value) { return text(json::serialize(canonical(value))); }
QString hash(const QString& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value.toUtf8(), QCryptographicHash::Sha256).toHex());
}
void freeze(json::value& schema, QJsonArray& versions, QJsonArray& seeds, QJsonArray& initial,
            const std::string& path = {}, int depth = 0) {
    if (depth > 64) throw std::invalid_argument("Run provenance: nested schema depth exceeds 64.");
    auto& root = schema.as_object();
    for (auto& value : root.at("elements").as_array()) {
        auto& element = value.as_object();
        const auto id = std::string(element.at("id").as_string());
        const auto type = std::string(element.at("type").as_string());
        const auto name = path.empty() ? id : path + "/" + id;
        const auto* descriptor = s7::ElementRegistry::instance().find(type);
        if (!element.if_contains("schema") && descriptor && descriptor->metadata().schema_definition)
            element["schema"] = *descriptor->metadata().schema_definition;
        versions.append(QJsonObject{{"path", text(name)}, {"type", text(type)},
            {"version", descriptor ? text(descriptor->metadata().version) : QString()}});
        if (auto* nested = element.if_contains("schema"); nested && nested->is_object()) {
            // The existing nested-schema factory uses the embedded schema,
            // not wrapper params. Preserve the request separately.
            element["params"] = json::object{};
            freeze(*nested, versions, seeds, initial, name, depth + 1);
            continue;
        }
        if (!descriptor) throw std::invalid_argument("Run provenance: unknown element type '" + type + "'.");
        auto params = s7::checked_params(name, descriptor->metadata(),
            element.if_contains("params") ? element.at("params").as_object() : json::object{});
        for (auto& parameter : params) {
            const auto key = std::string(parameter.key());
            if (key == "seed") {
                bool generated = false;
                if ((type == "WhiteNoiseGenerator" || type == "PinkNoiseGenerator") && parameter.value().to_number<std::int64_t>() == 0) {
                    parameter.value() = 1 + QRandomGenerator::system()->bounded(2'147'483'646);
                    generated = true;
                }
                seeds.append(QJsonObject{{"path", text(name)}, {"parameter", text(key)},
                    {"value_json", text(json::serialize(parameter.value()))}, {"generated", generated}});
            }
            if (key.starts_with("initial_") || key == "initial" || key == "phase")
                initial.append(QJsonObject{{"path", text(name)}, {"parameter", text(key)},
                    {"value_json", text(json::serialize(parameter.value()))}});
        }
        element["params"] = std::move(params);
    }
}
}
PreparedRun prepareRunProvenance(const RunConfiguration& config) {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    s7::register_net_elements(s7::ElementRegistry::instance());
    auto effective = config.schemaJson;
    // The run dt is authoritative, exactly as in LocalSchemaRunner.
    effective.as_object()["dt"] = config.dt;
    QJsonArray versions, seeds, initial, probes;
    freeze(effective, versions, seeds, initial);
    const auto rate = s7::Rate::from_seconds(config.dt);
    for (const auto& probe : config.probes)
        probes.append(QJsonObject{{"name", text(probe.name)}, {"element", text(probe.element)}, {"port", probe.port}, {"unit", text(probe.unit)}});
    const auto model = serialized(effective);
    QJsonObject provenance{{"format_version", 1}, {"run_id", QString::number(config.id)},
        {"run_uuid", QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {"prepared_utc", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {"model_name", effective.as_object().if_contains("name") ? text(std::string(effective.as_object().at("name").as_string())) : QString()},
        {"requested_model_json", serialized(config.schemaJson)}, {"effective_model_json", model},
        {"model_sha256", hash(model)}, {"requested_steps", QString::number(config.ticks)},
        {"dt_seconds", config.dt}, {"rate", QJsonObject{{"numerator", QString::number(rate.numerator)}, {"denominator", QString::number(rate.denominator)}}},
        {"probes", probes}, {"initial_conditions", initial}, {"seeds", seeds}, {"element_versions", versions},
        {"software", QJsonObject{{"project_version", S7_BUILD_VERSION}, {"revision", S7_BUILD_REVISION},
            {"modified_sources", bool(S7_BUILD_DIRTY)}, {"compiler", S7_BUILD_COMPILER},
            {"qt_version", qVersion()}, {"platform", QSysInfo::buildAbi()},
            {"standard_library", QStringLiteral("libstdc++ %1").arg(__GLIBCXX__)}}},
        {"reproduction_limits", QStringLiteral("Configuration replay requires matching element implementations and external input histories. Same-platform deterministic and explicitly seeded S7 noise models are tested; external I/O, arbitrary plugins and cross-platform bitwise/RNG equivalence are not guaranteed.")}};
    return {std::move(effective), std::move(provenance)};
}
RunConfiguration configurationFromProvenance(const QJsonObject& p) {
    QString error;
    if (!observation::validateRunProvenance(p, &error)) throw std::invalid_argument(error.toStdString());
    if (p.value("format_version").toInt() != 1 || !p.value("effective_model_json").isString()
        || !p.value("dt_seconds").isDouble() || !p.value("probes").isArray())
        throw std::invalid_argument("Missing or unsupported run provenance.");
    const auto model = p.value("effective_model_json").toString();
    if (hash(model) != p.value("model_sha256").toString()) throw std::invalid_argument("Run model fingerprint mismatch.");
    bool ok = false;
    const auto steps = p.value("requested_steps").toString().toLongLong(&ok);
    if (!ok || steps < -1 || steps > std::numeric_limits<long>::max()) throw std::invalid_argument("Invalid recorded step count.");
    const auto dt = p.value("dt_seconds").toDouble();
    const auto rate = s7::Rate::from_seconds(dt);
    const auto recorded = p.value("rate").toObject();
    if (recorded.value("numerator").toString() != QString::number(rate.numerator)
        || recorded.value("denominator").toString() != QString::number(rate.denominator))
        throw std::invalid_argument("Recorded rate and dt disagree.");
    std::vector<RunProbe> probes;
    for (const auto& item : p.value("probes").toArray()) {
        const auto probe = item.toObject();
        const auto port = probe.value("port").toInteger(-1);
        if (!probe.value("name").isString() || !probe.value("element").isString() || port < 0 || port > std::numeric_limits<int>::max())
            throw std::invalid_argument("Invalid recorded probe.");
        probes.push_back({probe.value("name").toString().toStdString(), probe.value("element").toString().toStdString(), static_cast<int>(port), probe.value("unit").toString().toStdString()});
    }
    auto schema = json::parse(model.toStdString());
    if (!schema.is_object()) throw std::invalid_argument("Recorded model is not an object.");
    return {0, std::move(schema), dt, static_cast<long>(steps), std::move(probes)};
}
} // namespace s7studio
