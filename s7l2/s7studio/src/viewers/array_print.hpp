// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QStringList>

#include <functional>

class QPainter;
class QPrinter;
class QRect;

namespace s7studio::viewers {

struct ArrayPrintSnapshot {
    QString title;
    QStringList details;
    QStringList rows;
    std::function<void(QPainter&, const QRect&)> draw;
};

[[nodiscard]] QString saveArrayPdf(const ArrayPrintSnapshot& snapshot, const QString& path);
[[nodiscard]] QString printArraySnapshot(const ArrayPrintSnapshot& snapshot, QPrinter& printer);

} // namespace s7studio::viewers
