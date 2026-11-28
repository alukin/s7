// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <s7/hardware.hpp>
namespace s7::sdr {
std::vector<std::string> backends();
std::unique_ptr<hardware::Provider> provider(std::string_view backend, bool keep_sdr_active = false);
bool can_keep_sdr_active(std::string_view device);
void register_elements(ElementRegistry&);
class Source final : public hardware::Source {
public: explicit Source(hardware::Request r) : hardware::Source(std::move(r),"SdrSource") {}
};
class BlockSource final : public hardware::CaptureBlockSource {
public:
    explicit BlockSource(hardware::Request request, std::uint64_t source_id = 1)
        : hardware::CaptureBlockSource("SdrBlockSource", std::move(request), source_id) {}
};
class Sink final : public hardware::Sink {
public: explicit Sink(hardware::Request r) : hardware::Sink(std::move(r),"SdrSink") {}
};
} // namespace s7::sdr
