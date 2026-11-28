// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
// The LiveSpectrum observer: its in-place FFT reads 0 dBFS for a full-scale
// sine, puts each tone in the right bin and bar (real one-sided, log bars; I/Q
// two-sided), and its window picks the style and frequency radio buttons.
#include "analysis/live_spectrum.hpp"
#include "viewers/live_spectrum_window.hpp"

#include <QImage>
#include <QPainter>
#include <QRadioButton>
#include <QToolButton>
#include <QSpinBox>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

using namespace s7studio;

namespace {
observation::LiveFrame toneFrame(double rate, std::size_t count, bool iq, double cyclesPerSample)
{
    observation::LiveFrame frame;
    frame.rateNumerator = static_cast<std::uint64_t>(rate);
    frame.dt = 1 / rate;
    if (iq) {
        frame.channels.push_back({"I", "FS", "f32"});
        frame.channels.push_back({"Q", "FS", "f32"});
    } else {
        frame.channels.push_back({"left", "FS", "f32"});
    }
    for (std::size_t n = 0; n < count; ++n) {
        frame.steps.push_back(static_cast<std::int64_t>(n));
        frame.times.push_back(static_cast<double>(n) / rate);
        const double phase = 2 * std::numbers::pi * cyclesPerSample * static_cast<double>(n);
        frame.values.push_back(std::cos(phase));
        if (iq) frame.values.push_back(std::sin(phase));
    }
    return frame;
}

std::size_t loudest(const std::vector<double>& values)
{
    return static_cast<std::size_t>(std::max_element(values.begin(), values.end()) - values.begin());
}
} // namespace

class LiveSpectrumTest : public QObject {
    Q_OBJECT
private slots:
    void realToneReadsFullScaleInItsBinAndBar()
    {
        // 100 cycles per 4096 samples: exactly bin 100 = 1171.875 Hz at 48 kHz.
        analysis::LiveSpectrum spectrum;
        const auto result = spectrum.compute(toneFrame(48000, 8192, false, 100.0 / 4096), {});
        QVERIFY(result);
        QCOMPARE(result->binSpacing, 48000.0 / 4096);
        const auto peak = loudest(result->decibels);
        QCOMPARE(peak, std::size_t(100));
        QVERIFY2(std::abs(result->decibels[peak]) < 1e-9, qPrintable(QString::number(result->decibels[peak])));
        QCOMPARE(result->barDecibels.size(), std::size_t(32));
        const auto bar = loudest(result->barDecibels);
        QVERIFY(result->barLow[bar] <= result->frequencies[peak] && result->frequencies[peak] < result->barHigh[bar]);
        // Log bands: equal ratios from about 20 Hz to Nyquist.
        QVERIFY(result->barLow.front() >= 20 && std::abs(result->barHigh.back() - 24000) < 1e-6);
        const double ratio = result->barHigh[0] / result->barLow[0];
        for (std::size_t i = 1; i < result->barLow.size(); ++i)
            QVERIFY(std::abs(result->barHigh[i] / result->barLow[i] - ratio) < 1e-9 && std::abs(result->barLow[i] - result->barHigh[i - 1]) < 1e-9);
        // Linear bands start at 0 Hz and are equally wide.
        analysis::LiveSpectrumSettings linear;
        linear.logFrequency = false;
        const auto even = spectrum.compute(toneFrame(48000, 8192, false, 100.0 / 4096), linear);
        QVERIFY(even && even->barLow.front() == 0 && std::abs((even->barHigh[0] - even->barLow[0]) - 24000.0 / 32) < 1e-9);
    }

    void iqToneAppearsAtItsSignedFrequency()
    {
        // A complex tone at -50 bins: two-sided spectrum centred on 0 Hz.
        analysis::LiveSpectrum spectrum;
        analysis::LiveSpectrumSettings settings;
        settings.iq = true;
        settings.fftSize = 1024;
        const auto result = spectrum.compute(toneFrame(1e6, 1024, true, -50.0 / 1024), settings);
        QVERIFY(result);
        QCOMPARE(result->frequencies.size(), std::size_t(1024));
        const auto peak = loudest(result->decibels);
        QCOMPARE(result->frequencies[peak], -50 * 1e6 / 1024);
        QVERIFY(std::abs(result->decibels[peak]) < 1e-9);
        // Its positive mirror is absent (only a real signal is symmetric).
        const auto mirror = static_cast<std::size_t>(std::find(result->frequencies.begin(), result->frequencies.end(), 50 * 1e6 / 1024) - result->frequencies.begin());
        QVERIFY(result->decibels[mirror] < -100);
        QVERIFY(result->barLow.front() == -5e5 && result->barHigh.back() == 5e5);
    }

    void shortOrUnratedFramesAreRefused()
    {
        analysis::LiveSpectrum spectrum;
        QVERIFY(!spectrum.compute(toneFrame(48000, 32, false, 0.1), {}));
        auto unrated = toneFrame(48000, 1024, false, 0.1);
        unrated.rateNumerator = 0;
        QVERIFY(!spectrum.compute(unrated, {}));
        analysis::LiveSpectrumSettings iq;
        iq.iq = true;
        QVERIFY(!spectrum.compute(toneFrame(48000, 1024, false, 0.1), iq));  // I/Q needs two channels
    }

    void windowChoosesStyleAndFrequencyScale()
    {
        viewers::LiveSpectrumWindow window(QStringLiteral("analyzer"));
        auto* bars = window.findChild<QRadioButton*>("spectrum.style.bars");
        auto* curve = window.findChild<QRadioButton*>("spectrum.style.curve");
        auto* log = window.findChild<QRadioButton*>("spectrum.scale.log");
        auto* linear = window.findChild<QRadioButton*>("spectrum.scale.linear");
        QVERIFY(bars && curve && log && linear);
        QVERIFY(bars->isChecked() && log->isChecked());

        // An I/Q source defaults to the curve; a log axis is unavailable.
        auto iq = std::make_shared<observation::LiveAcquisition>(1, 1e-6,
            QVector<observation::LiveChannel>{{"I", "FS", "f32"}, {"Q", "FS", "f32"}}, QJsonObject{}, 1000000, 1);
        window.attachLiveSource(iq);
        QVERIFY(curve->isChecked() && linear->isChecked() && !log->isEnabled());
        QVERIFY(iq->capacity() >= 4096);
        std::vector<std::int64_t> steps; std::vector<double> times, values;
        for (int n = 0; n < 4096; ++n) {
            steps.push_back(n); times.push_back(n * 1e-6);
            values.push_back(std::cos(2 * std::numbers::pi * 0.1 * n)); values.push_back(std::sin(2 * std::numbers::pi * 0.1 * n));
        }
        iq->append(steps, times, values);
        window.show();
        window.updateSpectrum();
        QVERIFY(window.display().iq && !window.display().spectrum.frequencies.empty());
        QCOMPARE(window.display().spectrum.frequencies[loudest(window.display().curve)], std::round(0.1 * 4096) * 1e6 / 4096);

        // Saved parameters select the radio buttons and the bar count.
        viewers::LiveSpectrumWindow audio(QStringLiteral("audio"));
        audio.applyParameters({{"bars", 16}, {"style", "bars"}, {"frequency_scale", "linear"}, {"floor_db", -70}});
        auto mono = std::make_shared<observation::LiveAcquisition>(2, 1.0 / 48000,
            QVector<observation::LiveChannel>{{"left", "FS", "f32"}}, QJsonObject{}, 48000, 1);
        audio.attachLiveSource(mono);
        QVERIFY(audio.findChild<QRadioButton*>("spectrum.style.bars")->isChecked());
        QVERIFY(audio.findChild<QRadioButton*>("spectrum.scale.linear")->isChecked());
        QVERIFY(audio.findChild<QRadioButton*>("spectrum.scale.log")->isEnabled());
        QCOMPARE(audio.findChild<QSpinBox*>("spectrum.bars")->value(), 16);
        QCOMPARE(audio.display().floorDb, -70.0);
        QVERIFY(!audio.display().logFrequency);
    }

    // Freeze holds the picture while samples keep arriving; the printout
    // paints a copy on white paper with lit LEDs in their colour bands.
    void freezeAndPaperPicture()
    {
        viewers::LiveSpectrumWindow window(QStringLiteral("print"));
        auto* freeze = window.findChild<QToolButton*>("spectrum.freeze");
        QVERIFY(freeze && freeze->isCheckable() && window.findChild<QToolButton*>("spectrum.print"));
        auto mono = std::make_shared<observation::LiveAcquisition>(2, 1.0 / 48000,
            QVector<observation::LiveChannel>{{"left", "FS", "f32"}}, QJsonObject{}, 48000, 1);
        window.attachLiveSource(mono);
        const auto feed = [&](double cyclesPerSample, std::int64_t first) {
            std::vector<std::int64_t> steps; std::vector<double> times, values;
            for (std::int64_t n = first; n < first + 8192; ++n) {
                steps.push_back(n); times.push_back(static_cast<double>(n) / 48000);
                values.push_back(0.5 * std::cos(2 * std::numbers::pi * cyclesPerSample * static_cast<double>(n)));
            }
            mono->append(steps, times, values);
        };
        feed(1000.0 / 48000, 0);
        window.show();
        window.updateSpectrum();
        QVERIFY(!window.display().spectrum.frequencies.empty());
        const auto before = window.display().spectrum.barDecibels;
        freeze->setChecked(true);
        feed(8000.0 / 48000, 8192);
        window.updateSpectrum();
        QCOMPARE(window.display().spectrum.barDecibels, before);
        freeze->setChecked(false);
        window.updateSpectrum();
        QVERIFY(window.display().spectrum.barDecibels != before);

        QImage paper(900, 400, QImage::Format_ARGB32);
        paper.fill(Qt::black);
        {
            QPainter painter(&paper);
            viewers::paintLiveSpectrum(painter, QRectF(paper.rect()), window.display(), true);
        }
        QCOMPARE(paper.pixel(2, 2), qRgb(255, 255, 255));
        int green = 0;
        for (int y = 0; y < paper.height(); ++y)
            for (int x = 0; x < paper.width(); ++x)
                if (paper.pixel(x, y) == qRgb(0, 150, 45)) ++green;
        QVERIFY2(green > 50, qPrintable(QString::number(green)));
    }
};

QTEST_MAIN(LiveSpectrumTest)
#include "live_spectrum_viewer_test.moc"
