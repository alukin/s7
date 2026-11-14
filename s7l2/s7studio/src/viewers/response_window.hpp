// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "observation/step_aligner.hpp"
#include "observation_viewer.hpp"
#include "trajectory_geometry.hpp"
#include <QWidget>
#include <array>
#include <memory>
#include <vector>
class QLabel;
class QPushButton;
class QDoubleSpinBox;
class QScatterSeries;
class QCheckBox;
namespace s7spectral {
struct Response;
}
namespace s7studio::ui {
class TrajectoryPlotView;
}
namespace s7studio::viewers {
class LinePlotView;
class SurfacePlotView;
class ResponseWindow final : public QWidget, public ObservationViewer {
    Q_OBJECT
  public:
    explicit ResponseWindow(const QString &name = {}, QWidget *parent = nullptr);
    ~ResponseWindow() override;
    static ViewerType viewerType();
    QWidget *widget() override { return this; }
    void clearSamples() override;
    void setDroppedSampleCount(std::uint64_t) override;
    void appendObservedSample(const ObservedSample &) override;
    bool refreshSource() override;
    void analyze();
    void poll();
    bool hasResult() const;
    void saveResult(const QString &path) const;
    void openResult(const QString &path);
    QString error() const { return error_; }

  private:
    void draw();
    void inspect(double frequency);
    void exportResult();
    void updateStatus();
    struct Work;
    std::shared_ptr<Work> work_;
    std::shared_ptr<const s7spectral::Response> result_;
    observation::StepAligner<3> aligner_;
    std::vector<std::array<double, 3>> samples_;
    std::int64_t previousStep_ = 0;
    bool loaded_ = false;
    bool imported_ = false;
    bool automaticAttempted_ = false;
    std::weak_ptr<const observation::ObservationSource> automaticSource_;
    QJsonObject importedMetadata_;
    bool phaseTruncated_ = false;
    QString error_, inputUnit_, outputUnit_;
    QLabel *status_;
    QLabel *inspection_;
    QPushButton *analyze_;
    QDoubleSpinBox *reference_;
    QDoubleSpinBox *frequency_;
    std::array<LinePlotView *, 4> plots_{};
    std::array<QScatterSeries *, 4> markers_{};
    ui::TrajectoryPlotView *trajectory_;
    TrajectoryBounds trajectoryBounds_;
    SurfacePlotView *landscape_;
    QCheckBox *referenceEnabled_;
    QCheckBox *unwrap_;
    QDoubleSpinBox *omega_;
    QDoubleSpinBox *damping_;
    QDoubleSpinBox *modelGain_;
    std::vector<double> phases_;
};
} // namespace s7studio::viewers
