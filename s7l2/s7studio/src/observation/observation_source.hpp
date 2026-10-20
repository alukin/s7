#pragma once

#include <QHash>
#include <QStringList>
#include <QTemporaryFile>
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
struct Channel { ChannelId id; QString name; };
struct SourceStatus {
    std::uint64_t samples = 0;
    std::uint64_t deliveryDropped = 0;
    QString storageError;
    [[nodiscard]] bool incomplete() const { return deliveryDropped != 0 || !storageError.isEmpty(); }
};
struct Page {
    QVector<Sample> samples;
    std::uint64_t next = 0;
    QString error;
};

// Window-independent delivered-sample journal. A source belongs to exactly one
// run. Const handles permit bounded history queries, never capture mutations.
// O1a: upstream mailbox loss is still loss; this is not authoritative capture.
class ObservationSource {
public:
    explicit ObservationSource(std::uint64_t runId = 0, qsizetype memoryLimit = 20'000,
                               QString temporaryDirectory = {});
    [[nodiscard]] std::uint64_t runId() const { return runId_; }
    ChannelId channel(const QString& name);
    [[nodiscard]] QVector<Channel> channels() const;
    bool append(const Sample& sample);
    void addDeliveryLoss(std::uint64_t count);
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
    mutable std::mutex mutex_;
    QVector<Channel> channels_;
    QHash<QString, ChannelId> ids_;
    QVector<Sample> memory_;
    mutable std::unique_ptr<QTemporaryFile> file_;
    SourceStatus status_;
};
} // namespace s7studio::observation
