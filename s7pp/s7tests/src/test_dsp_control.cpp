#include "s7.hpp"
#include "s7netio.hpp"
#include "s7stdlib.hpp"

#include <cmath>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <vector>

namespace s7tests {

using namespace s7;


auto nearlyEqual(const Real a, const Real b, const Real eps = 1e-9) -> bool
{
    return std::abs(a - b) <= eps;
}

auto readRealOutput(const std::shared_ptr<Element>& element, const int output_index = 0) -> Real
{
    const auto out = std::dynamic_pointer_cast<RealOutput>(element->getOutput(output_index));
    if (!out)
    {
        throw std::runtime_error("Expected RealOutput in test harness");
    }
    return out->value();
}

void require(const bool cond, const std::string& message)
{
    if (!cond)
    {
        throw std::runtime_error(message);
    }
}

void testFirImpulseResponse()
{
    auto schema = std::make_shared<Schema>(1.0);
    auto impulse = std::make_shared<ImpulseSource>(1.0);
    auto fir = std::make_shared<FIRFilter>(std::vector<Real>{0.25, 0.5, 0.25});

    schema->insert(impulse);
    schema->insert(fir);
    schema->connect(impulse, 0, fir, 0);

    const std::vector<Real> expected{0.25, 0.5, 0.25, 0.0, 0.0, 0.0};
    for (std::size_t k = 0; k < expected.size(); ++k)
    {
        schema->run(1);
        const auto y = readRealOutput(fir);
        require(nearlyEqual(y, expected[k], 1e-12),
                "FIR impulse response mismatch at sample " + std::to_string(k));
    }
}

void testIirStepResponse()
{
    constexpr Real alpha = 0.8;
    constexpr Real b0 = 1.0 - alpha;

    auto schema = std::make_shared<Schema>(1.0);
    auto step = std::make_shared<Constant<Real>>(1.0);
    auto iir = std::make_shared<IIRFilter>(std::vector<Real>{b0}, std::vector<Real>{-alpha});

    schema->insert(step);
    schema->insert(iir);
    schema->connect(step, 0, iir, 0);

    for (std::size_t k = 0; k < 20; ++k)
    {
        schema->run(1);
        const auto y = readRealOutput(iir);
        const auto expected = 1.0 - std::pow(alpha, static_cast<Real>(k + 1));
        require(nearlyEqual(y, expected, 1e-10),
                "IIR step response mismatch at sample " + std::to_string(k));
    }
}

void testOscillatorAgreement()
{
    constexpr Real frequency_hz = 440.0;
    constexpr long samples_per_period = 200;
    constexpr long periods = 100;
    const Real dt = 1.0 / (frequency_hz * static_cast<Real>(samples_per_period));
    const long total_samples = periods * samples_per_period;
    const Real total_time = static_cast<Real>(total_samples) * dt;

    auto schema = std::make_shared<Schema>(dt);

    HarmonicOscillator::Params ho;
    ho.frequency_hz = frequency_hz;
    ho.phase_rad = 0.2;
    ho.amplitude = 1.0;

    DigitalOscillator::Params dos;
    dos.frequency_hz = frequency_hz;
    dos.phase_rad = 0.2;
    dos.amplitude = 1.0;
    dos.radius = 1.0;

    auto harmonic = std::make_shared<HarmonicOscillator>(ho);
    auto digital = std::make_shared<DigitalOscillator>(dos);
    auto energy = std::make_shared<EnergyDifference>();

    schema->insert(harmonic);
    schema->insert(digital);
    schema->insert(energy);

    schema->connect(harmonic, 0, energy, 0);
    schema->connect(digital, 0, energy, 1);

    schema->run(total_samples);

    const auto e = readRealOutput(energy);
    const auto rms = std::sqrt(e / total_time);

    require(rms < 1e-8, "Oscillator RMS mismatch too high: " + std::to_string(rms));
}

void testClassicalOscillator2Integrators()
{
    // Classical oscillator built from two Euler integrators and scaling sums.
    //
    // Physics: dx/dt = v,  dv/dt = -x   (unit angular frequency omega = 1 rad/s)
    // Euler update:
    //   x[k+1] = x[k] + dt * v[k]      (int_x accumulates via gain_v =  dt * v)
    //   v[k+1] = v[k] - dt * x[k]      (int_v accumulates via gain_x = -dt * x)
    //
    // Initial conditions: x(0) = 1, v(0) = 0   =>   x(t) = cos(omega*t)
    //
    // The x output is compared against HarmonicOscillator at the same frequency
    // (phase = pi/2 so it produces cos).  Euler forward integration accumulates
    // an O(dt) amplitude and phase drift, so the RMS threshold is set accordingly
    // — much looser than the IIR-based DigitalOscillator test.
    //
    // Topology (mirrors classical_oscillator_2integrators.json):
    //   int_v ---> gain_v ---> int_x
    //     ^                      |
    //     +------- gain_x <------+

    constexpr Real omega   = 1.0;    // rad/s
    constexpr Real dt      = 0.001;  // time step [s]  (matches the JSON example)
    constexpr Real gain    = dt * omega;   // = 0.001
    constexpr int  periods = 3;

    const auto samples_per_period =
        static_cast<long>(2.0 * std::numbers::pi_v<Real> / (omega * dt));
    const long total_samples = periods * samples_per_period;

    auto schema = std::make_shared<Schema>(dt);

    // --- 2-integrator oscillator ---
    auto int_x  = std::make_shared<Integrator<Real>>(1.0);                  // x(0) = 1
    auto int_v  = std::make_shared<Integrator<Real>>(0.0);                  // v(0) = 0
    auto gain_v = std::make_shared<Sum<Real>>(std::vector<Real>{  gain });  //  dt * v
    auto gain_x = std::make_shared<Sum<Real>>(std::vector<Real>{ -gain });  // -dt * x

    schema->insert(int_x);
    schema->insert(int_v);
    schema->insert(gain_v);
    schema->insert(gain_x);

    // x[k+1] = x[k] + dt * v[k]
    schema->connect(int_v,  0, gain_v, 0);
    schema->connect(gain_v, 0, int_x,  0);

    // v[k+1] = v[k] - dt * x[k]
    schema->connect(int_x,  0, gain_x, 0);
    schema->connect(gain_x, 0, int_v,  0);

    // --- Reference: HarmonicOscillator producing cos(omega*t) ---
    HarmonicOscillator::Params ho;
    ho.frequency_hz = omega / (2.0 * std::numbers::pi_v<Real>);
    ho.phase_rad    = std::numbers::pi_v<Real> / 2.0;  // sin(wt + pi/2) = cos(wt)
    ho.amplitude    = 1.0;
    auto harmonic = std::make_shared<HarmonicOscillator>(ho);
    schema->insert(harmonic);

    // --- Measure accumulated energy of the difference: int_x vs harmonic ---
    auto energy = std::make_shared<EnergyDifference>();
    schema->insert(energy);

    schema->connect(int_x,    0, energy, 0);
    schema->connect(harmonic, 0, energy, 1);

    schema->run(total_samples);

    const Real total_time = static_cast<Real>(total_samples) * dt;
    const auto e   = readRealOutput(energy);
    const auto rms = std::sqrt(e / total_time);

    // Euler integration accumulates O(dt) amplitude/phase drift per period;
    // threshold is loose enough to accommodate that while catching gross failures.
    require(rms < 5e-2,
            "Classical 2-integrator oscillator RMS mismatch too high: " + std::to_string(rms));
}

void testNetIoRealLoopback()
{
    auto schema = std::make_shared<Schema>(1.0);

    auto constant = std::make_shared<Constant<Real>>(3.25);

    NetIOConfig cfg;
    cfg.endpoint = "ws://127.0.0.1:5577/s7/tests/real-loopback";
    cfg.num_channels = 1;
    cfg.queue_capacity = 16;

    auto sink = std::make_shared<RealNetSink>("net_sink", cfg);
    auto source = std::make_shared<RealNetSource>("net_source", cfg);

    schema->insert(constant);
    schema->insert(sink);
    schema->insert(source);
    schema->connect(constant, 0, sink, 0);

    schema->run(4);

    const auto sample = readRealOutput(source);
    require(nearlyEqual(sample, 3.25, 1e-12), "s7netio real loopback mismatch");
}

} // namespace s7tests

auto main() -> int
{
    try
    {
        s7tests::testFirImpulseResponse();
        s7tests::testIirStepResponse();
        s7tests::testOscillatorAgreement();
        s7tests::testClassicalOscillator2Integrators();
        s7tests::testNetIoRealLoopback();
        std::cout << "s7tests: all DSP/control examples passed\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "s7tests failure: " << ex.what() << '\n';
        return 1;
    }
}

