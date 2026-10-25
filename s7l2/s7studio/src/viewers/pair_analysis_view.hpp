// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/pair_analysis.hpp"
#include "analysis/window_inspection.hpp"
#include <QWidget>
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLabel;
class QTimer;
class QProgressBar;
class QScatterSeries;
namespace s7studio::viewers {
class LinePlotView;
class PairAnalysisView : public QWidget {
    Q_OBJECT
public:
    explicit PairAnalysisView(analysis::PairKind kind, QWidget* parent = nullptr);
    ~PairAnalysisView() override;
    void setFrame(std::shared_ptr<const observation::LiveFrame> frame);
    void clear();
    void setSuspended(bool suspended);
    std::uint64_t analysisRequestCount() const { return submitted_; }
    std::shared_ptr<const observation::LiveFrame> setFrozen(bool frozen);
    const std::optional<analysis::PairResult>& result() const { return result_; }
    LinePlotView* plotView() const { return plot_; }
    QString inspect(double x, double y, bool active = true);
    std::optional<analysis::WindowSnapshot> inspectionSnapshot() const;
signals:
    void inspectingChanged(bool active);
    void inspectionInvalidated();
protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void resizeEvent(QResizeEvent*) override;
private:
    analysis::PairSettings settings() const;
    void reconfigure();
    void submit();
    void poll();
    void render(bool fit);
    void updateStatus();
    QString lagText(std::size_t index) const;
    analysis::PairKind kind_;
    analysis::PairWorker worker_;
    std::shared_ptr<const observation::LiveFrame> frame_;
    std::optional<analysis::PairResult> result_;
    std::uint64_t revision_ = 1, submitted_ = 0;
    QTimer* pollTimer_;
    bool suspended_ = false;
    bool inspecting_ = false, frozen_ = false, rendering_ = false, resizePending_ = false;
    QString displayWarning_;
    QComboBox *first_, *second_, *mode_, *size_, *normalization_, *density_, *lagUnits_;
    QSpinBox *persistence_, *maxLag_;
    QCheckBox *equalAspect_, *removeMean_, *absolutePeak_;
    QLabel *status_, *readout_, *marker_;
    QProgressBar* busy_;
    LinePlotView* plot_;
    QScatterSeries *dots_, *newest_, *selected_;
};
}
