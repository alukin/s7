// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "radio.hpp"
#include <s7/element_registry.hpp>
#include <s7/float_types.hpp>
#include <numbers>

namespace s7::radio {
namespace {
class FmDiscriminator final : public Element {
public:
    FmDiscriminator() : Element("FmDiscriminator", true) {
        register_input(0, "iq", SignalType::C32, nullptr);
        register_output(0, "phase_step", SignalType::F64, &output_);
    }

    void step() override {
        const auto sample = read_input<std::complex<float>>(0);
        const std::complex<double> current{sample.real(), sample.imag()};
        if (math::isnan(current.real()) || math::isinf(current.real()) ||
            math::isnan(current.imag()) || math::isinf(current.imag())) {
            throw std::domain_error("FM discriminator input must be finite");
        }
        const auto product = current * std::conj(previous_);
        output_ = product == std::complex<double>{} ? 0.0 : math::atan2(product.imag(), product.real());
        previous_ = current;
    }
private:
    std::complex<double> previous_{};
    double output_{};
};

class FmModulator final : public Element {
public:
    FmModulator(Rate rate, double deviation)
        : Element("FmModulator", true), rate_(rate), phase_scale_(turn * (deviation / rate.to_hz())) {
        if (deviation >= rate.to_hz() / 2) {
            throw std::invalid_argument("FM deviation must be below half the sample rate");
        }
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
    static constexpr double turn = 2 * std::numbers::pi;
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
        return std::make_unique<FmModulator>(Rate::reduced(static_cast<std::uint64_t>(ctx.get_int("sample_rate_hz")), 1),
                                             ctx.get_real("deviation_hz"));
    }, fm));

    auto am = modulator_metadata("AmModulator", "AM Modulator",
        "Full-carrier double-sideband AM at zero IF. A normalized message in [-1, 1] produces "
        "an envelope (1 + depth × message) / (1 + depth).");
    am.stateless = true;
    am.parameters = {{.name = "depth", .type = "real", .description = "Modulation depth from 0 to 1",
                      .default_value = 0.8, .min_value = 0, .max_value = 1, .enum_values = {}}};
    registry.register_element(ElementDescriptor("AmModulator", [](const ElementCreationContext& ctx) {
        return std::make_unique<AmModulator>(ctx.get_real("depth"));
    }, am));
}
}
