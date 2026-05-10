//
// s7fileio.hpp — File-based I/O for s7io
//
// This module is intentionally OOP-oriented so we can grow to:
//  - s7fileio: binary + metadata sidecar file formats (this file)
//  - s7memio: shared-memory transports
//  - s7netio: network transports (RSocket, etc.)
//
// It separates Real and Complex pipelines via dedicated classes and supports
// multiple scalar encodings to match SDR ecosystem data formats.
//

#pragma once

#include "s7.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace s7 {

// ============================================================================
// File Format Model
// ============================================================================

/**
 * @brief Signal domain represented by this file stream.
 */
enum class SignalDomain : uint8_t {
    REAL = 0,
    COMPLEX = 1,
};

/**
 * @brief Scalar encoding used for one numeric component.
 *
 * For REAL streams, one sample per channel uses one scalar.
 * For COMPLEX streams, one sample per channel uses two scalars: I then Q.
 */
enum class ScalarEncoding : uint8_t {
    F64 = 0,
    F32 = 1,
    S32 = 2,
    S16 = 3,
    S8  = 4,
    U8  = 5,
};

/**
 * @brief Header metadata for one s7fileio stream.
 */
struct FileHeader {
    static constexpr uint32_t MAGIC = 0x5337494Fu; // "S7IO"
    static constexpr uint32_t FORMAT_VERSION = 1u;

    uint32_t magic = MAGIC;
    uint32_t format_version = FORMAT_VERSION;
    SignalDomain domain = SignalDomain::REAL;
    ScalarEncoding encoding = ScalarEncoding::F64;
    uint32_t num_channels = 1;
    double sample_rate_hz = 48000.0;
    double timestamp_unix = 0.0;
    uint64_t num_samples = 0;
    bool metadata_in_sidecar = false;
    std::string metadata_json = "{}";

    void writeBinary(std::ofstream& out) const;
    static FileHeader readBinary(std::ifstream& in);
};

/**
 * @brief Runtime options used by sinks/sources for metadata and sidecar behavior.
 */
struct FileIOOptions {
    std::string metadata_json = "{}";
    bool write_json_sidecar = false;
    std::string json_sidecar_extension = ".json";
};

class IScalarCodec {
public:
    virtual ~IScalarCodec() = default;
    virtual ScalarEncoding encoding() const = 0;
    virtual std::size_t bytesPerScalar() const = 0;
    virtual void encode(double value, std::vector<uint8_t>& dst) const = 0;
    virtual double decode(const uint8_t* src) const = 0;
};

auto makeScalarCodec(ScalarEncoding encoding) -> std::unique_ptr<IScalarCodec>;

auto scalarEncodingFromString(std::string_view value) -> ScalarEncoding;
auto scalarEncodingToString(ScalarEncoding value) -> std::string;
auto signalDomainToString(SignalDomain value) -> std::string;

// ============================================================================
// Base classes
// ============================================================================

class FileSinkBase : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    ~FileSinkBase() override;

    void tick() override;
    void flush();
    uint64_t sampleCount() const { return sample_count_; }
    const FileHeader& header() const { return header_; }

protected:
    FileSinkBase(const std::string& id,
                 SignalDomain domain,
                 ScalarEncoding encoding,
                 uint32_t num_channels,
                 double sample_rate_hz,
                 const std::string& output_path,
                 FileIOOptions options);

    virtual void encodeTickInterleaved(std::vector<uint8_t>& dst) = 0;

    FileHeader header_;
    std::filesystem::path output_path_;
    FileIOOptions options_;
    std::unique_ptr<IScalarCodec> codec_;
    std::vector<uint8_t> payload_bytes_;
    uint64_t sample_count_ = 0;
    bool flushed_ = false;
};

// ============================================================================
class FileSourceBase : public Element {
public:
    static constexpr bool kIsStateful = true;
    static constexpr bool kHasDirectFeedthrough = false;

    ~FileSourceBase() override;

    void tick() override;

    const FileHeader& header() const { return header_; }
    uint64_t currentSampleIndex() const { return current_sample_index_; }
    bool finished() const { return current_sample_index_ >= header_.num_samples; }

protected:
    FileSourceBase(const std::string& id,
                   SignalDomain expected_domain,
                   const std::string& input_path);

    virtual void decodeTickInterleaved(const uint8_t* src) = 0;

    FileHeader header_;
    std::filesystem::path input_path_;
    std::unique_ptr<IScalarCodec> codec_;
    std::vector<uint8_t> payload_bytes_;
    std::size_t bytes_per_tick_ = 0;
    uint64_t current_sample_index_ = 0;
};

// ============================================================================
// Concrete sinks and sources
// ============================================================================

class RealFileSink final : public FileSinkBase {
public:
    RealFileSink(const std::string& id,
                 uint32_t num_channels,
                 ScalarEncoding encoding,
                 double sample_rate_hz,
                 const std::string& output_path,
                 FileIOOptions options = {});

protected:
    void encodeTickInterleaved(std::vector<uint8_t>& dst) override;

private:
    std::vector<std::shared_ptr<TypedInput<Real>>> inputs_real_;
};

class ComplexFileSink final : public FileSinkBase {
public:
    ComplexFileSink(const std::string& id,
                    uint32_t num_channels,
                    ScalarEncoding encoding,
                    double sample_rate_hz,
                    const std::string& output_path,
                    FileIOOptions options = {});

protected:
    void encodeTickInterleaved(std::vector<uint8_t>& dst) override;

private:
    std::vector<std::shared_ptr<TypedInput<Complex>>> inputs_complex_;
};

class RealFileSource final : public FileSourceBase {
public:
    RealFileSource(const std::string& id, const std::string& input_path);

protected:
    void decodeTickInterleaved(const uint8_t* src) override;

private:
    std::vector<Real> values_;
};

class ComplexFileSource final : public FileSourceBase {
public:
    ComplexFileSource(const std::string& id, const std::string& input_path);

protected:
    void decodeTickInterleaved(const uint8_t* src) override;

private:
    std::vector<Complex> values_;
};

}  // namespace s7



