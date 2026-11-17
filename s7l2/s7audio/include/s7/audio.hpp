// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <s7/hardware.hpp>
namespace s7::audio {
// Library inspection/factory selection does not initialize an audio API.
std::vector<std::string> backends();
std::unique_ptr<hardware::Provider> provider(std::string_view backend);
void register_elements(ElementRegistry&);
class Source final : public hardware::Source {
public: explicit Source(hardware::Request r) : hardware::Source(std::move(r),"AudioSource") {}
};
class Sink final : public hardware::Sink {
public: explicit Sink(hardware::Request r) : hardware::Sink(std::move(r),"AudioSink") {}
};
} // namespace s7::audio
