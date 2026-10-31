// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/time_view_worker.hpp"
#include "viewers/observation_viewer.hpp"
#include <QElapsedTimer>
#include <QVariantList>
#include <QWidget>
#include <tuple>
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
class QStackedWidget;
class QToolButton;
class QProgressBar;
class QSlider;
namespace s7studio::viewers { class LinePlotView; class SpectrumView; class PairAnalysisView; class WindowInspectionPanel; }
namespace s7studio::ui {
class ChoiceButtons;
class Throttle;
class LiveOscilloscopeWindow : public QWidget, public viewers::ObservationViewer, public viewers::LiveObservationViewer {
    Q_OBJECT
public:
    explicit LiveOscilloscopeWindow(QString id);
    ~LiveOscilloscopeWindow() override;
    static viewers::ViewerType viewerType();
    QWidget* widget() override { return this; }
    void attachLiveSource(std::shared_ptr<observation::LiveAcquisition> source) override;
    bool refreshSource() override;
    void clearSamples() override;
    void setDroppedSampleCount(std::uint64_t) override {}
    void beginRun(const QString&, bool) override { attachLiveSource({}); }
    viewers::LinePlotView* plotView() const { return plot_; }
    const observation::LiveFrame& displayedFrame() const { return *frame_; }
    QString inspect(double time, bool active = true);
    // Markers on the samples of traces drawn within a few pixels of the
    // pointer at (x, y) in plot coordinates; empty when no trace is near.
    QVariantList hoverMarkers(double x, double y, bool active = true);
    // Applies saved trigger settings, e.g. a diagram preset, without
    // reporting them back through triggerSettingsEdited().
    void setTriggerSettings(const observation::TriggerSettings& settings) override;
    const observation::TriggerSettings& triggerSettings() const { return requestedTrigger_; }
    void setTriggerController(std::function<bool(const observation::TriggerSettings&, QString*)> controller) override { triggerController_ = std::move(controller); }
signals:
    // The user changed and applied trigger settings in this window.
    void triggerSettingsEdited();
protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    void appendObservedSample(const viewers::ObservedSample&) override {}
    void render(bool fit);
    analysis::TimeViewSettings timeViewSettings(bool prepare, bool fit) const;
    void presentTimeResult(const analysis::TimeViewResult& result);
    void updateStatus(const QString& suffix = {});
    void applyTriggerSettings();
    bool commitTriggerSettings(const observation::TriggerSettings& settings);
    void updateVerticalReadouts();
    void updateInstrumentMarkers();
    std::shared_ptr<observation::LiveAcquisition> live_;
    std::shared_ptr<const observation::LiveFrame> frame_ = std::make_shared<observation::LiveFrame>();
    viewers::LinePlotView* plot_ = nullptr;
    viewers::SpectrumView* spectrum_;
    viewers::PairAnalysisView *constellation_, *correlation_;
    viewers::WindowInspectionPanel* inspection_;
    QLabel *status_, *readout_, *duration_, *timeRange_;
    QPushButton* freeze_;
    QSpinBox* capacity_;
    QLineEdit* seconds_;
    QCheckBox* autoY_;
    QDoubleSpinBox* timeScale_;
    double effectiveTimeScale_ = 2;
    std::shared_ptr<const observation::LiveFrame> rawFrame_, timeFrame_;
    QComboBox *triggerMode_, *triggerChannel_;
    ChoiceButtons* triggerEdge_;
    QLineEdit *triggerLevel_, *triggerHysteresis_;
    QSpinBox *triggerPre_, *triggerHoldoff_, *triggerTimeout_;
    QSlider* triggerPreSlider_;
    QLabel* triggerStatus_;
    observation::TriggerSettings requestedTrigger_;
    std::uint64_t markerSourceRevision_ = 0, markerTriggerRevision_ = 0;
    std::function<bool(const observation::TriggerSettings&, QString*)> triggerController_;
    analysis::TimeViewWorker timeWorker_;
    std::uint64_t timeRequestRevision_ = 0, requestedCaptureId_ = 0;
    bool requestedCaptureFinished_ = false;
    QStackedWidget* channelPages_;
    QProgressBar* timeBusy_;
    struct ChannelControls {
        QCheckBox* visible;
        QDoubleSpinBox *gain, *offset;
        QToolButton* select;
        QDoubleSpinBox* scale;
        double scaleDivision = 0, scaleGain = 0, committedScale = 0;
    };
    QVector<ChannelControls> controls_;
    bool inspecting_ = false, rendering_ = false;
    std::optional<std::size_t> requestedCapacity_;
    QString traceWarning_, settingsError_, triggerError_;
    QElapsedTimer cadence_;
    Throttle* statusRefresh_ = nullptr;  // status after data refreshes at 10 Hz; actions update at once
    std::tuple<std::uint64_t, bool, std::uint64_t, bool, bool> statusShown_{};  // displayed frame identity last put in the status
};
}
