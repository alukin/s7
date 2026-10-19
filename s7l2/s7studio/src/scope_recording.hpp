#pragma once

#include <QList>
#include <QMap>
#include <QPointF>
#include <QSharedPointer>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>

class QIODevice;
class QTemporaryFile;

namespace s7studio::ui {

// The data side of a Scope: one series per channel of the current run plus
// imported comparison runs. Keeps a bounded in-memory preview per series and
// spills the full-resolution recording to a temporary file once the preview
// is full, so long runs keep every sample without growing memory. Saving,
// opening and CSV export read the full recording. No UI: ScopeViewWindow
// draws it, and a run import stages into a separate ScopeRecording.
class ScopeRecording
{
public:
    struct Series {
        struct SpillIndexEntry {
            double timeSeconds = 0.0;
            qint64 byteOffset = 0;
        };
        QString channelName;
        QString comparisonLabel;
        QString recordingWarning;
        QVector<double> time;
        QVector<double> value;
        QVector<SpillIndexEntry> spillIndex;
        QSharedPointer<QTemporaryFile> spillFile;
        qint64 spilledSamples = 0;
        std::uint64_t receivedSamples = 0;
        std::uint64_t plotStride = 1;
        double lastValue = 0.0;
        double firstTime = 0.0;
        double lastTime = 0.0;
        bool hasSamples = false;
        // Set once spilling to disk fails; stops retrying it on every sample
        // and marks that decimation below is now discarding full-resolution
        // data rather than trimming a disk-backed copy.
        bool spillFailed = false;

        [[nodiscard]] auto isComparison() const -> bool { return !comparisonLabel.isEmpty(); }
        [[nodiscard]] auto displayName() const -> QString;
    };

    static constexpr int kMaxInMemorySamplesPerSeries = 20'000;

    explicit ScopeRecording(QString scopeName);

    [[nodiscard]] auto scopeName() const -> QString { return scopeName_; }
    // Keyed "live:<channel>" or "comparison:<id>:<channel>"; map order is
    // the drawing order.
    [[nodiscard]] auto series() const -> const QMap<QString, Series>& { return series_; }

    void appendSample(const QString& channel, double timeSeconds, double value);
    // Drops the current run (live series), its loss count and storage error;
    // comparisons stay.
    void clearLive();
    void clearComparisons();
    void setDroppedSampleCount(std::uint64_t dropped);
    [[nodiscard]] auto droppedSampleCount() const -> std::uint64_t { return droppedSamples_; }

    // The model-time span covered by all series, if any has samples.
    [[nodiscard]] auto timeRange(double& minTime, double& maxTime) const -> bool;
    // Points of one series to draw for the visible window [lower, upper]:
    // the bounded preview for a full view, or full resolution read back from
    // the spill file for a zoomed view of a disk-backed series.
    [[nodiscard]] auto visiblePoints(const QString& key, double lower, double upper, bool fitFullRun) const
        -> QList<QPointF>;

    // Same full-resolution CSV writer used by the export action. The caller
    // supplies a writable device; finite doubles retain round-trip precision.
    [[nodiscard]] bool writeRecordedCsv(QIODevice& device, QString* error = nullptr) const;
    // A saved result contains the current recording, not comparison overlays.
    [[nodiscard]] bool writeRunResults(QIODevice& device, QString* error = nullptr) const;
    // Transactional: invalid input leaves existing recordings/comparisons intact.
    [[nodiscard]] bool openRunResults(QIODevice& device, const QString& label, QString* error = nullptr);

    // Capture loss, storage failure or an incomplete comparison: export
    // would hold only the retained samples.
    [[nodiscard]] auto exportMayBeIncomplete() const -> bool;
    [[nodiscard]] auto statusText() const -> QString;
    // Called whenever statusText() may have changed.
    void setStatusChangedHandler(std::function<void()> handler) { statusChanged_ = std::move(handler); }

private:
    [[nodiscard]] bool visitSamples(const Series& series,
        const std::function<void(double, double)>& visitor, QString* error) const;
    auto spillSeries(Series& series) -> bool;
    auto appendToSpill(Series& series, double timeSeconds, double value) -> bool;
    void decimatePlotBuffer(Series& series);
    void notifyStatus() const;

    QString scopeName_;
    std::uint64_t droppedSamples_ = 0;
    QString temporaryDirectory_;
    QString temporaryWriteError_;
    QMap<QString, Series> series_;
    std::uint64_t comparisonSequence_ = 0;
    std::function<void()> statusChanged_;
};

} // namespace s7studio::ui
