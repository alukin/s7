// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "viewers/observation_viewer.hpp"
#include <QElapsedTimer>
#include <QWidget>
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QCheckBox;
class QDoubleSpinBox;
class QComboBox;
namespace s7studio::viewers { class LinePlotView; class SpectrumView; class PairAnalysisView; class WindowInspectionPanel; }
namespace s7studio::ui {
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
protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
private:
    void appendObservedSample(const viewers::ObservedSample&) override {}
    void render(bool fit);
    void updateStatus(const QString& suffix = {});
    void applyTriggerSettings();
    std::shared_ptr<observation::LiveAcquisition> live_;
    std::shared_ptr<const observation::LiveFrame> frame_ = std::make_shared<observation::LiveFrame>();
    viewers::LinePlotView* plot_;
    viewers::SpectrumView* spectrum_;
    viewers::PairAnalysisView *constellation_, *correlation_;
    viewers::WindowInspectionPanel* inspection_;
    QLabel *status_, *readout_, *duration_;
    QPushButton* freeze_;
    QSpinBox* capacity_;
    QLineEdit* seconds_;
    QCheckBox* autoY_;
    QComboBox *triggerMode_, *triggerChannel_, *triggerEdge_;
    QLineEdit *triggerLevel_, *triggerHysteresis_;
    QSpinBox *triggerPre_, *triggerHoldoff_, *triggerTimeout_;
    QLabel* triggerStatus_;
    observation::TriggerSettings requestedTrigger_;
    struct ChannelControls { QCheckBox* visible; QDoubleSpinBox *gain, *offset; };
    QVector<ChannelControls> controls_;
    bool inspecting_ = false, rendering_ = false;
    std::optional<std::size_t> requestedCapacity_;
    QString traceWarning_, settingsError_, triggerError_;
    QElapsedTimer cadence_;
};
}
