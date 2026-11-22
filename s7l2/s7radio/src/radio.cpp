// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "radio.hpp"
#include <s7/element_registry.hpp>
#include <s7/float_types.hpp>
#include <algorithm>
#include <numbers>

namespace s7::radio {
namespace {
constexpr double turn = 2 * std::numbers::pi;

std::complex<double> finite_iq(std::complex<float> sample) {
    const std::complex<double> current{sample.real(), sample.imag()};
    if (math::isnan(current.real()) || math::isinf(current.real()) ||
        math::isnan(current.imag()) || math::isinf(current.imag())) {
        throw std::domain_error("Radio demodulation requires finite I/Q input");
    }
    return current;
}

double fm_phase_scale(Rate rate, double deviation) {
    if (!(deviation > 0 && deviation < rate.to_hz() / 2)) {
        throw std::invalid_argument("FM deviation must be positive and below half the sample rate");
    }
    return turn * (deviation / rate.to_hz());
}

class FmDiscriminator final : public Element {
public:
    FmDiscriminator() : FmDiscriminator("FmDiscriminator", "phase_step", std::nullopt, 1) {}
    FmDiscriminator(Rate rate, double deviation)
        : FmDiscriminator("FmDemodulator", "message", rate, fm_phase_scale(rate, deviation)) {}

    std::optional<Rate> declared_rate() const override { return rate_; }
    void step() override {
        const auto current = finite_iq(read_input<std::complex<float>>(0));
        const auto product = current * std::conj(previous_);
        output_ = product == std::complex<double>{} ? 0.0 : math::atan2(product.imag(), product.real()) / phase_scale_;
        previous_ = current;
    }
private:
    FmDiscriminator(const char* type, const char* output, std::optional<Rate> rate, double phase_scale)
        : Element(type, true), rate_(rate), phase_scale_(phase_scale) {
        register_input(0, "iq", SignalType::C32, nullptr);
        register_output(0, output, SignalType::F64, &output_);
    }
    std::optional<Rate> rate_;
    double phase_scale_;
    std::complex<double> previous_{};
    double output_{};
};

class FmModulator final : public Element {
public:
    FmModulator(Rate rate, double deviation)
        : Element("FmModulator", true), rate_(rate), phase_scale_(fm_phase_scale(rate, deviation)) {
        register_input(0, "message", SignalType::F64, nullptr);
        register_output(0, "iq", SignalType::C32, &output_);
    }
    std::optional<Rate> declared_rate() const override { return rate_; }
    void step() override {
        const double increment = phase_scale_ * read_input<double>(0);
        if (!(math::abs(increment) < turn / 2)) {
            throw std::domain_error("FM message exceeds the unambiguous phase step");
        }
        phase_ += increment;
        if (phase_ >= turn / 2) phase_ -= turn;
        if (phase_ < -turn / 2) phase_ += turn;
        output_ = {static_cast<float>(math::cos(phase_)), static_cast<float>(math::sin(phase_))};
    }
private:
    Rate rate_;
    double phase_scale_;
    double phase_{};
    std::complex<float> output_{};
};

class FmStereoDecoder final : public Element {
public:
    FmStereoDecoder(Rate rate, double deemphasis_us)
        : Element("FmStereoDecoder", true), rate_(rate),
          rotation_(math::cos(turn * 19000 / rate.to_hz()), math::sin(turn * 19000 / rate.to_hz())),
          pilot_alpha_(1 - math::exp(-turn * 30 / rate.to_hz())),
          blend_alpha_(1 - math::exp(-1 / (0.05 * rate.to_hz()))),
          dc_decay_(math::exp(-turn * 30 / rate.to_hz())),
          deemphasis_alpha_(1 - math::exp(-1e6 / (deemphasis_us * rate.to_hz()))),
          notch_decay_(math::exp(-turn * 1000 / rate.to_hz())) {
        if (rate.to_hz() < 160000 || (deemphasis_us != 50 && deemphasis_us != 75))
            throw std::invalid_argument("FM stereo requires at least 160 kHz and 50 or 75 µs de-emphasis");
        register_input(0, "multiplex", SignalType::F64, nullptr);
        register_output(0, "left", SignalType::F32, &left_.output);
        register_output(1, "right", SignalType::F32, &right_.output);
    }

    std::optional<Rate> declared_rate() const override { return rate_; }

    void step() override {
        const double input = read_input<double>(0);
        if (math::isnan(input) || math::isinf(input))
            throw std::domain_error("FM stereo requires a finite multiplex sample");
        dc_blocked_ = dc_decay_ * dc_blocked_ + input - previous_;
        previous_ = input;

        // Coherent detection rejects the mono and stereo sidebands. The pilot
        // phasor uses a cosine reference. Its squared negative imaginary part
        // recovers the ITU-R BS.450 sine-phase 38 kHz subcarrier.
        pilot_baseband_ += pilot_alpha_ * (dc_blocked_ * std::conj(oscillator_) - pilot_baseband_);
        pilot_ += pilot_alpha_ * (pilot_baseband_ - pilot_);
        const double power = std::norm(pilot_);
        const double strength = 2 * math::sqrt(power);
        const auto phasor = oscillator_ * pilot_;
        oscillator_ *= rotation_;
        if (++oscillator_steps_ == 4096) {
            oscillator_ /= math::sqrt(std::norm(oscillator_));
            oscillator_steps_ = 0;
        }
        const double target = std::clamp((strength - 0.02) / 0.02, 0.0, 1.0);
        blend_ += blend_alpha_ * (target - blend_);
        const double difference = power > 1e-8
            ? -4 * dc_blocked_ * phasor.real() * phasor.imag() / power : 0;
        left_.filter(dc_blocked_ + blend_ * difference, rotation_.real(), notch_decay_, deemphasis_alpha_);
        right_.filter(dc_blocked_ - blend_ * difference, rotation_.real(), notch_decay_, deemphasis_alpha_);
    }

private:
    struct Channel {
        // Remove the pilot and the RDS image at 19 kHz before audio resampling.
        void filter(double input, double cosine, double radius, double alpha) {
            const double notch = input - 2 * cosine * input1 + input2
                + 2 * radius * cosine * notch1 - radius * radius * notch2;
            input2 = input1;
            input1 = input;
            notch2 = notch1;
            notch1 = notch;
            deemphasized += alpha * (radius * notch - deemphasized);
            output = static_cast<float>(deemphasized);
        }
        double input1{}, input2{}, notch1{}, notch2{}, deemphasized{};
        float output{};
    };

    Rate rate_;
    std::complex<double> rotation_, oscillator_{1, 0}, pilot_baseband_{}, pilot_{};
    unsigned oscillator_steps_{};
    double pilot_alpha_, blend_alpha_, dc_decay_, deemphasis_alpha_, notch_decay_;
    double previous_{}, dc_blocked_{}, blend_{};
    Channel left_, right_;
};

class AmModulator final : public Element {
public:
    explicit AmModulator(double depth) : Element("AmModulator", false), depth_(depth) {
        register_input(0, "message", SignalType::F64, nullptr);
        register_output(0, "iq", SignalType::C32, &output_);
    }
    void step() override {
        const double message = read_input<double>(0);
        if (!(math::abs(message) <= 1)) throw std::domain_error("AM message must be between -1 and 1");
        output_ = {static_cast<float>((1 + depth_ * message) / (1 + depth_)), 0};
    }
private:
    double depth_;
    std::complex<float> output_{};
};

class AmDemodulator final : public Element {
public:
    explicit AmDemodulator(double depth) : Element("AmDemodulator", false), depth_(depth) {
        if (!(depth > 0 && depth <= 1)) {
            throw std::invalid_argument("AM demodulation depth must be greater than zero and at most one");
        }
        register_input(0, "iq", SignalType::C32, nullptr);
        register_output(0, "message", SignalType::F64, &output_);
    }
    void step() override {
        const auto sample = finite_iq(read_input<std::complex<float>>(0));
        output_ = ((1 + depth_) * math::abs(sample) - 1) / depth_;
    }
private:
    double depth_;
    double output_{};
};

ElementMetadata modulator_metadata(std::string type, std::string name, std::string description) {
    ElementMetadata metadata;
    metadata.type_name = std::move(type);
    metadata.display_name = std::move(name);
    metadata.description = std::move(description);
    metadata.library = "s7radio";
    metadata.category = "radio";
    metadata.version = "1.0.0";
    metadata.input_ports = {{"message", "Normalized real modulation signal", SignalType::F64}};
    metadata.output_ports = {{"iq", "Complex baseband sample", SignalType::C32}};
    return metadata;
}

ElementMetadata demodulator_metadata(const ElementMetadata& modulator, std::string type, std::string name,
                                     std::string description) {
    auto metadata = modulator;
    metadata.type_name = std::move(type);
    metadata.display_name = std::move(name);
    metadata.description = std::move(description);
    metadata.tags = {"demodulation", "quadrature"};
    metadata.input_ports = {{"iq", "Complex baseband sample", SignalType::C32}};
    metadata.output_ports = {{"message", "Recovered normalized real message", SignalType::F64}};
    return metadata;
}

Rate fm_rate(const ElementCreationContext& ctx) {
    return Rate::reduced(static_cast<std::uint64_t>(ctx.get_int("sample_rate_hz")), 1);
}

}

void register_elements(ElementRegistry& registry) {
    if (registry.find("FmDiscriminator")) return;
    ElementMetadata metadata;
    metadata.type_name = "FmDiscriminator";
    metadata.display_name = "FM Discriminator";
    metadata.description = "Signed phase change (radians per sample) between adjacent C32 I/Q samples. "
        "Outputs zero at startup or if either sample has zero amplitude. Direct feedthrough with one sample of history.";
    metadata.library = "s7radio";
    metadata.category = "radio";
    metadata.version = "1.0.0";
    metadata.tags = {"FM", "demodulation", "quadrature"};
    metadata.input_ports = {{"iq", "Complex baseband sample", SignalType::C32}};
    metadata.output_ports = {{"phase_step", "Phase change in radians per sample", SignalType::F64}};
    registry.register_element(ElementDescriptor("FmDiscriminator",
        [](const ElementCreationContext&) { return std::make_unique<FmDiscriminator>(); }, metadata));
    auto fm = modulator_metadata("FmModulator", "FM Modulator",
        "Unit-amplitude, zero-IF complex FM. Advances phase by 2π times deviation in Hz times message, divided by sample rate.");
    fm.parameters = {
        {.name = "sample_rate_hz", .type = "int", .description = "Exact integer sample rate",
         .default_value = 1200000, .min_value = 1, .max_value = 1000000000, .enum_values = {}},
        {.name = "deviation_hz", .type = "real", .description = "Frequency deviation in Hz at unit message",
         .default_value = 75000, .min_value = 1, .max_value = {}, .enum_values = {}}
    };
    registry.register_element(ElementDescriptor("FmModulator", [](const ElementCreationContext& ctx) {
        return std::make_unique<FmModulator>(fm_rate(ctx), ctx.get_real("deviation_hz"));
    }, fm));
    auto fm_demod = demodulator_metadata(fm, "FmDemodulator", "FM Demodulator",
        "Recovers the normalized message from zero-IF I/Q using adjacent phase steps. Match the sample rate "
        "and deviation to the modulator. Outputs zero on the first sample or when either I/Q sample is zero.");
    registry.register_element(ElementDescriptor("FmDemodulator", [](const ElementCreationContext& ctx) {
        return std::make_unique<FmDiscriminator>(fm_rate(ctx), ctx.get_real("deviation_hz"));
    }, fm_demod));

    auto stereo = fm_demod;
    stereo.type_name = "FmStereoDecoder";
    stereo.display_name = "FM Stereo Decoder";
    stereo.description = "Recovers left and right audio from a normalized broadcast FM multiplex. "
        "Tracks the 19 kHz stereo pilot, blends to mono as the pilot fades and applies audio de-emphasis. "
        "Resample both outputs to the audio device rate.";
    stereo.input_ports = {{"multiplex", "Normalized FM multiplex", SignalType::F64}};
    stereo.output_ports = {{"left", "Left audio", SignalType::F32}, {"right", "Right audio", SignalType::F32}};
    stereo.parameters.front().default_value = 300000;
    stereo.parameters.front().min_value = 160000;
    stereo.parameters.back() = {.name = "deemphasis_us", .type = "real",
        .description = "Broadcast de-emphasis time constant, 50 or 75 microseconds",
        .default_value = 50, .min_value = 50, .max_value = 75, .enum_values = {}};
    registry.register_element(ElementDescriptor("FmStereoDecoder", [](const ElementCreationContext& ctx) {
        return std::make_unique<FmStereoDecoder>(fm_rate(ctx), ctx.get_real("deemphasis_us"));
    }, stereo));

    auto am = modulator_metadata("AmModulator", "AM Modulator",
        "Full-carrier double-sideband AM at zero IF. A normalized message in [-1, 1] produces "
        "an envelope (1 + depth × message) / (1 + depth).");
    am.stateless = true;
    am.parameters = {{.name = "depth", .type = "real", .description = "Modulation depth from 0 to 1",
                      .default_value = 0.8, .min_value = 0, .max_value = 1, .enum_values = {}}};
    registry.register_element(ElementDescriptor("AmModulator", [](const ElementCreationContext& ctx) {
        return std::make_unique<AmModulator>(ctx.get_real("depth"));
    }, am));
    auto am_demod = demodulator_metadata(am, "AmDemodulator", "AM Demodulator",
        "Recovers the normalized message from the full-carrier zero-IF I/Q envelope. Requires the modulator's "
        "amplitude normalization and a matching, nonzero modulation depth.");
    am_demod.parameters.front().description = "Matching modulation depth, greater than zero and at most one";
    registry.register_element(ElementDescriptor("AmDemodulator", [](const ElementCreationContext& ctx) {
        return std::make_unique<AmDemodulator>(ctx.get_real("depth"));
    }, am_demod));
}
}
