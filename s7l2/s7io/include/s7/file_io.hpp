#ifndef S7IO_INCLUDE_S7_FILE_IO_HPP
#define S7IO_INCLUDE_S7_FILE_IO_HPP

#include "s7/element.hpp"
#include <boost/json.hpp>

#include <array>
#include <bit>
#include <charconv>
#include <locale>
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

inline void throw_bad_format(const std::string& message) {
    throw std::runtime_error("S7 IO format error: " + message);
}

inline std::string recording_description(const std::string& text) {
    if (text.size()>1024) throw std::invalid_argument("Recording description must be at most 1024 UTF-8 bytes.");
    // Parsing validates UTF-8; JSON escaping also makes text-header descriptions
    // single-line even when they contain quotes or line breaks.
    const auto value=boost::json::parse(boost::json::serialize(boost::json::value(text)));
    return std::string(value.as_string());
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
    explicit SampleFileFormat(Rate rate = Rate::from_hz(1000), std::string description = {})
        : rate_(Rate::reduced(rate.numerator,rate.denominator)), description_(detail::recording_description(description)) {}
    virtual ~SampleFileFormat() = default;
    [[nodiscard]] Rate rate() const { return rate_; }
    [[nodiscard]] const std::string& description() const { return description_; }

    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
    virtual auto write_header(std::ostream& out) const -> void = 0;
    virtual auto read_header(std::istream& in) const -> void = 0;
    virtual auto write_sample(std::ostream& out, std::uint64_t sample_index, const T& value) const -> void = 0;
    [[nodiscard]] virtual auto read_sample(std::istream& in, std::uint64_t& sample_index, T& value) const -> bool = 0;
protected:
    mutable Rate rate_;
    mutable std::string description_;
};

// Binary v3 is packed IEEE-754 binary64, always little-endian. Header fields
// are written separately; native structs/padding never become file bytes.
namespace detail {
inline void write_u64(std::ostream& out, std::uint64_t value) {
    std::array<char,8> bytes{};
    for (unsigned i=0;i<8;++i) bytes[i]=static_cast<char>((value>>(8*i))&255u);
    out.write(bytes.data(),8);
}
inline std::uint64_t read_u64(std::istream& in) {
    std::array<unsigned char,8> bytes{};
    in.read(reinterpret_cast<char*>(bytes.data()),8);
    if (!in) throw_bad_format("truncated binary field");
    std::uint64_t value=0;
    for (unsigned i=0;i<8;++i) value|=std::uint64_t(bytes[i])<<(8*i);
    return value;
}
inline Rate parse_recording_rate(const std::string& text) {
    const auto slash=text.find('/');
    if (slash==std::string::npos) throw_bad_format("rate must be numerator/denominator");
    std::uint64_t numerator{},denominator{};
    const auto a=std::from_chars(text.data(),text.data()+slash,numerator);
    const auto b=std::from_chars(text.data()+slash+1,text.data()+text.size(),denominator);
    if (a.ec!=std::errc{} || a.ptr!=text.data()+slash || b.ec!=std::errc{} || b.ptr!=text.data()+text.size())
        throw_bad_format("invalid recording rate");
    return Rate::reduced(numerator,denominator);
}
}

template <typename T>
class CompactBinaryFileFormat final : public SampleFileFormat<T> {
public:
    explicit CompactBinaryFileFormat(Rate rate = Rate::from_hz(1000), std::string description = {})
        : SampleFileFormat<T>(rate,std::move(description)) {}
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "compact-binary"; }
    auto write_header(std::ostream& out) const -> void override {
        // 40-byte fixed header + UTF-8 description: see docs/RECORDING_FORMAT.md.
        const auto type=static_cast<std::uint16_t>(detail::SignalTraits<T>::type);
        const std::array<unsigned char,8> prefix{'S','7','B','1',3,0,
            static_cast<unsigned char>(type&255u),static_cast<unsigned char>(type>>8)};
        out.write(reinterpret_cast<const char*>(prefix.data()),8);
        detail::write_u64(out,40+this->description_.size());
        detail::write_u64(out,this->rate_.numerator);
        detail::write_u64(out,this->rate_.denominator);
        detail::write_u64(out,this->description_.size());
        out.write(this->description_.data(),static_cast<std::streamsize>(this->description_.size()));
        write_index_=0;
        if (!out) detail::throw_bad_format("failed to write compact binary header");
    }
    auto read_header(std::istream& in) const -> void override {
        std::array<unsigned char,8> prefix{};
        in.read(reinterpret_cast<char*>(prefix.data()),8);
        if (!in) detail::throw_bad_format("truncated compact binary header");
        if (prefix[0]!='S'||prefix[1]!='7'||prefix[2]!='B'||prefix[3]!='1') detail::throw_bad_format("invalid compact binary magic");
        if (prefix[4]!=3) detail::throw_bad_format("unsupported compact binary version");
        if (prefix[5]!=0) detail::throw_bad_format("unsupported compact binary flags");
        const auto type=std::uint16_t(prefix[6])|(std::uint16_t(prefix[7])<<8);
        if (type!=static_cast<std::uint16_t>(detail::SignalTraits<T>::type)) detail::throw_bad_format("signal type does not match compact binary file");
        const auto header_size=detail::read_u64(in);
        const auto numerator=detail::read_u64(in);
        const auto denominator=detail::read_u64(in);
        this->rate_=Rate::reduced(numerator,denominator);
        const auto length=detail::read_u64(in);
        if (length>1024 || header_size!=40+length) detail::throw_bad_format("invalid compact binary header/description size");
        std::string description(static_cast<std::size_t>(length),'\0');
        in.read(description.data(),static_cast<std::streamsize>(length));
        if (!in) detail::throw_bad_format("truncated compact binary description");
        this->description_=detail::recording_description(description);
        read_index_=0;
    }
    auto write_sample(std::ostream& out, std::uint64_t sample_index, const T& value) const -> void override {
        if (sample_index!=write_index_) detail::throw_bad_format("compact binary samples must be contiguous from zero");
        if constexpr (std::is_same_v<T,RealValue>) detail::write_u64(out,std::bit_cast<std::uint64_t>(value));
        else {
            detail::write_u64(out,std::bit_cast<std::uint64_t>(value.real()));
            detail::write_u64(out,std::bit_cast<std::uint64_t>(value.imag()));
        }
        if (!out) detail::throw_bad_format("failed to write compact binary sample");
        ++write_index_;
    }
    [[nodiscard]] auto read_sample(std::istream& in, std::uint64_t& sample_index, T& value) const -> bool override {
        if (in.peek()==std::char_traits<char>::eof()) {
            if (in.bad()) detail::throw_bad_format("failed to read compact binary sample");
            return false;
        }
        const double real=std::bit_cast<double>(detail::read_u64(in));
        if constexpr (std::is_same_v<T,RealValue>) value=real;
        else value=T(real,std::bit_cast<double>(detail::read_u64(in)));
        sample_index=read_index_++;
        return true;
    }
private:
    mutable std::uint64_t read_index_{},write_index_{};
};

template <typename T>
class GnuplotFileFormat final : public SampleFileFormat<T> {
public:
    explicit GnuplotFileFormat(Rate rate = Rate::from_hz(1000), std::string description = {})
        : SampleFileFormat<T>(rate,std::move(description)) {}
    [[nodiscard]] auto name() const noexcept -> std::string_view override {
        return "gnuplot-text";
    }

    auto write_header(std::ostream& out) const -> void override {
        out.imbue(std::locale::classic());
        out << "# s7 gnuplot sample file\n";
        out << "# format=" << name() << '\n';
        out << "# version=1\n";
        out << "# signal=" << detail::SignalTraits<T>::name << '\n';
        out << "# rate=" << this->rate_.numerator << '/' << this->rate_.denominator << '\n';
        out << "# description=" << boost::json::serialize(boost::json::value(this->description_)) << '\n';
        out << "# columns=" << (std::is_same_v<T,RealValue> ? "sample_index value" : "sample_index real imag") << '\n';
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
        if (!std::getline(in,line) || detail::trim_left(line)!="# version=1") detail::throw_bad_format("unsupported gnuplot version");
        if (!std::getline(in, line) || detail::trim_left(line) != std::string("# signal=") + std::string(detail::SignalTraits<T>::name)) {
            detail::throw_bad_format("signal type does not match gnuplot file");
        }
        if (!std::getline(in,line) || !detail::trim_left(line).starts_with("# rate=")) detail::throw_bad_format("missing recording rate");
        this->rate_=detail::parse_recording_rate(detail::trim_left(line).substr(7));
        if (!std::getline(in,line) || !detail::trim_left(line).starts_with("# description=")) detail::throw_bad_format("missing recording description");
        const auto description=boost::json::parse(detail::trim_left(line).substr(14));
        if (!description.is_string()) detail::throw_bad_format("recording description must be a JSON string");
        this->description_=detail::recording_description(std::string(description.as_string()));
        const std::string columns=std::is_same_v<T,RealValue> ? "sample_index value" : "sample_index real imag";
        if (!std::getline(in,line) || detail::trim_left(line)!="# columns="+columns) detail::throw_bad_format("invalid recording columns");
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
            parser.imbue(std::locale::classic());
            parser >> sample_index;
            if (!parser) {
                detail::throw_bad_format("failed to parse gnuplot sample index");
            }
            read_sample_value(parser, value);
            if (!parser) {
                detail::throw_bad_format("failed to parse gnuplot sample value");
            }
            parser >> std::ws;
            if (!parser.eof()) detail::throw_bad_format("extra data after gnuplot sample value");
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
[[nodiscard]] inline auto make_compact_binary_format(Rate rate = Rate::from_hz(1000), std::string description = {}) -> std::unique_ptr<SampleFileFormat<T>> {
    return std::make_unique<CompactBinaryFileFormat<T>>(rate,std::move(description));
}

template <typename T>
[[nodiscard]] inline auto make_gnuplot_format(Rate rate = Rate::from_hz(1000), std::string description = {}) -> std::unique_ptr<SampleFileFormat<T>> {
    return std::make_unique<GnuplotFileFormat<T>>(rate,std::move(description));
}

template <typename T>
class FileSinkElement : public Element {
public:
    FileSinkElement(std::string_view type_name,
                    std::filesystem::path file_path,
                    std::unique_ptr<SampleFileFormat<T>> format,
                    std::ios::openmode mode)
        : Element(type_name, true),
          file_path_(std::move(file_path)),
          format_(std::move(format)),
          mode_(mode)
    {
        if (!format_) {
            throw std::invalid_argument("File sink requires a format.");
        }
        if (file_path_.empty()) throw std::invalid_argument("Recording file_path must not be empty.");
        register_input(0, port_names::in, detail::SignalTraits<T>::type, nullptr);
    }

    void step() override {
        const auto value = read_input<T>(0);
        if constexpr (std::is_same_v<T,RealValue>) {
            if (math::isnan(value)||math::isinf(value)) throw std::domain_error("Recording samples must be finite.");
        } else {
            if (math::isnan(value.real())||math::isinf(value.real())||math::isnan(value.imag())||math::isinf(value.imag()))
                throw std::domain_error("Recording samples must be finite.");
        }
        if (!stream_.is_open()) {
            stream_.open(file_path_,std::ios::out | mode_);
            if (!stream_) throw std::runtime_error("Could not open recording for writing: '"+file_path_.string()+"'.");
            format_->write_header(stream_);
        }
        format_->write_sample(stream_, sample_index_++, value);
        if (sample_index_ % 1024 == 0) flush();
    }
    void flush() {
        if (!stream_.is_open()) return;
        stream_.flush();
        if (!stream_) throw std::runtime_error("Failed to flush recording: '"+file_path_.string()+"'.");
    }
    [[nodiscard]] std::optional<Rate> declared_rate() const override { return format_->rate(); }

protected:
    std::filesystem::path file_path_;
    std::unique_ptr<SampleFileFormat<T>> format_;
    std::ios::openmode mode_;
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
        : Element(type_name, true),
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

    [[nodiscard]] std::optional<Rate> declared_rate() const override { return format_->rate(); }
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
    explicit CompactBinaryFileSink(std::filesystem::path file_path, Rate rate = Rate::from_hz(1000))
        : FileSinkElement<T>("CompactBinaryFileSink",
                             std::move(file_path),
                             make_compact_binary_format<T>(rate),
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
    explicit GnuplotFileSink(std::filesystem::path file_path, Rate rate = Rate::from_hz(1000))
        : FileSinkElement<T>("GnuplotFileSink",
                             std::move(file_path),
                             make_gnuplot_format<T>(rate),
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
