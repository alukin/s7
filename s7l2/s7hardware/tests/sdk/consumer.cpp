// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/hardware_plugin.hpp>
#include <s7/audio.hpp>
#if defined(S7_WITH_SDR)
#include <s7/sdr.hpp>
#endif
#include <cassert>
#include <thread>
int main(int argc,char** argv) {
    assert(argc==2); namespace hw=s7::hardware;
    s7::audio::register_elements(s7::ElementRegistry::instance());
#if defined(S7_WITH_SDR)
    s7::sdr::register_elements(s7::ElementRegistry::instance());
#endif
    std::unique_ptr<hw::Stream> stream;
    {
        hw::ProviderModule module(argv[1]);assert(!module.create("unknown"));
        auto provider=module.create("sdk-test");assert(provider && provider->id()=="sdk-test");
        hw::Request r;r.backend="sdk-test";stream=provider->prepare(r);
    } // Stream must retain executable code after its provider/module goes away.
    stream->start();hw::Buffer buffer(1024);
    std::jthread controller([&]{std::this_thread::sleep_for(std::chrono::milliseconds(10));stream->cancel();});
    const auto result=stream->read(buffer.bytes(),256,std::chrono::steady_clock::now()+std::chrono::seconds(2));
    assert(result.status==hw::Status::Cancelled);controller.join();stream.reset();
}
