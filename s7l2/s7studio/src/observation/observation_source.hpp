// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QHash>
#include <QStringList>
#include <QIODevice>
#include <QJsonObject>
#include <functional>
#include <QVector>
#include <cstdint>
#include <memory>
#include <mutex>

class QIODevice;

namespace s7studio::observation {
using ChannelId = std::uint32_t;
struct Sample {
    ChannelId channel = 0;
    std::int64_t step = 0;
    double timeSeconds = 0;
    double value = 0;
};
struct Channel { ChannelId id; QString name; QString sourceType; };
struct StorageOptions {
    qsizetype memoryLimit = 20'000;
    QString temporaryDirectory;
    // Optional random-access storage provider (e.g. fault-injecting devices).
    std::function<std::unique_ptr<QIODevice>()> createStorage;
    std::function<bool(QIODevice&)> flushStorage;
};
struct SourceStatus {
    std::uint64_t samples = 0;
    std::uint64_t captureDropped = 0;
    QString storageError;
    QString runError;
    bool finished = false;
    bool stopped = false;
    [[nodiscard]] bool incomplete() const { return captureDropped != 0 || !storageError.isEmpty() || !runError.isEmpty(); }
};
struct Page {
    QVector<Sample> samples;
    std::uint64_t next = 0;
    QString error;
};

// Window-independent delivered-sample journal. A source belongs to exactly one
// run. Const handles permit bounded history queries, never capture mutations.
// The producer decides whether these are authoritative or delivered-only data.
class ObservationSource {
public:
    explicit ObservationSource(std::uint64_t runId = 0, StorageOptions options = {});
    [[nodiscard]] std::uint64_t runId() const { return runId_; }
    // Host publishes exactly once before capture; callers only receive copies.
    void setProvenance(QJsonObject provenance);
    [[nodiscard]] QJsonObject provenance() const;
    ChannelId channel(const QString& name, const QString& sourceType = QStringLiteral("f64"));
    [[nodiscard]] QVector<Channel> channels() const;
    bool append(const Sample& sample);
    void addCaptureLoss(std::uint64_t count);
    // Flush temporary storage and freeze capture; false means incomplete.
    bool finish(bool stopped = false, const QString& runError = {});
    [[nodiscard]] SourceStatus status() const;
    [[nodiscard]] Page read(std::uint64_t cursor, qsizetype limit = 4096) const;
    // Explicit full export, not a repaint operation. Includes run/step/time.
    bool writeCsv(QIODevice& output, const QVector<ChannelId>& selection = {}, QString* error = nullptr) const;
    bool writeAlignedCsv(QIODevice& output, const QVector<ChannelId>& inputs, QString* error = nullptr) const;
private:
    bool spill();
    const std::uint64_t runId_;
    const qsizetype memoryLimit_;
    const QString temporaryDirectory_;
    const std::function<std::unique_ptr<QIODevice>()> createStorage_;
    const std::function<bool(QIODevice&)> flushStorage_;
    mutable std::mutex mutex_;
    QVector<Channel> channels_;
    QHash<QString, ChannelId> ids_;
    QVector<Sample> memory_;
    mutable std::unique_ptr<QIODevice> file_;
    mutable SourceStatus status_;
    QJsonObject provenance_;
    bool provenanceSet_ = false;
};
} // namespace s7studio::observation
