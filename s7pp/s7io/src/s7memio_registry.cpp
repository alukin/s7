//
// s7memio_registry.cpp — Registers s7memio elements for JSON instantiation
//

#include "s7factory.hpp"
#include "s7memio.hpp"

#include <cmath>
#include <stdexcept>

namespace s7 {

void registerMemIO(ElementRegistry& registry)
{
    auto parseConfig = [](const ParamMap& params) -> MemIOConfig {
        MemIOConfig cfg;
        cfg.num_channels = static_cast<uint32_t>(std::round(getParamAs<Real>(params, "num_channels", 1.0)));
        cfg.block_size = static_cast<uint32_t>(std::round(getParamAs<Real>(params, "block_size", 64.0)));
        cfg.capacity_blocks = static_cast<uint32_t>(std::round(getParamAs<Real>(params, "capacity_blocks", 64.0)));
        cfg.sample_rate_hz = getParamAs<Real>(params, "sample_rate_hz", 48000.0);
        const auto encoding = static_cast<uint32_t>(std::round(getParamAs<Real>(params, "encoding", 0.0)));
        cfg.shm_name = "/s7_mem_stream";

        if (encoding > static_cast<uint32_t>(MemScalarEncoding::U8)) {
            throw std::runtime_error("s7memio: encoding must be enum 0..3 (F32,S16,S8,U8)");
        }
        cfg.encoding = static_cast<MemScalarEncoding>(encoding);

        if (cfg.num_channels == 0 || cfg.block_size == 0 || cfg.capacity_blocks == 0) {
            throw std::runtime_error("s7memio: num_channels, block_size, and capacity_blocks must be > 0");
        }
        return cfg;
    };

    registry.registerType("RealMemSink", [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
        auto cfg = parseConfig(params);
        cfg.create_if_missing = true;
        return std::make_shared<RealMemSink>(id, cfg);
    });

    registry.registerType("RealMemSource", [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
        auto cfg = parseConfig(params);
        cfg.create_if_missing = false;
        return std::make_shared<RealMemSource>(id, cfg);
    });

    registry.registerType("ComplexMemSink", [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
        auto cfg = parseConfig(params);
        cfg.create_if_missing = true;
        return std::make_shared<ComplexMemSink>(id, cfg);
    });

    registry.registerType("ComplexMemSource", [parseConfig](const std::string& id, const ParamMap& params) -> ElementPtr {
        auto cfg = parseConfig(params);
        cfg.create_if_missing = false;
        return std::make_shared<ComplexMemSource>(id, cfg);
    });
}

} // namespace s7


