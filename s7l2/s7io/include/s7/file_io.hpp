#ifndef S7IO_INCLUDE_S7_FILE_IO_HPP
#define S7IO_INCLUDE_S7_FILE_IO_HPP

#include "s7/element.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace s7::io {
namespace detail {

template <typename T>
struct SignalTraits;

template <>
struct SignalTraits<RealValue> {
    static constexpr SignalType type = SignalType::F64;
    static constexpr std::string_view name = signal_type_name(SignalType::F64);
};

template <>
struct SignalTraits<ComplexValue> {
    static constexpr SignalType type = SignalType::C64;
    static constexpr std::string_view name = signal_type_name(SignalType::C64);
};

template <typename T>
void write_value(std::ostream& out, const T& value);

template <>
inline void write_value<RealValue>(std::ostream& out, const RealValue& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
}

template <>
inline void write_value<ComplexValue>(std::ostream& out, const ComplexValue& value) {
    const auto real = value.real();
    const auto imag = value.imag();
    out.write(reinterpret_cast<const char*>(&real), sizeof(real));
    out.write(reinterpret_cast<const char*>(&imag), sizeof(imag));
}

template <typename T>
void read_value(std::istream& in, T& value);

template <>
inline void read_value<RealValue>(std::istream& in, RealValue& value) {
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
}

template <>
inline void read_value<ComplexValue>(std::istream& in, ComplexValue& value) {
    RealValue real{};
    RealValue imag{};
    in.read(reinterpret_cast<char*>(&real), sizeof(real));
    in.read(reinterpret_cast<char*>(&imag), sizeof(imag));
    value = ComplexValue{real, imag};
}

inline void throw_bad_format(const std::string& message) {
    throw std::runtime_error("S7 IO format error: " + message);
}

inline auto trim_left(const std::string& line) -> std::string {
    const auto it = line.find_first_not_of(" \t\r");
    if (it == std::string::npos) {
        return {};
    }
    return line.substr(it);
}

} // namespace detail

template <typename T>
class SampleFileFormat {
public:
    virtual ~SampleFileFormat() = default;

    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
    virtual auto write_header(std::ostream& out) const -> void = 0;
    virtual auto read_header(std::istream& in) const -> void = 0;
    virtual auto write_sample(std::ostream& out, std::uint64_t sample_index, const T& value) const -> void = 0;
    [[nodiscard]] virtual auto read_sample(std::istream& in, std::uint64_t& sample_index, T& value) const -> bool = 0;
};

template <typename T>
class CompactBinaryFileFormat final : public SampleFileFormat<T> {
public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override {
        return "compact-binary";
    }

    auto write_header(std::ostream& out) const -> void override {
        constexpr std::array<char, 4> magic{'S', '7', 'B', '1'};
        out.write(magic.data(), static_cast<std::streamsize>(magic.size()));

        // Version 2: the signal type is its 16-bit wire type id, little-endian.
        const auto version = std::uint8_t{2};
        const auto type = static_cast<std::uint16_t>(detail::SignalTraits<T>::type);
        const std::array<std::uint8_t, 2> type_bytes{static_cast<std::uint8_t>(type & 0xFFu),
                                                     static_cast<std::uint8_t>(type >> 8)};
        out.write(reinterpret_cast<const char*>(&version), sizeof(version));
        out.write(reinterpret_cast<const char*>(type_bytes.data()), type_bytes.size());

        if (!out) {
            detail::throw_bad_format("failed to write compact binary header");
        }
    }

    auto read_header(std::istream& in) const -> void override {
        constexpr std::array<char, 4> expected_magic{'S', '7', 'B', '1'};
        std::array<char, 4> magic{};
        in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        if (!in || magic != expected_magic) {
            detail::throw_bad_format("invalid compact binary magic");
        }

        std::uint8_t version{};
        std::array<std::uint8_t, 2> type_bytes{};
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(type_bytes.data()), type_bytes.size());
        if (!in) {
            detail::throw_bad_format("truncated compact binary header");
        }
        if (version != 2) {
            detail::throw_bad_format("unsupported compact binary version");
        }
        const auto type = static_cast<std::uint16_t>(type_bytes[0] | (type_bytes[1] << 8));
        if (type != static_cast<std::uint16_t>(detail::SignalTraits<T>::type)) {
            detail::throw_bad_format("signal type does not match compact binary file");
        }
    }

    auto write_sample(std::ostream& out, std::uint64_t sample_index, const T& value) const -> void override {
        out.write(reinterpret_cast<const char*>(&sample_index), sizeof(sample_index));
        detail::write_value<T>(out, value);
        if (!out) {
            detail::throw_bad_format("failed to write compact binary sample");
        }
    }

    [[nodiscard]] auto read_sample(std::istream& in, std::uint64_t& sample_index, T& value) const -> bool override {
        in.read(reinterpret_cast<char*>(&sample_index), sizeof(sample_index));
        if (!in) {
            return false;
        }
        detail::read_value<T>(in, value);
        if (!in) {
            detail::throw_bad_format("truncated compact binary sample");
        }
        return true;
    }
};

template <typename T>
class GnuplotFileFormat final : public SampleFileFormat<T> {
public:
    [[nodiscard]] auto name() const noexcept -> std::string_view override {
        return "gnuplot-text";
    }

    auto write_header(std::ostream& out) const -> void override {
        out << "# s7 gnuplot sample file\n";
        out << "# format=" << name() << '\n';
        out << "# signal=" << detail::SignalTraits<T>::name << '\n';
        if (!out) {
            detail::throw_bad_format("failed to write gnuplot header");
        }
    }

    auto read_header(std::istream& in) const -> void override {
        std::string line;

        if (!std::getline(in, line) || detail::trim_left(line) != "# s7 gnuplot sample file") {
            detail::throw_bad_format("invalid gnuplot header");
        }
        if (!std::getline(in, line) || detail::trim_left(line) != "# format=gnuplot-text") {
            detail::throw_bad_format("missing gnuplot format marker");
        }
        if (!std::getline(in, line) || detail::trim_left(line) != std::string("# signal=") + std::string(detail::SignalTraits<T>::name)) {
            detail::throw_bad_format("signal type does not match gnuplot file");
        }
    }

    auto write_sample(std::ostream& out, std::uint64_t sample_index, const T& value) const -> void override {
        out << sample_index << ' ';
        write_sample_value(out, value);
        out << '\n';
        if (!out) {
            detail::throw_bad_format("failed to write gnuplot sample");
        }
    }

    [[nodiscard]] auto read_sample(std::istream& in, std::uint64_t& sample_index, T& value) const -> bool override {
        std::string line;
        while (std::getline(in, line)) {
            line = detail::trim_left(line);
            if (line.empty() || line.front() == '#') {
                continue;
            }

            std::istringstream parser(line);
            parser >> sample_index;
            if (!parser) {
                detail::throw_bad_format("failed to parse gnuplot sample index");
            }
            read_sample_value(parser, value);
            if (!parser) {
                detail::throw_bad_format("failed to parse gnuplot sample value");
            }
            return true;
        }
        return false;
    }

private:
    static void write_sample_value(std::ostream& out, const RealValue value) {
        out << std::setprecision(std::numeric_limits<RealValue>::max_digits10) << value;
    }

    static void write_sample_value(std::ostream& out, const ComplexValue& value) {
        out << std::setprecision(std::numeric_limits<RealValue>::max_digits10)
            << value.real() << ' ' << value.imag();
    }

    static void read_sample_value(std::istringstream& parser, RealValue& value) {
        parser >> value;
    }

    static void read_sample_value(std::istringstream& parser, ComplexValue& value) {
        RealValue real{};
        RealValue imag{};
        parser >> real >> imag;
        value = ComplexValue{real, imag};
    }
};

template <typename T>
[[nodiscard]] inline auto make_compact_binary_format() -> std::unique_ptr<SampleFileFormat<T>> {
    return std::make_unique<CompactBinaryFileFormat<T>>();
}

template <typename T>
[[nodiscard]] inline auto make_gnuplot_format() -> std::unique_ptr<SampleFileFormat<T>> {
    return std::make_unique<GnuplotFileFormat<T>>();
}

template <typename T>
class FileSinkElement : public Element {
public:
    FileSinkElement(std::string_view type_name,
                    std::filesystem::path file_path,
                    std::unique_ptr<SampleFileFormat<T>> format,
                    std::ios::openmode mode)
        : Element(std::move(type_name)),
          file_path_(std::move(file_path)),
          format_(std::move(format)),
          stream_(file_path_, std::ios::out | mode)
    {
        if (!format_) {
            throw std::invalid_argument("File sink requires a format.");
        }
        if (!stream_) {
            throw std::runtime_error("Could not open file for writing: '" + file_path_.string() + "'.");
        }
        register_input(0, port_names::in, detail::SignalTraits<T>::type, nullptr);
        format_->write_header(stream_);
    }

    void step() override {
        const auto value = read_input<T>(0);
        format_->write_sample(stream_, sample_index_++, value);
        stream_.flush();
    }

protected:
    std::filesystem::path file_path_;
    std::unique_ptr<SampleFileFormat<T>> format_;
    std::ofstream stream_;
    std::uint64_t sample_index_{0};
};

template <typename T>
class FileSourceElement : public Element {
public:
    FileSourceElement(std::string_view type_name,
                      std::filesystem::path file_path,
                      std::unique_ptr<SampleFileFormat<T>> format,
                      std::ios::openmode mode)
        : Element(std::move(type_name)),
          file_path_(std::move(file_path)),
          format_(std::move(format)),
          stream_(file_path_, std::ios::in | mode)
    {
        if (!format_) {
            throw std::invalid_argument("File source requires a format.");
        }
        if (!stream_) {
            throw std::runtime_error("Could not open file for reading: '" + file_path_.string() + "'.");
        }
        format_->read_header(stream_);
        register_output(0, port_names::out, detail::SignalTraits<T>::type, &output_value_);
    }

    void step() override {
        if (!format_->read_sample(stream_, sample_index_, output_value_)) {
            throw std::runtime_error("Unexpected end of file while reading: '" + file_path_.string() + "'.");
        }
    }

protected:
    std::filesystem::path file_path_;
    std::unique_ptr<SampleFileFormat<T>> format_;
    std::ifstream stream_;
    T output_value_{};
    std::uint64_t sample_index_{0};
};

template <typename T>
class CompactBinaryFileSink final : public FileSinkElement<T> {
public:
    explicit CompactBinaryFileSink(std::filesystem::path file_path)
        : FileSinkElement<T>("CompactBinaryFileSink",
                             std::move(file_path),
                             make_compact_binary_format<T>(),
                             std::ios::binary | std::ios::trunc)
    {}
};

template <typename T>
class CompactBinaryFileSource final : public FileSourceElement<T> {
public:
    explicit CompactBinaryFileSource(std::filesystem::path file_path)
        : FileSourceElement<T>("CompactBinaryFileSource",
                               std::move(file_path),
                               make_compact_binary_format<T>(),
                               std::ios::binary)
    {}
};

template <typename T>
class GnuplotFileSink final : public FileSinkElement<T> {
public:
    explicit GnuplotFileSink(std::filesystem::path file_path)
        : FileSinkElement<T>("GnuplotFileSink",
                             std::move(file_path),
                             make_gnuplot_format<T>(),
                             std::ios::trunc)
    {}
};

template <typename T>
class GnuplotFileSource final : public FileSourceElement<T> {
public:
    explicit GnuplotFileSource(std::filesystem::path file_path)
        : FileSourceElement<T>("GnuplotFileSource",
                               std::move(file_path),
                               make_gnuplot_format<T>(),
                               std::ios::in)
    {}
};

using RealCompactBinaryFileSink = CompactBinaryFileSink<RealValue>;
using RealCompactBinaryFileSource = CompactBinaryFileSource<RealValue>;
using ComplexCompactBinaryFileSink = CompactBinaryFileSink<ComplexValue>;
using ComplexCompactBinaryFileSource = CompactBinaryFileSource<ComplexValue>;

using RealGnuplotFileSink = GnuplotFileSink<RealValue>;
using RealGnuplotFileSource = GnuplotFileSource<RealValue>;
using ComplexGnuplotFileSink = GnuplotFileSink<ComplexValue>;
using ComplexGnuplotFileSource = GnuplotFileSource<ComplexValue>;

} // namespace s7::io

#endif // S7IO_INCLUDE_S7_FILE_IO_HPP
