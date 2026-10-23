// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

#include "s7/io.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool ok,const char* message) {if(!ok) throw std::runtime_error(message);}
template <typename F> void rejects(F f) {
    bool caught=false;
    try {f();} catch(const std::exception&) {caught=true;}
    require(caught,"Invalid recording/configuration must be rejected");
}
struct Temp {
    std::filesystem::path dir=std::filesystem::temp_directory_path()/
        ("s7-replay-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() {std::filesystem::create_directory(dir);}
    ~Temp() {std::error_code ec;std::filesystem::remove_all(dir,ec);}
};
template <typename T> T sample(s7::io::RecordedSource<T>& source) {
    source.step();return source.output_ports()[0].template as<T>();
}
template <typename T> void playback(const std::filesystem::path& path, bool binary, T a, T b) {
    using namespace s7::io;
    const auto kind=binary ? RecordingFormat::CompactBinary : RecordingFormat::Gnuplot;
    const std::string description="Output \"voltage\"\nΔV [V]";
    {
        RecordingSink<T> sink(path,8,kind,description);
        require(!std::filesystem::exists(path),"Sink construction must not create a file");
        const s7::InputBinding first[]={&a},second[]={&b};
        sink.bind_inputs(first);sink.step();
        sink.bind_inputs(second);sink.step();sink.flush();
    }
    if(binary) require(std::filesystem::file_size(path)==40+description.size()+2*sizeof(double)*(std::is_same_v<T,double> ? 1 : 2),"Packed binary file size without per-sample indices");
    for(auto behavior : {ReplayEnd::Error,ReplayEnd::Hold,ReplayEnd::Repeat}) {
        RecordedSource<T> source(path,0,kind,behavior);
        require(source.description()==description,"UTF-8 description round trip");
        require(source.has_state(),"Replay has state");
        require(source.declared_rate()==s7::Rate::from_hz(8),"Replay pins its model rate");
        require(sample(source)==a&&sample(source)==b,"Replay first and last sample");
        if(behavior==ReplayEnd::Error) rejects([&] {source.step();});
        else for(int n=0;n<10;++n)
            require(sample(source)==(behavior==ReplayEnd::Hold||n%2 ? b : a),"Hold/repeat boundary behavior");
    }
    // Registration is idempotent and the actual factory works through the compiler.
    auto& registry=s7::ElementRegistry::instance();
    s7::register_io_elements(registry);s7::register_io_elements(registry);
    s7::Schema schema("recording");
    const auto type=s7::io::detail::SignalTraits<T>::type;
    schema.add_element(s7::make_element_spec("replay",*registry.find("RecordedSource"),
        {{"file_path",path.string()},{"type",std::string(s7::signal_type_name(type))},
         {"sample_rate_hz",8},{"format",binary ? "compact-binary" : "gnuplot-text"},{"end_behavior","repeat"}}));
    require(s7::Validator::validate(schema).ok(),"Replay registry schema validates");
    s7::NodeRuntime runtime(s7::Compiler::compile(schema));
    for(int n=0;n<6;++n) {
        runtime.step();
        if constexpr(std::is_same_v<T,s7::RealValue>) require(runtime.output_of<s7::SignalType::F64>("replay")== (n%2 ? b : a),"Real replay runtime samples");
        else require(runtime.output_of<s7::SignalType::C64>("replay")== (n%2 ? b : a),"Complex replay runtime samples");
    }
    if(binary) {
        std::ofstream file(path,std::ios::binary|std::ios::app);file.put('x');file.close();
        rejects([&] {RecordedSource<T> source(path,8,kind);});
    }
}
template <typename T> void text_line_endings(const std::filesystem::path& path) {
    using namespace s7::io;
    constexpr bool real=std::is_same_v<T,s7::RealValue>;
    for(const std::string ending : {"\n","\r\n"}) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        for(const std::string line : {"# s7 gnuplot sample file","# format=gnuplot-text","# version=1",
                                     real ? "# signal=F64" : "# signal=C64", "# rate=8/1",
                                     "# description=\"First\\r\\nSecond\"",
                                     real ? "# columns=sample_index value" : "# columns=sample_index real imag",
                                     real ? "0 1.25" : "0 1.25 -2.5", real ? "1 -3.5" : "1 -3.5 4.75"})
            file<<line<<ending;
        file.close();
        RecordedSource<T> source(path,0,RecordingFormat::Gnuplot,ReplayEnd::Repeat);
        require(source.description()=="First\r\nSecond","Line delimiters must not change escaped description content");
        require(source.declared_rate()==s7::Rate::from_hz(8),"Text newline fixture rate");
        for(int n=0;n<4;++n) {
            if constexpr(real) require(sample(source)==(n%2 ? -3.5 : 1.25),"LF/CRLF real playback");
            else require(sample(source)==(n%2 ? s7::ComplexValue(-3.5,4.75) : s7::ComplexValue(1.25,-2.5)),"LF/CRLF complex playback");
        }
    }
}

void registered_sink(const std::filesystem::path& path) {
    auto& registry=s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);s7::register_io_elements(registry);
    for(const char* type : {"F64","C64"}) {
        {std::ofstream file(path);file<<"keep this during validation";}
        const auto contents=[&] {std::ifstream file(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(file),{});};
        s7::Schema schema("recording_sink");
        schema.add_element(s7::make_element_spec("value",*registry.find("Constant"),{{"type",type},{"value",2}}));
        schema.add_element(s7::make_element_spec("sink",*registry.find("RecordingSink"),
            {{"file_path",path.string()},{"sample_rate_hz",8},{"description","Recorded fixture"}}));
        schema.connect("value",0,"sink",0);
        require(s7::Validator::validate(schema).ok(),"Recording sink validates and infers its input type");
        require(contents()=="keep this during validation","Validation must not truncate an existing recording");
        {
            s7::NodeRuntime runtime(s7::Compiler::compile(schema));
            require(contents()=="keep this during validation","Compilation must not truncate an existing recording");
            runtime.step();runtime.step();
        }
        if(std::string(type)=="F64") {
            s7::io::RealRecordedSource source(path);
            require(source.declared_rate()==s7::Rate::from_hz(8),"Default binary replay reads header rate");
            require(sample(source)==2&&sample(source)==2,"Registered real sink round trip");
        } else {
            s7::io::ComplexRecordedSource source(path);
            require(sample(source)==s7::ComplexValue(2,0)&&sample(source)==s7::ComplexValue(2,0),"Registered complex sink round trip");
        }
    }
}

void binary_header(const std::filesystem::path& path) {
    using namespace s7::io;
    // Independent on-disk fixture: v3, F64, header size 41, rate 8/1,
    // description "V", one IEEE-754 little-endian sample 1.25.
    const std::vector<unsigned char> golden={
        'S','7','B','1',3,0,0x0f,0,
        41,0,0,0,0,0,0,0, 8,0,0,0,0,0,0,0,
        1,0,0,0,0,0,0,0, 1,0,0,0,0,0,0,0,
        'V', 0,0,0,0,0,0,0xf4,0x3f};
    {
        RealRecordingSink sink(path,8,RecordingFormat::CompactBinary,"V");
        const double value=1.25;const s7::InputBinding binding[]={&value};
        sink.bind_inputs(binding);sink.step();sink.flush();
    }
    {std::ifstream file(path,std::ios::binary);const std::vector<unsigned char> actual{std::istreambuf_iterator<char>(file),{}};
        require(actual==golden,"Binary writer matches independently specified header and sample bytes");}
    const auto write=[&](const std::vector<unsigned char>& bytes) {
        std::ofstream file(path,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
    };
    write(golden);
    {RealRecordedSource source(path);require(sample(source)==1.25&&source.description()=="V","Independent binary fixture decodes");}
    for(std::size_t size=0;size<golden.size();++size) {
        write({golden.begin(),golden.begin()+size});
        rejects([&] {RealRecordedSource source(path);});
    }
    for(std::size_t offset : {0u,4u,5u,6u,8u,16u,24u,32u,40u}) {
        auto bad=golden;bad[offset]=offset==40 ? 0xff : (offset==16||offset==24 ? 0 : 0xff);write(bad);
        rejects([&] {RealRecordedSource source(path);});
    }
    rejects([&] {RealRecordingSink sink(path,8,RecordingFormat::CompactBinary,std::string(1025,'x'));});
}

void invalid(const std::filesystem::path& path) {
    using namespace s7::io;
    rejects([&] {RealRecordedSource missing(path);});
    auto format=make_gnuplot_format<s7::RealValue>(s7::Rate::from_hz(8));
    for(const std::string data : {"", "1 2\n", "0 2\n2 3\n", "0 2 garbage\n", "0 bad\n"}) {
        {std::ofstream file(path);format->write_header(file);file<<data;}
        rejects([&] {RealRecordedSource source(path,0,RecordingFormat::Gnuplot);});
    }
    {std::ofstream file(path);format->write_header(file);format->write_sample(file,0,1);}
    rejects([&] {RealRecordedSource source(path,9,RecordingFormat::Gnuplot);});
    rejects([&] {ComplexRecordedSource source(path,0,RecordingFormat::Gnuplot);});
    RealRecordedSource single(path,8,RecordingFormat::Gnuplot,ReplayEnd::Repeat);
    for(int n=0;n<5;++n) require(sample(single)==1,"Single-sample recording repeats");
}
}
int main() {
    try {
        Temp temp;
        for(bool binary : {false,true}) {
            playback<double>(temp.dir/(binary ? "real.bin" : "real.dat"),binary,1.25,-3.5);
            playback<s7::ComplexValue>(temp.dir/(binary ? "complex.bin" : "complex.dat"),binary,{1,2},{-3,4});
        }
        registered_sink(temp.dir/"registered.s7bin");
        text_line_endings<s7::RealValue>(temp.dir/"newlines-real.dat");
        text_line_endings<s7::ComplexValue>(temp.dir/"newlines-complex.dat");
        binary_header(temp.dir/"header.s7bin");
        invalid(temp.dir/"invalid.dat");
        std::cout<<"Recording replay: real/complex formats, EOF modes, registry and malformed data passed\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
