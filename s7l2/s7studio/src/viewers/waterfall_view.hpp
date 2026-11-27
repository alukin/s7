// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "viewers/waterfall_history.hpp"

#include <QWidget>

class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace s7studio::viewers {

class WaterfallView final : public QWidget {
public:
    explicit WaterfallView(QWidget* parent);
    [[nodiscard]] QString present(const s7::CommittedArraySample& sample);
    void clear();

private:
    class Plot;
    void updateScale();
    void renderHistory();

    WaterfallHistory history_;
    WaterfallScale scale_;
    Plot* plot_;
    QDoubleSpinBox* minimum_;
    QDoubleSpinBox* maximum_;
    QDoubleSpinBox* reference_;
    QComboBox* palette_;
    QLabel* scaleStatus_;
};

} // namespace s7studio::viewers
