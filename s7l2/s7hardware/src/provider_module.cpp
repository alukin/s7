// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/hardware_plugin.hpp>
#include <s7/platform.hpp>
namespace s7::hardware {
struct ProviderModule::Impl {
    void* library{};
    ProviderFactoryV1 factory{};
    ~Impl() { if (library) platform::close_library(library); }
};
namespace {
class ModuleStream final : public Stream {
public:
    ModuleStream(std::shared_ptr<void> code,std::unique_ptr<Stream> s)
        : Stream(s->descriptor()),code_(std::move(code)),stream_(std::move(s)) {}
    ~ModuleStream() override { stop(); }
    void cancel() noexcept override { Stream::cancel(); stream_->cancel(); }
    std::uint64_t emergency_output_frames() const noexcept override { return stream_->emergency_output_frames(); }
protected:
    void do_start() override { stream_->start(); }
    void do_stop() noexcept override { stream_->stop(); }
    Transfer do_read(std::span<std::byte> b,std::size_t n,Deadline d,std::stop_token st) override {
        return stream_->read(b,n,d,st);
    }
    Transfer do_write(std::span<const std::byte> b,std::size_t n,Deadline d,std::stop_token st) override { return stream_->write(b,n,d,st); }
private:
    std::shared_ptr<void> code_; // Destroy stream before unloading its code.
    std::unique_ptr<Stream> stream_;
};
class ModuleProvider final : public Provider {
public:
    ModuleProvider(std::shared_ptr<void> code,std::unique_ptr<Provider> p) : code_(std::move(code)),provider_(std::move(p)) {}
    std::string_view id() const noexcept override { return provider_->id(); }
    std::vector<DeviceInfo> devices() override { return provider_->devices(); }
    std::unique_ptr<Stream> prepare(const Request& r) override {
        auto stream=provider_->prepare(r);
        if (!stream) throw std::runtime_error("external provider returned no stream");
        return std::make_unique<ModuleStream>(code_,std::move(stream));
    }
private:
    std::shared_ptr<void> code_;
    std::unique_ptr<Provider> provider_;
};
}
ProviderModule::ProviderModule(const std::filesystem::path& path) : impl_(std::make_shared<Impl>()) {
    std::string error; impl_->library=platform::open_library(path,error);
    if (!impl_->library) throw std::runtime_error("hardware provider load failed: "+error);
    impl_->factory=reinterpret_cast<ProviderFactoryV1>(platform::library_symbol(impl_->library,"s7_hardware_provider_v1"));
    if (!impl_->factory) throw std::invalid_argument("hardware provider does not export ABI v1 factory");
}
std::unique_ptr<Provider> ProviderModule::create(std::string_view backend) const {
    auto p=std::unique_ptr<Provider>(impl_->factory(std::string(backend).c_str()));
    if (!p) return {};
    return std::make_unique<ModuleProvider>(impl_,std::move(p));
}
} // namespace s7::hardware
