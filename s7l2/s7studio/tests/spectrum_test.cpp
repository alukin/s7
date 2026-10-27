// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/spectrum.hpp"
#include "analysis/spectrum_worker.hpp"
#include <QtTest>
#include <algorithm>
#include <complex>
#include <numbers>
#include <numeric>
#include <random>

using namespace s7studio;
using namespace analysis;
namespace {
observation::LiveFrame frame(std::size_t count, const std::function<std::complex<double>(std::int64_t)>& value,
                             std::int64_t first = 1, std::uint64_t run = 1)
{
    observation::LiveFrame f;
    f.runId = run; f.generation = 1; f.dt = 1./1024; f.rateNumerator = 1024;
    f.channels = {{"I", "V", "f64"}, {"Q", "V", "f64"}};
    f.capacity = count; f.received = static_cast<std::uint64_t>(first)+count-1;
    for (std::size_t i = 0; i < count; ++i) {
        const auto step = first+static_cast<std::int64_t>(i);
        f.steps.push_back(step); f.times.push_back(static_cast<double>(step)/1024);
        const auto z = value(step); f.values.push_back(z.real()); f.values.push_back(z.imag());
    }
    return f;
}
std::vector<std::complex<long double>> direct(std::span<const std::complex<double>> input)
{
    std::vector<std::complex<long double>> out(input.size());
    for (std::size_t k = 0; k < input.size(); ++k) for (std::size_t n = 0; n < input.size(); ++n) {
        const auto phase = -2*std::numbers::pi_v<long double>*static_cast<long double>(k*n)/static_cast<long double>(input.size());
        out[k] += std::complex<long double>(input[n].real(), input[n].imag())*std::complex<long double>(std::cos(phase), std::sin(phase));
    }
    return out;
}
}
class SpectrumTest : public QObject {
    Q_OBJECT
private slots:
    void fftAdapterAgreesWithIndependentDftAndInverse() {
        std::vector<std::complex<double>> input(64);
        for (std::size_t i = 0; i < input.size(); ++i) input[i] = {std::sin(static_cast<double>(i)*.27), std::cos(static_cast<double>(i)*.41)};
        Fft fft; const auto actual = fft.transform(input); const auto expected = direct(input);
        for (std::size_t i = 0; i < input.size(); ++i) {
            QVERIFY(std::abs(actual[i].real()-static_cast<double>(expected[i].real())) < 2e-12);
            QVERIFY(std::abs(actual[i].imag()-static_cast<double>(expected[i].imag())) < 2e-12);
        }
        const auto restored = fft.transform(actual, true);
        for (std::size_t i = 0; i < input.size(); ++i) QVERIFY(std::abs(restored[i]-input[i]) < 2e-14);
        input.resize(63);
        const auto irregular = fft.transform(input);
        const auto irregularReference = direct(input);
        for (std::size_t i = 0; i < input.size(); ++i)
            QVERIFY(std::abs(irregular[i]-std::complex<double>(irregularReference[i])) < 2e-12);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, fft.transform(std::vector<std::complex<double>>{}));
    }
    void fullIntervalWithNonPowerOfTwoLength() {
        constexpr std::size_t n = 125;
        SpectrumSettings settings; settings.size = n; settings.window = SpectrumWindow::Rectangular;
        SpectrumAnalyzer analyzer;
        const auto result = analyzer.analyze(frame(n, [](auto step) {
            return 2.5*std::sin(2*std::numbers::pi*17*static_cast<double>(step)/n);
        }), settings);
        QCOMPARE(result.state, SpectrumResult::State::Ready);
        QCOMPARE(result.values.size(), std::size_t(63));
        QVERIFY(std::abs(result.values[17]-2.5) < 2e-13);
        QVERIFY(std::abs(result.binSpacing-1024./n) < 1e-14);
        settings.iq = true;
        for (const int signedBin : {-17, 17}) {
            analyzer.reset();
            const auto complexResult = analyzer.analyze(frame(n, [signedBin](auto step) {
                const double phase = 2*std::numbers::pi*signedBin*static_cast<double>(step)/n;
                return std::complex<double>(std::cos(phase), std::sin(phase));
            }), settings);
            QCOMPARE(complexResult.state, SpectrumResult::State::Ready);
            const auto index = static_cast<std::size_t>(static_cast<int>(n/2)+signedBin);
            QVERIFY(std::abs(complexResult.values[index]-1.0) < 2e-13);
            QVERIFY(std::abs(complexResult.frequencies[index]-signedBin*1024./n) < 2e-14);
        }
    }
    void dcNyquistAndToneAmplitude() {
        SpectrumSettings settings; settings.size = 256; settings.window = SpectrumWindow::Rectangular;
        SpectrumAnalyzer analyzer;
        auto dc = analyzer.analyze(frame(256, [](auto) { return 3.; }), settings);
        QCOMPARE(dc.state, SpectrumResult::State::Ready); QCOMPARE(dc.values[0], 3.); QCOMPARE(dc.values.size(), std::size_t(129));
        analyzer.reset();
        auto nyquist = analyzer.analyze(frame(256, [](auto step) { return step % 2 ? -3. : 3.; }), settings);
        QCOMPARE(nyquist.values.back(), 3.); QCOMPARE(nyquist.frequencies.back(), 512.);
        for (int window = 0; window < 5; ++window) {
            analyzer.reset(); settings.window = static_cast<SpectrumWindow>(window);
            auto tone = analyzer.analyze(frame(256, [](auto step) { return 2.5*std::sin(2*std::numbers::pi*17*static_cast<double>(step)/256); }), settings);
            QCOMPARE(tone.state, SpectrumResult::State::Ready);
            QVERIFY(std::abs(tone.values[17]-2.5) < 2e-13); QCOMPARE(tone.frequencies[17], 68.);
            const double gain[]{1, .5, .54, .42, .35875};
            const double energy[]{1, .375, .3974, .3046, .25796335495};
            QVERIFY(std::abs(tone.coherentGain-gain[window]) < 2e-15);
            QVERIFY(std::abs(tone.enbw-4*energy[window]/(gain[window]*gain[window])) < 2e-12);
        }
    }
    void parsevalForNoiseAndIq_data() {
        QTest::addColumn<int>("window"); QTest::addColumn<bool>("iq"); QTest::addColumn<bool>("psd");
        for (int window = 0; window < 5; ++window) for (bool iq : {false, true}) for (bool psd : {false, true})
            QTest::newRow(qPrintable(QString("%1-%2-%3").arg(window).arg(iq).arg(psd))) << window << iq << psd;
    }
    void parsevalForNoiseAndIq() {
        QFETCH(int, window); QFETCH(bool, iq); QFETCH(bool, psd);
        std::mt19937 random(4242); std::normal_distribution<double> normal;
        auto input = frame(256, [&](auto) { return std::complex<double>{normal(random), normal(random)}; });
        SpectrumSettings settings; settings.size = 256; settings.window = static_cast<SpectrumWindow>(window); settings.iq = iq;
        settings.quantity = psd ? SpectrumQuantity::Psd : SpectrumQuantity::Power;
        SpectrumAnalyzer analyzer; const auto result = analyzer.analyze(input, settings);
        QCOMPARE(result.state, SpectrumResult::State::Ready);
        // Independent time-domain energy reference; not a coherent-tone amplitude squared.
        const auto weights = spectrumWindow(settings.window, 256);
        long double numerator = 0, denominator = 0;
        for (std::size_t i = 0; i < 256; ++i) {
            const long double w2 = static_cast<long double>(weights[i])*weights[i];
            const long double real = input.values[2*i], imag = iq ? input.values[2*i+1] : 0;
            numerator += w2*(real*real+imag*imag); denominator += w2;
        }
        const double integral = std::accumulate(result.values.begin(), result.values.end(), 0.)*(psd ? result.binSpacing : 1);
        QVERIFY(std::abs(integral-static_cast<double>(numerator/denominator)) < 4e-13);
    }
    void offBinAndTwoTonesMatchDirectReference() {
        auto input = frame(64, [](auto step) { const double t = static_cast<double>(step-1); return 1.2+.8*std::cos(2*std::numbers::pi*3.4*t/64)+.3*std::sin(2*std::numbers::pi*13*t/64); });
        SpectrumSettings settings; settings.size = 64; settings.window = SpectrumWindow::Hann;
        std::vector<std::complex<double>> weighted(64);
        for (std::size_t i = 0; i < 64; ++i) weighted[i] = input.values[2*i]*(.5-.5*std::cos(2*std::numbers::pi*static_cast<double>(i)/64));
        const auto dft = direct(weighted);
        SpectrumAnalyzer analyzer; const auto result = analyzer.analyze(input, settings);
        for (std::size_t k = 0; k < result.values.size(); ++k) {
            const double expected = static_cast<double>(std::abs(dft[k]))/32*(k == 0 || k == 32 ? 1 : 2);
            QVERIFY(std::abs(result.values[k]-expected) < 4e-14);
        }
    }
    void iqSignUnitsAndMeanRemoval() {
        SpectrumSettings settings; settings.size = 128; settings.window = SpectrumWindow::Rectangular; settings.iq = true;
        auto positive = frame(128, [](auto step) { return std::polar(2., 2*std::numbers::pi*7*static_cast<double>(step)/128); });
        SpectrumAnalyzer analyzer; auto result = analyzer.analyze(positive, settings);
        QCOMPARE(result.state, SpectrumResult::State::Ready);
        const auto peak = std::max_element(result.values.begin(), result.values.end())-result.values.begin();
        QCOMPARE(result.frequencies[static_cast<std::size_t>(peak)], 56.); QVERIFY(std::abs(result.values[static_cast<std::size_t>(peak)]-2) < 2e-14);
        for (std::size_t i = 1; i < positive.values.size(); i += 2) positive.values[i] = -positive.values[i];
        analyzer.reset(); result = analyzer.analyze(positive, settings);
        QCOMPARE(result.frequencies[static_cast<std::size_t>(std::max_element(result.values.begin(), result.values.end())-result.values.begin())], -56.);
        positive.channels[1].unit = "A"; analyzer.reset();
        QCOMPARE(analyzer.analyze(positive, settings).state, SpectrumResult::State::Unavailable);
        settings.declaredUnit = "declared V";
        QCOMPARE(analyzer.analyze(positive, settings).unit, QString("declared V"));
        settings.iq = false; settings.removeMean = true;
        result = analyzer.analyze(frame(128, [](auto) { return 12.; }), settings);
        QCOMPARE(result.state, SpectrumResult::State::Ready);
        for (const auto value : result.values) QCOMPARE(value, 0.);
    }
    void decibelsHaveExplicitReferenceAndPreserveZero() {
        QVERIFY(std::abs(spectrumDecibels(2, SpectrumQuantity::Amplitude, 1)-6.020599913279624) < 2e-14);
        QVERIFY(std::abs(spectrumDecibels(4, SpectrumQuantity::Power, 1)-6.020599913279624) < 2e-14);
        QCOMPARE(spectrumDecibels(1, SpectrumQuantity::Psd, .001), 30.);
        QCOMPARE(spectrumDecibels(0, SpectrumQuantity::Amplitude, 1), -std::numeric_limits<double>::infinity());
        QCOMPARE(spectrumDecibels(1e300, SpectrumQuantity::Amplitude, 1e-300), 12000.);
        QVERIFY_THROWS_EXCEPTION(std::invalid_argument, spectrumDecibels(1, SpectrumQuantity::Power, 0));
    }
    void fillingGapsNonfiniteAndOverflowAreExplicit() {
        SpectrumSettings settings; settings.size = 64;
        SpectrumAnalyzer analyzer;
        QCOMPARE(analyzer.analyze(frame(32, [](auto) { return 1.; }), settings).state, SpectrumResult::State::Filling);
        auto input = frame(64, [](auto) { return 1.; }); input.steps[30] += 1;
        QCOMPARE(analyzer.analyze(input, settings).state, SpectrumResult::State::Unavailable);
        input = frame(64, [](auto) { return std::numeric_limits<double>::quiet_NaN(); });
        QCOMPARE(analyzer.analyze(input, settings).state, SpectrumResult::State::Unavailable);
        input = frame(64, [](auto) { return std::numeric_limits<double>::max(); });
        QCOMPARE(analyzer.analyze(input, settings).state, SpectrumResult::State::Unavailable);
        input = frame(64, [](auto) { return 1.; }); input.rateNumerator = 0;
        QCOMPARE(analyzer.analyze(input, settings).state, SpectrumResult::State::Unavailable);
        settings.size = 63;
        QCOMPARE(analyzer.analyze(input, settings).state, SpectrumResult::State::Unavailable);
    }
    void averagesAreLinearAndCountEachHopOnce() {
        auto value = [](auto step) { return step <= 64 ? 1. : 3.; };
        SpectrumSettings s; s.size = 64; s.window = SpectrumWindow::Rectangular;
        s.quantity = SpectrumQuantity::Power; s.average = SpectrumAverage::Block; s.averageCount = 2; s.peakHold = true;
        SpectrumAnalyzer analyzer;
        auto first = analyzer.analyze(frame(64, value), s); QCOMPARE(first.averaged, std::size_t(1)); QCOMPARE(first.values[0], 1.);
        auto repeated = analyzer.analyze(frame(64, value), s); QCOMPARE(repeated.averaged, std::size_t(1));
        auto tooSoon = analyzer.analyze(frame(64, value, 10), s); QCOMPARE(tooSoon.lastStep, std::int64_t(64));
        auto second = analyzer.analyze(frame(64, value, 33), s); QCOMPARE(second.averaged, std::size_t(2)); QVERIFY(std::abs(second.values[0]-2.5) < 2e-14);
        QCOMPARE(second.averageFirstStep, std::int64_t(1)); QCOMPARE(second.lastStep, std::int64_t(96));
        auto nextBlock = analyzer.analyze(frame(64, value, 65), s); QCOMPARE(nextBlock.averaged, std::size_t(1)); QCOMPARE(nextBlock.values[0], 9.);
        QCOMPARE(nextBlock.averageFirstStep, std::int64_t(65)); QCOMPARE(nextBlock.holdFirstStep, std::int64_t(1));
        QCOMPARE(nextBlock.peakHold[0], 9.);
        s.average = SpectrumAverage::Exponential; s.alpha = .25; analyzer.reset();
        analyzer.analyze(frame(64, value), s);
        auto exponential = analyzer.analyze(frame(64, value, 33), s); QVERIFY(std::abs(exponential.values[0]-1.75) < 2e-14);
        auto skipped = analyzer.analyze(frame(64, value, 129), s); QCOMPARE(skipped.skippedHops, std::uint64_t(2)); QCOMPARE(skipped.averaged, std::size_t(1));
        auto rerun = analyzer.analyze(frame(64, value, 1, 2), s); QCOMPARE(rerun.averaged, std::size_t(1)); QCOMPARE(rerun.skippedHops, std::uint64_t(0));
        auto resizedFrame = frame(64, value, 33, 2); resizedFrame.generation = 2;
        QCOMPARE(analyzer.analyze(resizedFrame, s).averaged, std::size_t(1));
    }
    void maximumSizeAndCancellation() {
        SpectrumSettings settings; settings.size = 65536; settings.window = SpectrumWindow::Rectangular;
        SpectrumAnalyzer analyzer; auto input = frame(65536, [](auto) { return 1.; });
        auto result = analyzer.analyze(input, settings); QCOMPARE(result.state, SpectrumResult::State::Ready);
        QCOMPARE(result.values.size(), std::size_t(32769)); QCOMPARE(result.values[0], 1.);
        analyzer.reset();
        QCOMPARE(analyzer.analyze(input, settings, [] { return true; }).state, SpectrumResult::State::Cancelled);
    }
    void workerRetainsAveragesAcrossDispatchAndRejectsCancelledRuns() {
        SpectrumWorker worker; SpectrumSettings settings; settings.size = 64; settings.window = SpectrumWindow::Rectangular;
        settings.quantity = SpectrumQuantity::Power; settings.average = SpectrumAverage::Block;
        auto value = [](auto step) { return step <= 64 ? 1. : 3.; };
        worker.submit(std::make_shared<observation::LiveFrame>(frame(64, value)), settings, 1);
        std::optional<SpectrumResult> first;
        QTRY_VERIFY(first.has_value() || (first = worker.takeResult()).has_value()); QCOMPARE(first->averaged, std::size_t(1));
        QTRY_VERIFY(!worker.busy());
        worker.submit(std::make_shared<observation::LiveFrame>(frame(64, value, 10)), settings, 1);
        std::optional<SpectrumResult> reused;
        QTRY_VERIFY(reused.has_value() || (reused = worker.takeResult()).has_value());
        QVERIFY(reused->reused); QCOMPARE(reused->frame->steps.front(), std::int64_t(1)); QCOMPARE(reused->lastStep, std::int64_t(64));
        worker.submit(std::make_shared<observation::LiveFrame>(frame(64, value, 33)), settings, 1);
        std::optional<SpectrumResult> second;
        QTRY_VERIFY(second.has_value() || (second = worker.takeResult()).has_value()); QCOMPARE(second->averaged, std::size_t(2));
        settings.size = 65536;
        auto big = std::make_shared<observation::LiveFrame>(frame(65536, value));
        for (int i = 0; i < 100; ++i) worker.submit(big, settings, 2);
        worker.cancel(); settings.size = 64;
        worker.submit(std::make_shared<observation::LiveFrame>(frame(64, [](auto) { return 9.; }, 1, 9)), settings, 3);
        std::optional<SpectrumResult> latest;
        QTRY_VERIFY(latest.has_value() || (latest = worker.takeResult()).has_value());
        QCOMPARE(latest->runId, std::uint64_t(9)); QCOMPARE(latest->revision, std::uint64_t(3)); QCOMPARE(latest->values[0], 81.);
        QVERIFY(latest->frame); QCOMPARE(latest->frame->runId, std::uint64_t(9));
    }
    void workerOverlaysAllRealInputsAndReportsInvalidChannels() {
        SpectrumWorker worker;
        SpectrumSettings settings; settings.size = 256; settings.window = SpectrumWindow::Rectangular;
        auto input = std::make_shared<observation::LiveFrame>(frame(256, [](auto step) {
            const double phase = 2*std::numbers::pi*static_cast<double>(step)/256;
            return std::complex<double>(std::sin(11*phase), .5*std::sin(29*phase));
        }));
        worker.submit(input, settings, 1, true);
        std::optional<SpectrumResult> result;
        QTRY_VERIFY(result.has_value() || (result = worker.takeResult()).has_value());
        QCOMPARE(result->state, SpectrumResult::State::Ready);
        QCOMPARE(result->traces.size(), std::size_t(2));
        QCOMPARE(result->traces[0].channel, std::size_t(0));
        QCOMPARE(result->traces[1].channel, std::size_t(1));
        QVERIFY(std::abs(result->traces[0].values[11]-1.) < 2e-13);
        QVERIFY(std::abs(result->traces[1].values[29]-.5) < 2e-13);
        input = std::make_shared<observation::LiveFrame>(*input);
        input->values[2*7+1] = std::numeric_limits<double>::quiet_NaN();
        worker.cancel(); worker.submit(input, settings, 2, true);
        result.reset();
        QTRY_VERIFY(result.has_value() || (result = worker.takeResult()).has_value());
        QCOMPARE(result->state, SpectrumResult::State::Ready);
        QCOMPARE(result->traces.size(), std::size_t(1));
        QVERIFY(result->message.contains(QStringLiteral("Q:")));
    }
};
QTEST_GUILESS_MAIN(SpectrumTest)
#include "spectrum_test.moc"
