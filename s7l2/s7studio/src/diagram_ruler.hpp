// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QWidget>

class QGraphicsView;

namespace s7studio::diagram {

class DiagramRuler final : public QWidget
{
    Q_OBJECT

public:
    enum class Axis {
        Horizontal,
        Vertical
    };

    DiagramRuler(QGraphicsView* view, Axis axis, QWidget* parent = nullptr);

    [[nodiscard]] auto sizeHint() const -> QSize override;
    [[nodiscard]] auto minimumSizeHint() const -> QSize override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] auto majorTickStepMm() const -> int;
    [[nodiscard]] auto minorTickStepMm(int majorTickStep) const -> int;
    [[nodiscard]] auto mapSceneMillimetersToLocal(double millimeters) const -> int;
    void drawHorizontal(QPainter& painter);
    void drawVertical(QPainter& painter);

    QGraphicsView* view_ = nullptr;
    Axis axis_ = Axis::Horizontal;
};

} // namespace s7studio::diagram

