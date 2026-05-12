//
// s7fileio.cpp — OOP file I/O implementation for s7io
//

#include "s7fileio.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <limits>
#include <boost/json.hpp>
#include <stdexcept>

namespace s7 {

namespace {

void writeBe32(std::ofstream& out, uint32_t v)
{
    uint8_t b[4] = {uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    out.write(reinterpret_cast<const char*>(b), 4);
}

void writeBe64(std::ofstream& out, uint64_t v)
{
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = uint8_t(v >> (56 - i * 8));
    out.write(reinterpret_cast<const char*>(b), 8);
}

void writeBeDouble(std::ofstream& out, double value)
{
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(double));
    writeBe64(out, bits);
}

auto readBe32(std::ifstream& in) -> uint32_t
{
    uint8_t b[4];
    in.read(reinterpret_cast<char*>(b), 4);
    if (!in) throw std::runtime_error("s7fileio: truncated uint32");
    return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
}

auto readBe64(std::ifstream& in) -> uint64_t
{
    uint8_t b[8];
    in.read(reinterpret_cast<char*>(b), 8);
    if (!in) throw std::runtime_error("s7fileio: truncated uint64");
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v = (v << 8) | b[i];
    return v;
}

auto readBeDouble(std::ifstream& in) -> double
{
    const uint64_t bits = readBe64(in);
    double value;
    std::memcpy(&value, &bits, sizeof(double));
    return value;
}

auto scalarCountPerChannel(SignalDomain domain) -> std::size_t
{
    return (domain == SignalDomain::COMPLEX) ? 2u : 1u;
}

class F64Codec final : public IScalarCodec {
public:
    auto encoding() const -> ScalarEncoding override { return ScalarEncoding::F64; }
    auto bytesPerScalar() const -> std::size_t override { return sizeof(double); }
    void encode(double value, std::vector<uint8_t>& dst) const override
    {
        const auto* raw = reinterpret_cast<const uint8_t*>(&value);
        dst.insert(dst.end(), raw, raw + sizeof(double));
    }
    auto decode(const uint8_t* src) const -> double override
    {
        double v;
        std::memcpy(&v, src, sizeof(double));
        return v;
    }
};

class F32Codec final : public IScalarCodec {
public:
    auto encoding() const -> ScalarEncoding override { return ScalarEncoding::F32; }
    auto bytesPerScalar() const -> std::size_t override { return sizeof(float); }
    void encode(double value, std::vector<uint8_t>& dst) const override
    {
        const float v = static_cast<float>(value);
        const auto* raw = reinterpret_cast<const uint8_t*>(&v);
        dst.insert(dst.end(), raw, raw + sizeof(float));
    }
    auto decode(const uint8_t* src) const -> double override
    {
        float v;
        std::memcpy(&v, src, sizeof(float));
        return static_cast<double>(v);
    }
};

template <typename TInt, ScalarEncoding KEncoding>
class IntCodec final : public IScalarCodec {
public:
    auto encoding() const -> ScalarEncoding override { return KEncoding; }
    auto bytesPerScalar() const -> std::size_t override { return sizeof(TInt); }
    void encode(double value, std::vector<uint8_t>& dst) const override
    {
        constexpr double lo = static_cast<double>(std::numeric_limits<TInt>::lowest());
        constexpr double hi = static_cast<double>(std::numeric_limits<TInt>::max());
        const auto clamped = std::clamp(value, lo, hi);
        const TInt v = static_cast<TInt>(std::llround(clamped));
        const auto* raw = reinterpret_cast<const uint8_t*>(&v);
        dst.insert(dst.end(), raw, raw + sizeof(TInt));
    }
    auto decode(const uint8_t* src) const -> double override
    {
        TInt v{};
        std::memcpy(&v, src, sizeof(TInt));
        return static_cast<double>(v);
    }
};

template <typename TUInt, ScalarEncoding KEncoding>
class UIntCodec final : public IScalarCodec {
public:
    auto encoding() const -> ScalarEncoding override { return KEncoding; }
    auto bytesPerScalar() const -> std::size_t override { return sizeof(TUInt); }
    void encode(double value, std::vector<uint8_t>& dst) const override
    {
        constexpr double lo = 0.0;
        constexpr double hi = static_cast<double>(std::numeric_limits<TUInt>::max());
        const auto clamped = std::clamp(value, lo, hi);
        const TUInt v = static_cast<TUInt>(std::llround(clamped));
        const auto* raw = reinterpret_cast<const uint8_t*>(&v);
        dst.insert(dst.end(), raw, raw + sizeof(TUInt));
    }
    auto decode(const uint8_t* src) const -> double override
    {
        TUInt v{};
        std::memcpy(&v, src, sizeof(TUInt));
        return static_cast<double>(v);
    }
};

void writeSidecarJson(const std::filesystem::path& base,
                     const FileHeader& header,
                     const std::string& extension)
{
    std::filesystem::path sidecar = base;
    sidecar.replace_extension(extension);

    boost::json::object j;
    j["format"] = "s7.io.file.v1";
    j["domain"] = signalDomainToString(header.domain);
    j["scalar_encoding"] = scalarEncodingToString(header.encoding);
    j["num_channels"] = header.num_channels;
    j["sample_rate_hz"] = header.sample_rate_hz;
    j["timestamp_unix"] = header.timestamp_unix;
    j["num_samples"] = header.num_samples;
    if (!header.metadata_json.empty()) {
        try {
            j["metadata"] = boost::json::parse(header.metadata_json);
        } catch (...) {
            j["metadata_raw"] = header.metadata_json;
        }
    }

    std::ofstream out(sidecar);
    if (!out) throw std::runtime_error("s7fileio: cannot write sidecar file: " + sidecar.string());
    out << boost::json::serialize(j) << '\n';
}

auto maybeReadSidecarMetadata(const std::filesystem::path& base,
                              const std::string& extension) -> std::string
{
    std::filesystem::path sidecar = base;
    sidecar.replace_extension(extension);
    if (!std::filesystem::exists(sidecar)) return "{}";

    std::ifstream in(sidecar);
    if (!in) return "{}";
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (text.empty()) return "{}";

    const auto j = boost::json::parse(text);
    if (!j.is_object()) return "{}";

    const auto& obj = j.as_object();
    if (const auto* metadata = obj.if_contains("metadata")) return boost::json::serialize(*metadata);
    if (const auto* metadata_raw = obj.if_contains("metadata_raw"); metadata_raw && metadata_raw->is_string()) {
        return boost::json::value_to<std::string>(*metadata_raw);
    }
    return "{}";
}

} // namespace

auto scalarEncodingFromString(std::string_view value) -> ScalarEncoding
{
    if (value == "f64") return ScalarEncoding::F64;
    if (value == "f32") return ScalarEncoding::F32;
    if (value == "s32") return ScalarEncoding::S32;
    if (value == "s16") return ScalarEncoding::S16;
    if (value == "s8")  return ScalarEncoding::S8;
    if (value == "u8")  return ScalarEncoding::U8;
    throw std::runtime_error("s7fileio: unknown scalar encoding: " + std::string(value));
}

auto scalarEncodingToString(ScalarEncoding value) -> std::string
{
    switch (value) {
        case ScalarEncoding::F64: return "f64";
        case ScalarEncoding::F32: return "f32";
        case ScalarEncoding::S32: return "s32";
        case ScalarEncoding::S16: return "s16";
        case ScalarEncoding::S8:  return "s8";
        case ScalarEncoding::U8:  return "u8";
    }
    return "f64";
}

auto signalDomainToString(SignalDomain value) -> std::string
{
    return value == SignalDomain::COMPLEX ? "complex" : "real";
}

auto makeScalarCodec(ScalarEncoding encoding) -> std::unique_ptr<IScalarCodec>
{
    switch (encoding) {
        case ScalarEncoding::F64: return std::make_unique<F64Codec>();
        case ScalarEncoding::F32: return std::make_unique<F32Codec>();
        case ScalarEncoding::S32: return std::make_unique<IntCodec<int32_t, ScalarEncoding::S32>>();
        case ScalarEncoding::S16: return std::make_unique<IntCodec<int16_t, ScalarEncoding::S16>>();
        case ScalarEncoding::S8:  return std::make_unique<IntCodec<int8_t, ScalarEncoding::S8>>();
        case ScalarEncoding::U8:  return std::make_unique<UIntCodec<uint8_t, ScalarEncoding::U8>>();
    }
    throw std::runtime_error("s7fileio: unsupported scalar encoding");
}

void FileHeader::writeBinary(std::ofstream& out) const
{
    if (!out) throw std::runtime_error("s7fileio: output stream not open");
    writeBe32(out, magic);
    writeBe32(out, format_version);
    out.put(static_cast<char>(domain));
    out.put(static_cast<char>(encoding));
    writeBe32(out, num_channels);
    writeBeDouble(out, sample_rate_hz);
    writeBeDouble(out, timestamp_unix);
    writeBe64(out, num_samples);
    out.put(metadata_in_sidecar ? char{1} : char{0});
    writeBe32(out, static_cast<uint32_t>(metadata_json.size()));
    if (!metadata_json.empty()) out.write(metadata_json.data(), static_cast<std::streamsize>(metadata_json.size()));
    if (!out) throw std::runtime_error("s7fileio: header write failed");
}

auto FileHeader::readBinary(std::ifstream& in) -> FileHeader
{
    FileHeader hdr;
    hdr.magic = readBe32(in);
    if (hdr.magic != MAGIC) throw std::runtime_error("s7fileio: invalid file magic");
    hdr.format_version = readBe32(in);
    if (hdr.format_version != FORMAT_VERSION) throw std::runtime_error("s7fileio: unsupported format version");

    uint8_t domain_raw = 0;
    uint8_t encoding_raw = 0;
    in.read(reinterpret_cast<char*>(&domain_raw), 1);
    in.read(reinterpret_cast<char*>(&encoding_raw), 1);
    if (!in) throw std::runtime_error("s7fileio: truncated domain/encoding fields");

    if (domain_raw > 1) throw std::runtime_error("s7fileio: invalid signal domain");
    if (encoding_raw > static_cast<uint8_t>(ScalarEncoding::U8))
        throw std::runtime_error("s7fileio: invalid scalar encoding");

    hdr.domain = static_cast<SignalDomain>(domain_raw);
    hdr.encoding = static_cast<ScalarEncoding>(encoding_raw);
    hdr.num_channels = readBe32(in);
    hdr.sample_rate_hz = readBeDouble(in);
    hdr.timestamp_unix = readBeDouble(in);
    hdr.num_samples = readBe64(in);
    char sidecar = 0;
    in.get(sidecar);
    hdr.metadata_in_sidecar = (sidecar != 0);
    const uint32_t meta_len = readBe32(in);
    if (meta_len > 4 * 1024 * 1024) throw std::runtime_error("s7fileio: metadata too large");
    hdr.metadata_json.resize(meta_len);
    if (meta_len > 0) {
        in.read(hdr.metadata_json.data(), meta_len);
        if (!in) throw std::runtime_error("s7fileio: truncated metadata");
    }
    return hdr;
}

FileSinkBase::FileSinkBase(const std::string& id,
                           SignalDomain domain,
                           ScalarEncoding encoding,
                           uint32_t num_channels,
                           double sample_rate_hz,
                           const std::string& output_path,
                           FileIOOptions options)
    : Element(id), output_path_(output_path), options_(std::move(options)), codec_(makeScalarCodec(encoding))
{
    setTraits(kIsStateful, kHasDirectFeedthrough);
    if (num_channels == 0) throw std::runtime_error("s7fileio: num_channels must be > 0");
    if (sample_rate_hz <= 0.0) throw std::runtime_error("s7fileio: sample_rate_hz must be > 0");

    header_.domain = domain;
    header_.encoding = encoding;
    header_.num_channels = num_channels;
    header_.sample_rate_hz = sample_rate_hz;
    header_.metadata_json = options_.metadata_json;
    header_.metadata_in_sidecar = options_.write_json_sidecar;
}

FileSinkBase::~FileSinkBase()
{
    try {
        flush();
    } catch (...) {
    }
}

void FileSinkBase::tick()
{
    encodeTickInterleaved(payload_bytes_);
    ++sample_count_;
}

void FileSinkBase::flush()
{
    if (flushed_) return;
    flushed_ = true;

    header_.num_samples = sample_count_;
    header_.timestamp_unix = static_cast<double>(std::time(nullptr));
    if (options_.write_json_sidecar) header_.metadata_json = "";

    std::ofstream out(output_path_, std::ios::binary);
    if (!out) throw std::runtime_error("s7fileio: cannot open output file: " + output_path_.string());

    header_.writeBinary(out);
    if (!payload_bytes_.empty()) {
        out.write(reinterpret_cast<const char*>(payload_bytes_.data()),
                  static_cast<std::streamsize>(payload_bytes_.size()));
    }
    out.close();
    if (!out) throw std::runtime_error("s7fileio: failed writing output file");

    if (options_.write_json_sidecar) {
        FileHeader json_header = header_;
        json_header.metadata_json = options_.metadata_json;
        writeSidecarJson(output_path_, json_header, options_.json_sidecar_extension);
    }
}

FileSourceBase::FileSourceBase(const std::string& id,
                               SignalDomain expected_domain,
                               const std::string& input_path)
    : Element(id), input_path_(input_path)
{
    setTraits(kIsStateful, kHasDirectFeedthrough);

    std::ifstream in(input_path_, std::ios::binary);
    if (!in) throw std::runtime_error("s7fileio: cannot open input file: " + input_path);

    header_ = FileHeader::readBinary(in);
    if (header_.domain != expected_domain)
        throw std::runtime_error("s7fileio: source domain mismatch with file domain");

    codec_ = makeScalarCodec(header_.encoding);
    if (header_.metadata_in_sidecar && header_.metadata_json.empty()) {
        header_.metadata_json = maybeReadSidecarMetadata(input_path_, ".json");
    }

    bytes_per_tick_ = header_.num_channels * scalarCountPerChannel(header_.domain) * codec_->bytesPerScalar();
    const std::size_t payload_size = static_cast<std::size_t>(header_.num_samples) * bytes_per_tick_;
    payload_bytes_.resize(payload_size);
    if (payload_size > 0) {
        in.read(reinterpret_cast<char*>(payload_bytes_.data()), static_cast<std::streamsize>(payload_size));
        if (!in || in.gcount() != static_cast<std::streamsize>(payload_size))
            throw std::runtime_error("s7fileio: file payload truncated");
    }
}

FileSourceBase::~FileSourceBase() = default;

void FileSourceBase::tick()
{
    if (current_sample_index_ >= header_.num_samples) return;
    const auto offset = static_cast<std::size_t>(current_sample_index_) * bytes_per_tick_;
    decodeTickInterleaved(payload_bytes_.data() + offset);
    ++current_sample_index_;
}

RealFileSink::RealFileSink(const std::string& id,
                           uint32_t num_channels,
                           ScalarEncoding encoding,
                           double sample_rate_hz,
                           const std::string& output_path,
                           FileIOOptions options)
    : FileSinkBase(id, SignalDomain::REAL, encoding, num_channels, sample_rate_hz, output_path, std::move(options))
{
    inputs_real_.reserve(num_channels);
    for (uint32_t i = 0; i < num_channels; ++i) inputs_real_.push_back(addInput<Real>());
}

void RealFileSink::encodeTickInterleaved(std::vector<uint8_t>& dst)
{
    for (const auto& in : inputs_real_) codec_->encode(in->value(), dst);
}

ComplexFileSink::ComplexFileSink(const std::string& id,
                                 uint32_t num_channels,
                                 ScalarEncoding encoding,
                                 double sample_rate_hz,
                                 const std::string& output_path,
                                 FileIOOptions options)
    : FileSinkBase(id, SignalDomain::COMPLEX, encoding, num_channels, sample_rate_hz, output_path, std::move(options))
{
    inputs_complex_.reserve(num_channels);
    for (uint32_t i = 0; i < num_channels; ++i) inputs_complex_.push_back(addInput<Complex>());
}

void ComplexFileSink::encodeTickInterleaved(std::vector<uint8_t>& dst)
{
    for (const auto& in : inputs_complex_) {
        const auto v = in->value();
        codec_->encode(v.real(), dst);
        codec_->encode(v.imag(), dst);
    }
}

RealFileSource::RealFileSource(const std::string& id, const std::string& input_path)
    : FileSourceBase(id, SignalDomain::REAL, input_path)
{
    values_.assign(header_.num_channels, 0.0);
    for (uint32_t i = 0; i < header_.num_channels; ++i) addOutput<Real>(values_[i]);
}

void RealFileSource::decodeTickInterleaved(const uint8_t* src)
{
    const auto stride = codec_->bytesPerScalar();
    for (uint32_t ch = 0; ch < header_.num_channels; ++ch) {
        values_[ch] = codec_->decode(src + ch * stride);
    }
}

ComplexFileSource::ComplexFileSource(const std::string& id, const std::string& input_path)
    : FileSourceBase(id, SignalDomain::COMPLEX, input_path)
{
    values_.assign(header_.num_channels, Complex{0.0, 0.0});
    for (uint32_t i = 0; i < header_.num_channels; ++i) addOutput<Complex>(values_[i]);
}

void ComplexFileSource::decodeTickInterleaved(const uint8_t* src)
{
    const auto stride = codec_->bytesPerScalar();
    for (uint32_t ch = 0; ch < header_.num_channels; ++ch) {
        const auto base = src + (ch * 2u * stride);
        const double i = codec_->decode(base);
        const double q = codec_->decode(base + stride);
        values_[ch] = Complex{i, q};
    }
}

} // namespace s7


