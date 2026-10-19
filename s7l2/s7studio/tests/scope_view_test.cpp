#include "scope_view_window.hpp"
#include "qcp.h"

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
        auto* plot = scope.findChild<QCustomPlot*>();
        if (!plot || plot->graphCount() != 1) {
            throw std::runtime_error("Missing oscillator trace");
        }
        const auto data = plot->graph(0)->data();
        double previousTime = 0.0;
        for (auto it = data->constBegin(); it != data->constEnd(); ++it) {
            if (it->key - previousTime > 0.01 || std::abs(it->value - std::sin(it->key)) > 1e-12) {
                throw std::runtime_error("Oscillator preview lost curve resolution");
            }
            previousTime = it->key;
        }
        if (std::abs(previousTime - static_cast<double>(count - 1) / frequency) > 1e-12
            || data->size() > s7studio::ui::ScopeViewWindow::kMaxInMemorySamplesPerSeries + 1) {
            throw std::runtime_error("Preview endpoint or memory bound incorrect");
        }
        for (auto* check : scope.findChildren<QCheckBox*>()) {
            if (check->text() == QStringLiteral("Fit full run")) check->setChecked(false);
        }
        plot->xAxis->setRange(2.0, 3.0);
        scope.appendSample(10.0, std::sin(10.0));
        QMetaObject::invokeMethod(&scope, "refreshPlot", Qt::DirectConnection);
        if (plot->xAxis->range().lower != 2.0 || plot->xAxis->range().upper != 3.0) {
            throw std::runtime_error("Auto Y changed the visible time range");
        }
    }
}
