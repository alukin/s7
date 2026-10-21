// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "scope_view_window.hpp"
#include "viewers/line_plot_view.hpp"

#include <QtGraphs/QLineSeries>

#include <QApplication>
#include <QBuffer>
#include <QLocale>
#include <QCheckBox>
#include <QMetaObject>

#include <cmath>
#include <stdexcept>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    // Export must preserve doubles regardless of UI locale and escape labels.
    {
        QLocale::setDefault(QLocale(QLocale::German));
        s7studio::ui::ScopeViewWindow scope(QStringLiteral("a,\"b"));
        const double time = 0.12345678901234566;
        const double value = -1.2345678901234567;
        scope.appendSample(QStringLiteral("channel,1"), time, value);
        QBuffer buffer;
        QString error;
        if (scope.writeRecordedCsv(buffer, &error) || error.isEmpty())
            throw std::runtime_error("Unwritable CSV destination accepted");
        buffer.open(QIODevice::ReadWrite);
        if (!scope.writeRecordedCsv(buffer, &error) || !error.isEmpty())
            throw std::runtime_error("CSV export failed");
        const auto row = QString::fromUtf8(buffer.data()).split('\n').at(1);
        if (!row.startsWith(QStringLiteral("\"a,\"\"b\",\"channel,1\",")))
            throw std::runtime_error("CSV labels were not escaped");
        const auto fields = row.split(',');
        if (fields[fields.size()-2].toDouble() != time || fields.back().toDouble() != value)
            throw std::runtime_error("CSV lost double precision or used UI locale");
        QLocale::setDefault(QLocale::c());
    }
    // The example's oscillator is approximately sin(t). Increasing the
    // discretization frequency must retain its shape over the whole run.
    for (const int frequency : {1000, 10000, 100000}) {
        s7studio::ui::ScopeViewWindow scope(QStringLiteral("oscillator"));
        const int count = 10 * frequency;
        for (int i = 0; i < count; ++i) {
            const double time = static_cast<double>(i) / frequency;
            scope.appendSample(time, std::sin(time));
        }
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        auto* plot = scope.plotView();
        if (!plot || plot->series().size() != 1) {
            throw std::runtime_error("Missing oscillator trace");
        }
        const auto data = plot->series().front()->points();
        double previousTime = 0.0;
        for (const auto& point : data) {
            if (point.x() - previousTime > 0.01 || std::abs(point.y() - std::sin(point.x())) > 1e-12) {
                throw std::runtime_error("Oscillator preview lost curve resolution");
            }
            previousTime = point.x();
        }
        if (std::abs(previousTime - static_cast<double>(count - 1) / frequency) > 1e-12
            || data.size() > s7studio::ui::ScopeViewWindow::kMaxInMemorySamplesPerSeries + 1) {
            throw std::runtime_error("Preview endpoint or memory bound incorrect");
        }
        if (plot->xMin() != 0.0 || std::abs(plot->xMax() - previousTime) > 1e-12) {
            throw std::runtime_error("Fit full run did not show the whole run");
        }
        for (auto* check : scope.findChildren<QCheckBox*>()) {
            if (check->text() == QStringLiteral("Fit full run")) check->setChecked(false);
        }
        plot->setXRange(2.0, 3.0);
        scope.appendSample(10.0, std::sin(10.0));
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        if (plot->xMin() != 2.0 || plot->xMax() != 3.0) {
            throw std::runtime_error("Auto Y changed the visible time range");
        }
        // Auto Y fits the visible window only: sin over [2, 3] stays within [0.14, 0.91].
        if (plot->yMin() < 0.1 || plot->yMax() > 1.0) {
            throw std::runtime_error("Auto Y did not fit the visible time window");
        }
        // Wheel zoom about the window center and drag pan move the time window.
        plot->bridge()->zoom(0.5, 0.5, 0.5);
        if (std::abs(plot->xMin() - 2.25) > 1e-12 || std::abs(plot->xMax() - 2.75) > 1e-12) {
            throw std::runtime_error("Zoom did not keep its anchor");
        }
        plot->bridge()->pan(1.0, 0.0);
        if (std::abs(plot->xMin() - 2.75) > 1e-12 || std::abs(plot->xMax() - 3.25) > 1e-12) {
            throw std::runtime_error("Pan did not move the time window");
        }
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        if (std::abs(plot->xMin() - 2.75) > 1e-12) {
            throw std::runtime_error("Refreshing reset a user zoom with Fit full run off");
        }
    }
}
