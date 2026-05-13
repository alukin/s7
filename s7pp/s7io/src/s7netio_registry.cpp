//
// s7netio_registry.cpp — Registers s7netio elements for JSON instantiation
//

#include "s7factory.hpp"
#include "s7netio.hpp"

#include <cmath>
#include <stdexcept>

namespace s7 {

void registerNetIO(ElementRegistry& registry)
{
    auto parseConfig = [](const ParamMap& params) -> NetIOConfig {
        NetIOConfig cfg;
        cfg.num_channels = static_cast<uint32_t>(std::round(getParamAs<Real>(params, "num_channels", 1.0)));
        cfg.queue_capacity = static_cast<uint32_t>(std::round(getParamAs<Real>(params, "queue_capacity", 4096.0)));
        cfg.zero_on_underrun = getParamAs<Real>(params, "zero_on_underrun", 1.0) != 0.0;
        cfg.endpoint = getParamAs<std::string>(params, "endpoint", cfg.endpoint);
        if (cfg.num_channels == 0 || cfg.queue_capacity == 0) {
            throw std::runtime_error("s7netio: num_channels and queue_capacity must be > 0");
        }
        return cfg;
    };

    registry.registerType(
        TypeDescriptor{
            .type_name            = "RealNetSink",
            .category             = "I/O",
            .description          = "Write N-channel real stream to network",
            .default_input_count  = 1,
            .default_output_count = 0,
        },
        [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
            return std::make_shared<RealNetSink>(id, parseConfig(params));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "RealNetSource",
            .category             = "I/O",
            .description          = "Read N-channel real stream from network",
            .default_input_count  = 0,
            .default_output_count = 1,
        },
        [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
            return std::make_shared<RealNetSource>(id, parseConfig(params));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "ComplexNetSink",
            .category             = "I/O",
            .description          = "Write N-channel complex stream to network",
            .default_input_count  = 1,
            .default_output_count = 0,
        },
        [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
            return std::make_shared<ComplexNetSink>(id, parseConfig(params));
        });

    registry.registerType(
        TypeDescriptor{
            .type_name            = "ComplexNetSource",
            .category             = "I/O",
            .description          = "Read N-channel complex stream from network",
            .default_input_count  = 0,
            .default_output_count = 1,
        },
        [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
            return std::make_shared<ComplexNetSource>(id, parseConfig(params));
        });
}

} // namespace s7
