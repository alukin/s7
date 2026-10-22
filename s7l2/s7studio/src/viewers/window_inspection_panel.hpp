// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/window_inspection.hpp"
#include <QWidget>
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTimer;
namespace s7studio::viewers {
class LinePlotView;
class WindowInspectionPanel : public QWidget {
    Q_OBJECT
public:
    using Capture = std::function<std::optional<analysis::WindowSnapshot>()>;
    explicit WindowInspectionPanel(Capture capture, QWidget* parent = nullptr);
    void setPlot(LinePlotView* plot);
    void clear(bool cancelExport = false);
    bool pin();
    void pick(double x, double y);
    bool exportTo(const QString& path, analysis::WindowExportFormat format);
    void cancelExport();
    const std::shared_ptr<const analysis::WindowSnapshot>& snapshot() const { return snapshot_; }
    QString readout() const;
signals:
    void pinned();
    void exportFinished(bool success, bool cancelled, const QString& error);
private:
    void updateReadout();
    Capture capture_;
    LinePlotView* plot_ = nullptr;
    std::shared_ptr<const analysis::WindowSnapshot> snapshot_;
    analysis::WindowExportWorker exporter_;
    QSpinBox *a_, *b_;
    QComboBox *active_, *format_;
    QPlainTextEdit* readout_;
    QLabel* status_;
    QPushButton *export_, *cancel_;
    QTimer* timer_;
    bool measured_ = false;
};
}
