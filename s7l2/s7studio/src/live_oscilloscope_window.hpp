// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/time_view_worker.hpp"
#include "viewers/observation_viewer.hpp"
#include <QElapsedTimer>
#include <QVariantList>
#include <QWidget>
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
class LiveOscilloscopeWindow : public QWidget, public viewers::ObservationViewer {
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
    // Tooltip rows for traces drawn within a few pixels of the pointer at
    // (x, y) in plot coordinates; empty when no trace is near.
    QVariantList hoverTip(double x, double y, bool active = true);
    // Applies saved trigger settings, e.g. a diagram preset, without
    // reporting them back through triggerSettingsEdited().
    void setTriggerSettings(const observation::TriggerSettings& settings);
    const observation::TriggerSettings& triggerSettings() const { return requestedTrigger_; }
    void setTriggerController(std::function<bool(const observation::TriggerSettings&, QString*)> controller) { triggerController_ = std::move(controller); }
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
    std::shared_ptr<observation::LiveAcquisition> live_;
    std::shared_ptr<const observation::LiveFrame> frame_ = std::make_shared<observation::LiveFrame>();
    viewers::LinePlotView* plot_;
    viewers::SpectrumView* spectrum_;
    viewers::PairAnalysisView *constellation_, *correlation_;
    viewers::WindowInspectionPanel* inspection_;
    QLabel *status_, *readout_, *duration_, *timeRange_;
    QPushButton* freeze_;
    QSpinBox* capacity_;
    QLineEdit* seconds_;
    QCheckBox* autoY_;
    QDoubleSpinBox* timeScale_;
    bool timeScaleEdited_ = false;
    std::shared_ptr<const observation::LiveFrame> rawFrame_, timeFrame_;
    QComboBox *triggerMode_, *triggerChannel_;
    ChoiceButtons* triggerEdge_;
    QLineEdit *triggerLevel_, *triggerHysteresis_;
    QSpinBox *triggerPre_, *triggerHoldoff_, *triggerTimeout_;
    QSlider* triggerPreSlider_;
    QLabel* triggerStatus_;
    observation::TriggerSettings requestedTrigger_;
    std::function<bool(const observation::TriggerSettings&, QString*)> triggerController_;
    analysis::TimeViewWorker timeWorker_;
    std::uint64_t timeRequestRevision_ = 0, requestedCaptureId_ = 0;
    bool requestedCaptureFinished_ = false;
    QStackedWidget* channelPages_;
    QProgressBar* timeBusy_;
    struct ChannelControls { QCheckBox* visible; QDoubleSpinBox *gain, *offset; QToolButton* select; };
    QVector<ChannelControls> controls_;
    bool inspecting_ = false, rendering_ = false;
    std::optional<std::size_t> requestedCapacity_;
    QString traceWarning_, settingsError_, triggerError_;
    QElapsedTimer cadence_;
};
}
