//
// s7fileio_registry.cpp — Registers s7fileio elements for JSON instantiation
//

#include "s7factory.hpp"
#include "s7fileio.hpp"

#include <cmath>
#include <stdexcept>

namespace s7 {

void registerFileIO(ElementRegistry& registry) {
    auto decodeCommon = [](const ParamMap& params, uint32_t default_channels)
    {
        const auto num_ch_real  = getParamAs<Real>(params, "num_channels", static_cast<Real>(default_channels));
        const auto sample_rate  = getParamAs<Real>(params, "sample_rate_hz", 48000.0);
        const auto encoding_raw = getParamAs<Real>(params, "encoding", static_cast<Real>(ScalarEncoding::F64));

        const auto channels = static_cast<uint32_t>(std::round(num_ch_real));
        if (channels == 0) throw std::runtime_error("fileio: num_channels must be > 0");

        const auto enc_u8 = static_cast<uint8_t>(std::round(encoding_raw));
        if (enc_u8 > static_cast<uint8_t>(ScalarEncoding::U8))
            throw std::runtime_error("fileio: invalid encoding enum");

        struct Parsed { uint32_t channels; double sample_rate; ScalarEncoding encoding; };
        return Parsed{channels, sample_rate, static_cast<ScalarEncoding>(enc_u8)};
    };

    registry.registerType("RealFileSink", [decodeCommon](const std::string& id, const ParamMap& params) -> ElementPtr {
        const auto p = decodeCommon(params, 1);
        return std::make_shared<RealFileSink>(id, p.channels, p.encoding, p.sample_rate, "/tmp/s7_real_sink.s7io");
    });

    registry.registerType("ComplexFileSink", [decodeCommon](const std::string& id, const ParamMap& params) -> ElementPtr {
        const auto p = decodeCommon(params, 1);
        return std::make_shared<ComplexFileSink>(id, p.channels, p.encoding, p.sample_rate, "/tmp/s7_complex_sink.s7io");
    });

    registry.registerType("RealFileSource", [](const std::string&, const ParamMap&) -> ElementPtr {
        throw std::runtime_error("RealFileSource JSON factory needs string path support. Use programmatic creation.");
    });

    registry.registerType("ComplexFileSource", [](const std::string&, const ParamMap&) -> ElementPtr {
        throw std::runtime_error("ComplexFileSource JSON factory needs string path support. Use programmatic creation.");
    });

    // Legacy FileSource/FileSink aliases intentionally removed.
}

}  // namespace s7


