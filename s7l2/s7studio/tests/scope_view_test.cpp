// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "scope_view_window.hpp"
#include "viewers/line_plot_view.hpp"

#include <QtGraphs/QLineSeries>

#include <QApplication>
#include <QBuffer>
#include <QLocale>
#include <QCheckBox>
#include <QLabel>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <limits>
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
    // Hover must read the exact disk-backed recording, including a sample
    // omitted from the bounded preview, and clear the readout on exit.
    {
        s7studio::ui::ScopeViewWindow scope(QStringLiteral("hover"));
        for (int i = 0; i < 30'000; ++i)
            scope.appendSample(i * .001, i == 20'001 ? 7.0 : 0.0);
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        auto* plot = scope.plotView();
        const auto preview = plot->series().front()->points();
        if (std::any_of(preview.cbegin(), preview.cend(),
                [](const auto& point) { return point.y() == 7.0; }))
            throw std::runtime_error("Hover fixture peak was not omitted from the preview");
        plot->setXRange(19.99, 20.01);
        plot->setYRange(-1.0, 8.0);
        plot->bridge()->plotAreaSize = QSizeF(400, 200);
        emit plot->hoveredPoint(20.001, 7.0, true);
        const auto rows = plot->bridge()->hoverTip;
        const auto markers = plot->bridge()->hoverMarkers;
        auto* readout = scope.findChild<QLabel*>(QStringLiteral("scope.hoverReadout"));
        if (rows.size() != 1 || !rows.front().toMap().value("text").toString().contains(QStringLiteral("= 7"))
            || markers.size() != 1 || markers.front().toMap().value("y").toDouble() != 7.0
            || !readout || !readout->text().contains(QStringLiteral("= 7")) || plot->bridge()->hoverTipEnabled
            || !plot->bridge()->cursorVisible || std::abs(plot->bridge()->cursorX - 20.001) > 1e-12)
            throw std::runtime_error("Scope hover did not show the exact stored sample");
        emit plot->hoveredPoint(20.001, 7.0, false);
        if (!plot->bridge()->hoverTip.isEmpty() || !plot->bridge()->hoverMarkers.isEmpty() || !readout->text().isEmpty()
            || plot->bridge()->cursorVisible)
            throw std::runtime_error("Scope hover did not clear on pointer exit");
    }
    // Sparse line segments can be hovered between stored points; a NaN
    // sample breaks that segment and must not create a false readout.
    {
        s7studio::ui::ScopeViewWindow scope(QStringLiteral("sparse"));
        scope.appendSample(0.0, 0.0);
        scope.appendSample(1.0, 1.0);
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        auto* plot = scope.plotView();
        plot->setXRange(0.0, 1.0);
        plot->setYRange(0.0, 1.0);
        plot->bridge()->plotAreaSize = QSizeF(400, 200);
        emit plot->hoveredPoint(.5, .5, true);
        if (plot->bridge()->hoverTip.size() != 1 || plot->bridge()->hoverMarkers.size() != 1)
            throw std::runtime_error("Scope hover missed a sparse line segment");
        scope.clearSamples();
        scope.appendSample(0.0, 0.0);
        scope.appendSample(.5, std::numeric_limits<double>::quiet_NaN());
        scope.appendSample(1.0, 1.0);
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        plot->setXRange(0.0, 1.0);
        plot->setYRange(0.0, 1.0);
        emit plot->hoveredPoint(.25, .25, true);
        if (!plot->bridge()->hoverTip.isEmpty())
            throw std::runtime_error("Scope hover crossed an explicit trace gap");
    }
    // Display reduction must retain an isolated peak and a broken trace;
    // the independent recording/export still has every source sample.
    {
        s7studio::ui::ScopeViewWindow scope(QStringLiteral("dense"));
        constexpr int count = 30'000;
        for (int i = 0; i < count; ++i) {
            const double value = i == 12'000 ? 17.0
                : i == 18'000 ? std::numeric_limits<double>::quiet_NaN() : std::sin(i * .01);
            scope.appendSample(i * .001, value);
        }
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        const auto points = scope.plotView()->series().front()->points();
        if (points.size() > 4096 || points.front().x() != 0.0
            || std::abs(points.back().x() - 29.999) > 1e-12
            || !std::any_of(points.cbegin(), points.cend(), [](const auto& point) { return point.y() == 17.0; })
            || !std::any_of(points.cbegin(), points.cend(), [](const auto& point) { return std::isnan(point.y()); }))
            throw std::runtime_error("Dense Scope display lost its bound, endpoint, peak or gap");
        auto* bridge = scope.plotView()->bridge();
        bridge->plotAreaSize = QSizeF(320, 220);
        emit bridge->plotAreaChanged();
        const auto narrow = scope.plotView()->series().front()->points();
        if (narrow.size() > 320 * 3 + 2 || narrow.front() != points.front() || narrow.back() != points.back()
            || !std::any_of(narrow.cbegin(), narrow.cend(), [](const auto& point) { return point.y() == 17.0; })
            || !std::any_of(narrow.cbegin(), narrow.cend(), [](const auto& point) { return std::isnan(point.y()); }))
            throw std::runtime_error("Scope resize did not adapt the display to plot pixels");
        QBuffer buffer;
        buffer.open(QIODevice::ReadWrite);
        QString error;
        if (!scope.writeRecordedCsv(buffer, &error) || buffer.data().count('\n') != count + 1)
            throw std::runtime_error("Scope display reduction changed full-resolution export");
        bool truncated = false;
        const auto exact = scope.recording().analysisSamples(QStringLiteral("live:dense"), 11.995, 12.005,
            64, &truncated, &error);
        if (!error.isEmpty() || truncated || exact.size() != 11
            || exact[5].x() != 12.0 || exact[5].y() != 17.0)
            throw std::runtime_error("Scope analysis did not read the exact disk-backed interval");
        const auto capped = scope.recording().analysisSamples(QStringLiteral("live:dense"), 0.0, 30.0,
            64, &truncated, &error);
        if (!error.isEmpty() || !truncated || capped.size() != 64
            || std::abs(capped.front().x()-29.936) > 1e-12)
            throw std::runtime_error("Scope analysis cap did not select the newest exact samples");
    }
}
