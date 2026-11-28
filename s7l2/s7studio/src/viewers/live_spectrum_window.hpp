// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/live_spectrum.hpp"
#include "viewers/observation_viewer.hpp"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QRadioButton;
class QSpinBox;
class QTimer;

namespace s7studio::viewers {

// Display state shared by the window and its canvas.
struct LiveSpectrumDisplay {
    enum class Style { Bars, Curve };
    Style style = Style::Bars;
    double floorDb = -90, ceilingDb = 0;
    double centreHz = 0;  // added to frequency labels (an SDR's tuned frequency)
    bool iq = false;
    bool logFrequency = true;  // log x axis (real input only)
    analysis::LiveSpectrumResult spectrum;
    std::vector<double> bars, barPeaks, barPeakAge;  // smoothed bar levels and peak caps (dB, s)
    std::vector<double> curve, curvePeak;            // smoothed per-bin trace and its decaying peak (dB)
    QString message = QStringLiteral("Waiting for live samples");
};

// A simple live spectrum observer in the style of hardware audio analyzers:
// LED bar columns with falling peak caps, or a filled spectrum curve with a
// decaying peak trace for SDR I/Q. It reads the newest samples of its live
// source and runs a small FFT in place on a timer; nothing is recorded.
class LiveSpectrumWindow : public QWidget, public ObservationViewer, public LiveObservationViewer {
    Q_OBJECT
public:
    explicit LiveSpectrumWindow(QString id);
    ~LiveSpectrumWindow() override;
    static ViewerType viewerType();
    QWidget* widget() override { return this; }

    void attachLiveSource(std::shared_ptr<observation::LiveAcquisition> source) override;
    void setTriggerSettings(const observation::TriggerSettings&) override {}
    void setTriggerController(std::function<bool(const observation::TriggerSettings&, QString*)>) override {}
    bool refreshSource() override { return false; }
    void clearSamples() override;
    void setDroppedSampleCount(std::uint64_t) override {}
    void beginRun(const QString&, bool) override { attachLiveSource({}); }

    // Saved observer parameters: bars, style ("bars"/"curve"), frequency_scale
    // ("auto"/"log"/"linear"), fft_size, floor_db, input ("auto"/"real"/"iq")
    // and centre_hz.
    void applyParameters(const QJsonObject& parameters);
    // One refresh from the live source (the timer calls it every 50 ms).
    void updateSpectrum();
    [[nodiscard]] const LiveSpectrumDisplay& display() const { return display_; }
    [[nodiscard]] analysis::LiveSpectrumSettings settings() const;

private:
    void appendObservedSample(const ObservedSample&) override {}
    void resetDisplay();
    [[nodiscard]] bool iqInput() const;

    QString id_;
    std::shared_ptr<observation::LiveAcquisition> source_;
    analysis::LiveSpectrum analyzer_;
    LiveSpectrumDisplay display_;
    QString input_ = QStringLiteral("auto");
    QWidget* canvas_ = nullptr;
    QButtonGroup* style_ = nullptr;
    QRadioButton* barsStyle_ = nullptr;
    QRadioButton* curveStyle_ = nullptr;
    QButtonGroup* scale_ = nullptr;
    QRadioButton* logScale_ = nullptr;
    QRadioButton* linearScale_ = nullptr;
    QString scaleChoice_ = QStringLiteral("auto");
    QSpinBox* bars_ = nullptr;
    QComboBox* fftSize_ = nullptr;
    QSpinBox* floor_ = nullptr;
    QCheckBox* peakHold_ = nullptr;
    QLabel* status_ = nullptr;
    QTimer* timer_ = nullptr;
    QElapsedTimer clock_;
};

} // namespace s7studio::viewers
