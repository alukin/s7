// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "observation/run_metadata.hpp"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <cmath>
#include <limits>
namespace s7studio::observation {
QString probeUnit(const QJsonObject& provenance, const QString& name) {
    for (const auto& item : provenance.value("probes").toArray()) {
        const auto probe = item.toObject();
        if (probe.value("name").toString() == name) return probe.value("unit").toString();
    }
    return {};
}
bool validateRunProvenance(const QJsonObject& p, QString* error) {
    if (p.isEmpty()) return true;
    const auto fail = [&](const QString& message) { if (error) *error = message; return false; };
    if (p.value("format_version").toInt() != 1 || QUuid(p.value("run_uuid").toString()).isNull()
        || !p.value("effective_model_json").isString() || !p.value("requested_model_json").isString()
        || !p.value("software").isObject() || !p.value("rate").isObject()
        || !p.value("dt_seconds").isDouble() || !p.value("probes").isArray()
        || !p.value("seeds").isArray() || !p.value("initial_conditions").isArray()
        || !p.value("element_versions").isArray())
        return fail(QStringLiteral("Missing or unsupported run provenance fields"));
    bool ok = false;
    const auto steps = p.value("requested_steps").toString().toLongLong(&ok);
    if (!ok || steps < -1) return fail(QStringLiteral("Invalid provenance step count"));
    const auto dt = p.value("dt_seconds").toDouble();
    if (!(dt > 0) || !std::isfinite(dt)) return fail(QStringLiteral("Invalid provenance dt"));
    for (const auto& key : {"numerator", "denominator"}) {
        const auto part = p.value("rate").toObject().value(key).toString().toULongLong(&ok);
        if (!ok || part == 0) return fail(QStringLiteral("Invalid provenance rate"));
    }
    for (const auto& entry : p.value("probes").toArray()) {
        const auto probe = entry.toObject();
        const auto port = probe.value("port").toDouble(-1);
        if ((probe.contains("unit") && !probe.value("unit").isString()) || !probe.value("name").isString() || !probe.value("element").isString()
            || port < 0 || port > std::numeric_limits<int>::max() || std::floor(port) != port)
            return fail(QStringLiteral("Invalid provenance probe"));
    }
    const auto model = p.value("effective_model_json").toString().toUtf8();
    const auto fingerprint = QString::fromLatin1(QCryptographicHash::hash(model, QCryptographicHash::Sha256).toHex());
    if (fingerprint != p.value("model_sha256").toString()) return fail(QStringLiteral("Run model fingerprint mismatch"));
    if (!QJsonDocument::fromJson(model).isObject()
        || !QJsonDocument::fromJson(p.value("requested_model_json").toString().toUtf8()).isObject())
        return fail(QStringLiteral("Invalid provenance model JSON"));
    return true;
}
QString runMetadataSummary(const QJsonObject& observation) {
    const auto p = observation.value("provenance").toObject();
    QStringList lines;
    lines << QStringLiteral("Run: %1").arg(observation.value("run_id").toString());
    lines << QStringLiteral("Recording: %1%2").arg(observation.value("finished").toBool() ?
        (observation.value("stopped").toBool() ? QStringLiteral("stopped") : QStringLiteral("finished")) : QStringLiteral("active or status unavailable"))
        .arg(observation.value("incomplete").toBool() ? QStringLiteral(" (INCOMPLETE)") : QString());
    for (const auto& key : {"storage_error", "run_error"})
        if (!observation.value(key).toString().isEmpty()) lines << observation.value(key).toString();
    if (p.isEmpty()) {
        lines << QStringLiteral("Run provenance is unavailable for this recording.");
        return lines.join('\n');
    }
    const auto rate = p.value("rate").toObject(), software = p.value("software").toObject();
    lines << QStringLiteral("Identity: %1").arg(p.value("run_uuid").toString())
          << QStringLiteral("Model: %1").arg(p.value("model_name").toString())
          << QStringLiteral("Model SHA-256: %1").arg(p.value("model_sha256").toString())
          << QStringLiteral("Prepared: %1").arg(p.value("prepared_utc").toString())
          << QStringLiteral("Rate: %1/%2 Hz; dt: %3 s").arg(rate.value("numerator").toString(), rate.value("denominator").toString())
                .arg(p.value("dt_seconds").toDouble(), 0, 'g', 17)
          << QStringLiteral("Requested steps: %1 (-1 means continuous)").arg(p.value("requested_steps").toString())
          << QStringLiteral("Software: S7 %1, revision %2%3").arg(software.value("project_version").toString(), software.value("revision").toString(),
                software.value("modified_sources").toBool() ? QStringLiteral(" (modified sources)") : QString())
          << QStringLiteral("Environment: %1; Qt %2; %3; %4").arg(software.value("compiler").toString(), software.value("qt_version").toString(), software.value("platform").toString(), software.value("standard_library").toString());
    lines << QStringLiteral("\nInitial conditions (other constructor state is defined by the effective configuration and recorded software):");
    for (const auto& entry : p.value("initial_conditions").toArray()) {
        const auto item = entry.toObject();
        lines << QStringLiteral("  %1 / %2 = %3").arg(item.value("path").toString(), item.value("parameter").toString(), item.value("value_json").toString());
    }
    lines << QStringLiteral("\nRandom seeds:");
    if (p.value("seeds").toArray().isEmpty()) lines << QStringLiteral("  None declared.");
    for (const auto& entry : p.value("seeds").toArray()) {
        const auto item = entry.toObject();
        lines << QStringLiteral("  %1 = %2%3").arg(item.value("path").toString(), item.value("value_json").toString(),
            item.value("generated").toBool() ? QStringLiteral(" (generated before execution)") : QString());
    }
    lines << QStringLiteral("\nElement versions:");
    for (const auto& entry : p.value("element_versions").toArray()) {
        const auto item = entry.toObject();
        lines << QStringLiteral("  %1: %2 %3").arg(item.value("path").toString(), item.value("type").toString(), item.value("version").toString());
    }
    lines << QStringLiteral("\nReproduction: ") + p.value("reproduction_limits").toString();
    return lines.join('\n');
}
}
