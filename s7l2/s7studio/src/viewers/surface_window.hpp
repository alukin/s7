// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "observation/step_aligner.hpp"
#include "observation_viewer.hpp"
#include "surface_plot.hpp"
class QLabel;
namespace s7studio::viewers {
class SurfaceWindow final : public QWidget, public ObservationViewer {
    Q_OBJECT
  public:
    explicit SurfaceWindow(bool complex, const QString &name = {}, QWidget *parent = nullptr);
    static ViewerType viewerType(bool complex);
    static QStringList roles(bool complex);
    QWidget *widget() override { return this; }
    void clearSamples() override;
    void setDroppedSampleCount(std::uint64_t count) override;
    void appendObservedSample(const ObservedSample &sample) override;
    const analysis::SurfaceGrid &grid() const { return grid_; }
    SurfacePlotView *plot() const { return plot_; }
    QString error() const { return error_; }
    void refreshPlot();
    bool refreshSource() override;
    void saveResult(const QString &path);
    void openResult(const QString &path);

  private:
    void updateStatus();
    void exportCsv();
    bool complex_;
    QStringList roles_, units_;
    analysis::SurfaceGrid grid_;
    observation::StepAligner<8> aligner_;
    SurfacePlotView *plot_;
    QLabel *status_;
    QString error_;
    std::uint64_t dropped_ = 0;
    bool dirty_ = false;
    bool imported_ = false;
    QJsonObject importedMetadata_;
};
} // namespace s7studio::viewers
