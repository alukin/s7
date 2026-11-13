// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/result_file.hpp"
#include "viewers/surface_plot.hpp"
#include "viewers/time_frequency_window.hpp"
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>
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
        for (bool wavelet : {false, true}) {
            auto source = std::make_shared<s7studio::observation::ObservationSource>(123);
            source->channel("map/in");
            for (int n = 0; n < 2048; ++n)
                require(source->append(
                            {0, n + 1, double(n + 1) / 256, std::sin(2 * std::numbers::pi * 32 * n / 256)}),
                        "Capture");
            require(source->finish(), "Flush");
            s7studio::viewers::TimeFrequencyWindow window;
            window.show();
            window.setWavelet(wavelet);
            window.attachSource(source, {{"in", 0}});
            while (window.refreshSource()) {
            }
            window.analyze();
            QElapsedTimer timer;
            timer.start();
            while (!window.hasResult() && window.error().isEmpty() && timer.elapsed() < 10000) {
                window.poll();
                app.processEvents();
                QThread::msleep(5);
            }
            require(window.hasResult(), window.error().toUtf8().constData());
            if (!wavelet) {
                auto *quantity = window.findChild<QComboBox *>("timeFrequency.quantity");
                require(quantity, "Quantity control");
                quantity->setCurrentIndex(4);
                auto *plot = window.findChild<s7studio::viewers::SurfacePlotView *>();
                require(plot && std::abs(plot->bridge()->geometry.bounds().max[2] - 1.0 / 3) < 1e-10,
                        "Displayed PSD normalization");
                quantity->setCurrentIndex(0);
            }
            const auto path = directory.filePath(wavelet ? "wavelet.s7analysis" : "stft.s7analysis");
            window.saveResult(path);
            auto saved = s7studio::analysis::ResultFile::open(path);
            require(saved.metadata["run_id"].toString() == "123", "Transform run identity");
            require(saved.kind == "time_frequency" && saved.metadata["rate_hz"].toDouble() == 256,
                    "Result identity/rate");
            if (!wavelet) {
                const auto row = std::size_t(std::find(saved.y.begin(), saved.y.end(), 32) - saved.y.begin());
                require(row < saved.y.size(), "Tone frequency");
                for (std::size_t t = 0; t < saved.x.size(); ++t)
                    require(std::abs(std::abs(saved.values[row * saved.x.size() + t]) - 1) < 1e-10,
                            "Tone coefficient normalization/transpose");
            }
            require(saved.input.size() == 2048, "Frozen raw interval must be retained");
            const auto csvPath = directory.filePath("roundtrip.csv");
            saved.csv(csvPath);
            const auto csv = s7studio::analysis::ResultFile::open(csvPath);
            require(csv.values == saved.values && csv.valid == saved.valid, "Coefficient CSV round-trip");
            window.clearSamples();
            require(!window.hasResult(), "Clear result");
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
            saved.csv(directory.filePath("map.csv"));
            require(QFile::exists(directory.filePath("map.csv")), "CSV export");
        }
        std::cout << "Time-frequency capture, workers, save/open and rollback passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
