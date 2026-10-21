#pragma once
#include <QJsonObject>
#include <QString>
namespace s7studio::observation {
// Empty provenance is explicitly unavailable (standalone/imported data).
[[nodiscard]] bool validateRunProvenance(const QJsonObject& provenance, QString* error = nullptr);
[[nodiscard]] QString runMetadataSummary(const QJsonObject& observation);
}
