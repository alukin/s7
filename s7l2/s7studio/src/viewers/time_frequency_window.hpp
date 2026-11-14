// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/result_file.hpp"
#include "observation_viewer.hpp"
#include <QWidget>
#include <deque>
#include <memory>
#include <optional>
class QLabel;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QScatterSeries;
class QPushButton;
namespace s7studio::viewers {
class SurfacePlotView;
class LinePlotView;
class TimeFrequencyWindow final : public QWidget, public ObservationViewer {
    Q_OBJECT
  public:
    explicit TimeFrequencyWindow(const QString &name = {}, QWidget *parent = nullptr);
    ~TimeFrequencyWindow() override;
    static ViewerType viewerType();
    QWidget *widget() override { return this; }
    void clearSamples() override;
    void setDroppedSampleCount(std::uint64_t) override;
    void appendObservedSample(const ObservedSample &) override;
    bool refreshSource() override;
    void analyze();
    void poll();
    bool hasResult() const { return bool(result_); }
    bool isPreparingDisplay() const;
    const analysis::ResultFile *result() const { return result_.get(); }
    QString error() const { return error_; }
    void setWavelet(bool enabled);
    void saveResult(const QString &path) const;
    void openResult(const QString &path);

  protected:
    void hideEvent(QHideEvent *) override;
    void showEvent(QShowEvent *) override;

  private:
    void present();
    void submitDisplay();
    void applyDisplay();
    struct DisplayWork;
    std::unique_ptr<DisplayWork> displayWork_;
    void inspect(int row, int column);
    void chooseInspection(double time, std::optional<double> frequency = {});
    void updateStatus();
    void startAnalysis(bool currentCapture);
    bool smallAutomaticJob() const;
    struct Work;
    std::shared_ptr<Work> work_;
    std::shared_ptr<const analysis::ResultFile> result_;
    struct Sample {
        std::int64_t step;
        double time, value;
    };
    std::deque<Sample> samples_;
    std::int64_t previous_ = 0;
    bool loaded_ = false, imported_ = false;
    bool pending_ = false;
    bool polling_ = false;
    bool requestActive_ = false;
    bool currentCaptureRequested_ = false;
    bool automaticAttempted_ = false;
    std::weak_ptr<const observation::ObservationSource> automaticSource_;
    QString error_, unit_;
    QLabel *status_;
    QLabel *inspection_;
    QLabel *empty_;
    QPushButton *analyze_;
    QComboBox *method_;
    QComboBox *quantity_;
    QComboBox *window_;
    QSpinBox *scales_;
    QDoubleSpinBox *minimum_;
    QDoubleSpinBox *maximum_;
    QDoubleSpinBox *displayMaximum_;
    QCheckBox *follow_;
    QCheckBox *logFrequency_;
    QSpinBox *row_;
    QSpinBox *column_;
    SurfacePlotView *plot_;
    LinePlotView *trace_;
    QScatterSeries *traceMarker_ = nullptr;
    LinePlotView *slice_;
    std::uint64_t lastSubmitted_ = 0;
    std::size_t bandRows_ = 0;
};
} // namespace s7studio::viewers
