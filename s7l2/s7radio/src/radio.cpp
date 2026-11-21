// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "radio.hpp"
#include <s7/element_registry.hpp>
#include <s7/float_types.hpp>
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
