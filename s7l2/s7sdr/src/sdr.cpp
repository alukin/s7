// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <s7/sdr.hpp>
namespace s7::sdr {
#if S7_HAVE_SOAPYSDR
std::unique_ptr<hardware::Provider> soapy_provider();
#endif
std::vector<std::string> backends() {
#if S7_HAVE_SOAPYSDR
    return {"soapy"};
#else
    return {};
#endif
}
std::unique_ptr<hardware::Provider> provider(std::string_view name) {
#if S7_HAVE_SOAPYSDR
    if (name=="soapy") return soapy_provider();
#endif
    throw std::invalid_argument("SDR backend unavailable: "+std::string(name));
}
void register_elements(ElementRegistry& r) {
    hardware::register_elements(r,"s7sdr","SdrSource","SdrSink",{SignalType::C32,SignalType::CI12,SignalType::CI16},"soapy","radio");
}
} // namespace s7::sdr
