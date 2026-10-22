// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/spectrum_worker.hpp"
#include <QWidget>
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QTimer;
namespace s7studio::viewers {
class LinePlotView;
class SpectrumView : public QWidget {
    Q_OBJECT
public:
    explicit SpectrumView(QWidget* parent = nullptr);
    ~SpectrumView() override;
    void setFrame(std::shared_ptr<const observation::LiveFrame> frame);
    void clear();
    void setSuspended(bool suspended);
    std::uint64_t analysisRequestCount() const { return submitted_; }
    std::shared_ptr<const observation::LiveFrame> setFrozen(bool frozen);
    const std::optional<analysis::SpectrumResult>& result() const { return result_; }
    LinePlotView* plotView() const { return plot_; }
    QString inspect(double frequency, bool active = true);
signals:
    void inspectingChanged(bool active);
protected:
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    analysis::SpectrumSettings settings() const;
    void reconfigure();
    void submit();
    void poll();
    void render(bool fit);
    void updateStatus();
    QString quantityUnit() const;
    QString binText(std::size_t index) const;
    analysis::SpectrumWorker worker_;
    std::shared_ptr<const observation::LiveFrame> frame_;
    std::optional<analysis::SpectrumResult> result_;
    std::uint64_t revision_ = 1, submitted_ = 0;
    QTimer* pollTimer_;
    bool suspended_ = false;
    bool inspecting_ = false, frozen_ = false, rendering_ = false;
    QComboBox *mode_, *channel_, *quadrature_, *size_, *window_, *quantity_, *scale_, *average_, *marker_;
    QSpinBox *count_, *fixedBin_;
    QDoubleSpinBox *alpha_, *reference_, *dbOffset_, *floor_, *frequencyOffset_;
    QCheckBox *removeMean_, *peakHold_;
    QLineEdit* declaredUnit_;
    QLabel *status_, *readout_, *markerText_;
    LinePlotView* plot_;
};
}
