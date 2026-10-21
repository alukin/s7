// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "scope_recording.hpp"
#include "viewers/observation_viewer.hpp"

#include <QHash>
#include <QString>
#include <QWidget>

#include <cstdint>
#include <Qt>

class QIODevice;
class QCheckBox;
class QLabel;
class QLineSeries;
class QPushButton;
class QTimer;
class QScrollBar;

namespace s7studio::viewers {
class LinePlotView;
}

namespace s7studio::ui {

// A time-series view for one Studio Scope observer. Each connected observer
// input is a separate plotted channel; observation is outside the runtime graph.
// The recording (ScopeRecording) is drawn with Qt Graphs over model time.
class ScopeViewWindow : public QWidget, public viewers::ObservationViewer
{
    Q_OBJECT

public:
    explicit ScopeViewWindow(QString probeName,
                             QWidget* parent = nullptr,
                             Qt::WindowFlags flags = Qt::WindowFlags());
    ~ScopeViewWindow() override;

    // Registry entry: "Scope", one channel per "<id>/<channel>" probe.
    static auto viewerType() -> viewers::ViewerType;

    [[nodiscard]] auto probeName() const -> QString;

    void appendSample(double timeSeconds, double value);
    void appendSample(const QString& channel, double timeSeconds, double value);
    void clearSamples() override;
    void setComparisonSources(const QVector<viewers::ComparisonBinding>& comparisons) override;
    void beginRun(const QString& previousRunLabel, bool preserve) override;
    // Same full-resolution CSV writer used by the export action. The caller
    // supplies a writable device; finite doubles retain round-trip precision.
    [[nodiscard]] bool writeRecordedCsv(QIODevice& device, QString* error = nullptr) const;
    // A saved result contains the current recording, not comparison overlays.
    [[nodiscard]] bool writeRunResults(QIODevice& device, QString* error = nullptr) const;
    // Transactional: invalid input leaves existing recordings/comparisons intact.
    [[nodiscard]] bool openRunResults(QIODevice& device, const QString& label, QString* error = nullptr);
    void clearComparisons();
    // Samples the runtime produced but the UI dropped because it fell behind.
    void setDroppedSampleCount(std::uint64_t dropped) override;

    // ObservationViewer
    [[nodiscard]] auto widget() -> QWidget* override { return this; }
protected:
    void appendObservedSample(const viewers::ObservedSample& sample) override;
public:

    [[nodiscard]] auto recording() const -> const ScopeRecording& { return recording_; }
    [[nodiscard]] auto plotView() const -> viewers::LinePlotView* { return plot_; }
    // The plotted series of a recording key (see ScopeRecording::series()).
    [[nodiscard]] auto plottedSeries(const QString& key) const -> QLineSeries*;

    static constexpr int kMaxInMemorySamplesPerSeries = ScopeRecording::kMaxInMemorySamplesPerSeries;

protected:
    void showEvent(QShowEvent* event) override;

private slots:
    void clearBuffers();
    void exportRecordedCsv();
    void saveRunResults();
    void openRunResultsFile();
    void showRunDetails();
    void refreshPlot();

private:
    // Creates/removes plotted series so they match the recording's series.
    void syncSeries(bool restyle);
    void setVisibleData();
    void updateStatus();
    void updateTimeScrollBar();
    void scrollToTimePosition(int position);
    void onXRangeChanged();

    QString probeName_;
    ScopeRecording recording_;
    QVector<std::shared_ptr<const observation::ObservationSource>> projectedComparisons_;
    QHash<QString, QLineSeries*> lines_;
    bool dirty_ = false;
    bool updatingTimeScrollBar_ = false;
    bool hasTimeRange_ = false;
    double dataMinTime_ = 0.0;
    double dataMaxTime_ = 0.0;

    viewers::LinePlotView* plot_ = nullptr;
    QCheckBox* autoScaleYCheck_ = nullptr;
    QCheckBox* fitAllTimeCheck_ = nullptr;
    QScrollBar* timeScrollBar_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui
