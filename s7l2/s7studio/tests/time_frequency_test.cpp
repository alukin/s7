// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/result_file.hpp"
#include "viewers/surface_plot.hpp"
#include "viewers/time_frequency_window.hpp"
#include "viewers/line_plot_view.hpp"
#include "analysis/worker_pool.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTabWidget>
#include <QThreadPool>
#include <QtGraphs/QLineSeries>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <numbers>
#include <stdexcept>
namespace {
void require(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir directory;
        require(directory.isValid(), "Temporary directory");
        for (int kind = 0; kind < 5; ++kind) {
            const bool wavelet = kind == 1 || kind == 3, impulse = kind == 2;
            const int rate = kind == 2 || kind == 3 ? 1024 : 256,
                      count = kind == 4 ? 256 : kind >= 2 ? 8192 : 2048;
            auto source = std::make_shared<s7studio::observation::ObservationSource>(123);
            source->channel("map/in");
            for (int n = 0; n < count; ++n)
                require(source->append(
                            {0, n + 1, double(n + 1) / rate,
                             impulse ? (n == 1535 ? 1024.0 : 0.0) : std::sin(2 * std::numbers::pi * 32 * n / rate)}),
                        "Capture");
            require(source->finish(), "Flush");
            s7studio::viewers::TimeFrequencyWindow window;
            window.show();
            window.setWavelet(wavelet);
            window.attachSource(source, {{"in", 0}});
            while (window.refreshSource()) {
            }
            auto *analyze = window.findChild<QPushButton *>("timeFrequency.analyze");
            require(analyze && analyze->styleSheet().contains("#1565c0"), "Prominent analysis action");
            QElapsedTimer timer;
            timer.start();
            while (!window.hasResult() && window.error().isEmpty() && timer.elapsed() < 10000) {
                window.poll();
                app.processEvents();
                QThread::msleep(5);
            }
            require(window.hasResult(), window.error().toUtf8().constData());
            std::cout << "Automatic " << (wavelet ? "CWT" : "STFT") << " for " << count
                      << " samples: " << timer.elapsed() << " ms including presentation\n";
            require(!window.findChild<QLabel *>("timeFrequency.empty")->isVisible(), "Hide empty-state guidance after automatic analysis");
            if (kind == 0) {
                auto &pool = s7studio::analysis::analysisPool();
                QSemaphore entered, release;
                bool released = false;
                const auto cleanup = qScopeGuard([&] {
                    if (!released) release.release(2);
                    pool.waitForDone(5000);
                });
                for (int i = 0; i < 2; ++i)
                    pool.start([&] { entered.release(); release.acquire(); });
                entered.acquire(2);
                analyze->click();
                const bool retained = window.hasResult();
                auto *tabs = window.findChild<QTabWidget *>("timeFrequency.views");
                tabs->setCurrentIndex(1);
                window.poll();
                release.release(2); released = true;
                require(pool.waitForDone(5000), "Cancelled analysis completion");
                window.poll();
                std::cout << "Reanalysis retained result: " << retained
                          << "; action recovered on input trace: " << analyze->isEnabled() << '\n';
                require(retained, "Reanalysis must retain the last valid map while work is pending");
                require(analyze->isEnabled(), "Input trace must allow Analyze after cancelling derived work");
                tabs->setCurrentIndex(0);
                window.poll();
            }
            if (kind == 4) {
                const auto image = window.findChild<QWidget *>("surface.heatmap")->grab().toImage();
                int red = 0;
                for (int y = 0; y < image.height(); ++y)
                    for (int x = 0; x < image.width(); ++x) {
                        const auto color = image.pixelColor(x, y);
                        red += color.red() > 200 && color.green() < 100 && color.blue() < 100;
                    }
                require(red > 1000, "A single FFT frame must display its real coefficients as a heatmap band");
            }
            if (!wavelet && !impulse) {
                auto *quantity = window.findChild<QComboBox *>("timeFrequency.quantity");
                require(quantity, "Quantity control");
                quantity->setCurrentIndex(4);
                auto *plot = window.findChild<s7studio::viewers::SurfacePlotView *>();
                require(plot && std::abs(plot->bridge()->geometry.bounds().max[2] - 1.0 / 3) < 1e-10,
                        "Displayed PSD normalization");
                quantity->setCurrentIndex(0);
            }
            if (impulse) {
                auto *column = window.findChild<QSpinBox *>("timeFrequency.column");
                require(std::abs(window.result()->x[std::size_t(column->value())] - 1.5) < .125,
                        "Initial inspection must show the transient rather than a silent record midpoint");
                auto *trace = window.findChild<s7studio::viewers::LinePlotView *>("timeFrequency.inputTrace");
                require(trace && trace->series().size() == 1, "Input trace");
                const auto &points = trace->series().front()->points();
                require(std::any_of(points.begin(), points.end(), [](const QPointF &p) {
                    return p.x() == 1.5 && p.y() == 1024;
                }), "Envelope must preserve the exact one-sample impulse and its timestamp");
                auto *band = window.findChild<QDoubleSpinBox *>("timeFrequency.displayMaximum");
                auto *plot = window.findChild<s7studio::viewers::SurfacePlotView *>();
                band->setValue(40);
                require(plot->bridge()->geometry.bounds().max[1] == 40, "Displayed frequency band");
                require(window.result()->y.back() == 512, "Display band must retain full transform");
            }
            const auto path = directory.filePath(wavelet ? "wavelet.s7analysis" : "stft.s7analysis");
            window.saveResult(path);
            auto saved = s7studio::analysis::ResultFile::open(path);
            require(saved.metadata["run_id"].toString() == "123", "Transform run identity");
            require(saved.kind == "time_frequency" && saved.metadata["rate_hz"].toDouble() == rate,
                    "Result identity/rate");
            if (!wavelet && !impulse) {
                const auto row = std::size_t(std::find(saved.y.begin(), saved.y.end(), 32) - saved.y.begin());
                require(row < saved.y.size(), "Tone frequency");
                for (std::size_t t = 0; t < saved.x.size(); ++t)
                    require(std::abs(std::abs(saved.values[row * saved.x.size() + t]) - 1) < 1e-10,
                            "Tone coefficient normalization/transpose");
            }
            require(saved.input.size() == std::size_t(count), "Frozen raw interval must be retained");
            const auto csvPath = directory.filePath("roundtrip.csv");
            saved.csv(csvPath);
            const auto csv = s7studio::analysis::ResultFile::open(csvPath);
            require(csv.values == saved.values && csv.valid == saved.valid, "Coefficient CSV round-trip");
            window.clearSamples();
            require(!window.hasResult(), "Clear result");
            window.poll();
            require(!window.hasResult(), "Clear must not immediately restart automatic analysis");
            window.openResult(path);
            require(window.hasResult(), "Open result");
            QFile bad(directory.filePath("bad.s7analysis"));
            require(bad.open(QIODevice::WriteOnly), "Bad fixture");
            bad.write("invalid");
            bad.close();
            bool rejected = false;
            try {
                window.openResult(bad.fileName());
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected && window.hasResult(), "Malformed import rollback");
            if (kind == 0) {
                auto *column = window.findChild<QSpinBox *>("timeFrequency.column");
                const auto time = window.result()->x[std::size_t(column->value())];
                window.setWavelet(true);
                timer.restart();
                while (!window.result()->metadata["method"].toString().startsWith("Morlet") &&
                       window.error().isEmpty() && timer.elapsed() < 10000) {
                    window.poll();
                    app.processEvents();
                    QThread::msleep(5);
                }
                require(window.result()->metadata["method"].toString().startsWith("Morlet"),
                        "Frozen interval method switch");
                require(std::abs(window.result()->x[std::size_t(column->value())] - time) <= 1.0 / rate,
                        "Switching transform must retain cursor model time");
                auto *row = window.findChild<QSpinBox *>("timeFrequency.row");
                require(std::abs(window.result()->y[std::size_t(row->value())] - 32) < 4,
                        "Switching transform must retain cursor frequency rather than row number");
                require(!window.findChild<QComboBox *>("timeFrequency.fftSize")->isEnabled(),
                        "CWT must disable irrelevant FFT control");
            }
            saved.csv(directory.filePath("map.csv"));
            require(QFile::exists(directory.filePath("map.csv")), "CSV export");
        }
        {
            auto source = std::make_shared<s7studio::observation::ObservationSource>(124);
            source->channel("map/in");
            for (int n = 0; n < 8193; ++n)
                require(source->append({0, n + 1, double(n + 1) / 1024, std::sin(.1 * n)}), "Long capture");
            require(source->finish(), "Long capture finish");
            s7studio::viewers::TimeFrequencyWindow window;
            window.show();
            window.attachSource(source, {{"in", 0}});
            while (window.refreshSource()) {}
            window.poll();
            require(!window.hasResult(), "Long recordings require explicit analysis");
            require(window.findChild<QPushButton *>("timeFrequency.analyze")->isEnabled(), "Long recording primary action enabled");
            require(window.findChild<QLabel *>("timeFrequency.empty")->isVisible(), "Explain the empty map for manual analysis");
        }
        {
            auto source = std::make_shared<s7studio::observation::ObservationSource>(125);
            source->channel("map/in");
            auto append = [&](int first, int last) {
                for (int n = first; n < last; ++n)
                    require(source->append({0, n + 1, double(n + 1) / 256,
                        std::sin(2 * std::numbers::pi * 32 * n / 256)}), "Growing capture");
            };
            append(0, 512);
            s7studio::viewers::TimeFrequencyWindow window;
            window.show();
            window.attachSource(source, {{"in", 0}});
            while (window.refreshSource()) {}
            auto waitForSize = [&](std::size_t count) {
                QElapsedTimer timer; timer.start();
                while ((!window.hasResult() || window.result()->input.size() != count) &&
                       window.error().isEmpty() && timer.elapsed() < 5000) {
                    window.poll(); app.processEvents(); QThread::msleep(5);
                }
                require(window.hasResult() && window.result()->input.size() == count,
                        "Analyze / freeze must use the latest capture, not the preceding frozen input");
            };
            window.analyze();
            waitForSize(512);
            append(512, 1024);
            while (window.refreshSource()) {}
            auto *button = window.findChild<QPushButton *>("timeFrequency.analyze");
            button->click();
            require(window.hasResult() && window.result()->input.size() == 512, "Retain preceding snapshot while refreshing");
            waitForSize(1024);
            require(window.result()->metadata["last_step"].toString() == "1024", "Fresh capture provenance");
            require(source->finish(), "Growing capture finish");
            auto *fft = window.findChild<QComboBox *>("timeFrequency.fftSize");
            fft->setCurrentIndex(fft->findData(65536));
            QElapsedTimer failedJob; failedJob.start();
            while (window.error().isEmpty() && failedJob.elapsed() < 5000) {
                window.poll(); app.processEvents(); QThread::msleep(5);
            }
            require(!window.error().isEmpty() && window.hasResult() && window.result()->input.size() == 1024,
                    "Failed reanalysis must retain the last valid result");
            fft->setCurrentIndex(fft->findData(512));
            failedJob.restart();
            while (window.result()->metadata["window_size"].toInt() != 512 && failedJob.elapsed() < 5000) {
                window.poll(); app.processEvents(); QThread::msleep(5);
            }
            require(window.error().isEmpty() && window.result()->metadata["window_size"].toInt() == 512,
                    "Valid settings must recover from a failed analysis");

            auto imported = *window.result();
            for (auto &value : imported.values) value *= 2;
            imported.input.clear();
            const auto path = directory.filePath("read-only.s7analysis");
            imported.save(path);
            auto &pool = s7studio::analysis::analysisPool();
            QSemaphore entered, release;
            bool released = false;
            const auto cleanup = qScopeGuard([&] {
                if (!released) release.release(2);
                pool.waitForDone(5000);
            });
            for (int i = 0; i < 2; ++i)
                pool.start([&] { entered.release(); release.acquire(); });
            entered.acquire(2);
            button->click();
            window.openResult(path);
            window.hide(); window.poll();
            release.release(2); released = true;
            require(pool.waitForDone(5000), "Retired worker after import");
            window.show();
            for (int i = 0; i < 20; ++i) { window.poll(); app.processEvents(); QThread::msleep(5); }
            require(window.result()->values == imported.values && window.result()->input.empty(),
                    "Retired work must not overwrite an imported result after hiding/showing");
            require(!button->isEnabled() && !window.findChild<QComboBox *>("timeFrequency.method")->isEnabled(),
                    "Imported results without input must disable reanalysis controls");
        }
        std::cout << "Time-frequency capture, workers, save/open and rollback passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
