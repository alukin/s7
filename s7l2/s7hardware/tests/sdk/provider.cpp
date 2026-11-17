// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/hardware_plugin.hpp>
#include <thread>
namespace hw=s7::hardware;
class Stream final : public hw::Stream {
public:
    explicit Stream(hw::Request r):hw::Stream([&]{hw::Descriptor d;d.actual=std::move(r);return d;}()){}
    ~Stream() override {stop();}
    void do_start() override {}
    void do_stop() noexcept override {}
    hw::Transfer do_read(std::span<std::byte>,std::size_t,hw::Deadline d,std::stop_token st) override {
        while(!cancelled() && !st.stop_requested() && std::chrono::steady_clock::now()<d)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return {0,cancelled()||st.stop_requested()?hw::Status::Cancelled:hw::Status::Timeout,0,{}};
    }
};
class Provider final : public hw::Provider {
public:
    std::string_view id() const noexcept override {return "sdk-test";}
    std::vector<hw::DeviceInfo> devices() override {return {};}
    std::unique_ptr<hw::Stream> prepare(const hw::Request& r) override {return std::make_unique<Stream>(r);}
};
extern "C" hw::Provider* s7_hardware_provider_v1(const char* backend) {
    return backend && std::string_view(backend)=="sdk-test"?new Provider:nullptr;
}
