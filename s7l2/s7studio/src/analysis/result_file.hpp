// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <QJsonObject>
#include <QString>
#include <complex>
#include <vector>
namespace s7studio::analysis {
struct ResultFile {
    QString kind;
    QJsonObject metadata;
    std::vector<double> x, y, input;
    std::vector<std::complex<double>> values;
    std::vector<bool> valid;
    void validate() const;
    void save(const QString &path) const;
    static ResultFile open(const QString &path);
    void csv(const QString &path) const;
};
} // namespace s7studio::analysis
