// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once
#include <s7/hardware.hpp>
#include <filesystem>
namespace s7::hardware {
// Provider ABI v1 uses the installed C++ SDK/compiler ABI. It is independent
// of the element-plugin ABI. Export a factory returning an uninitialized
// Provider, or nullptr for an unrecognized backend. Exceptions may propagate.
using ProviderFactoryV1 = Provider* (*)(const char* backend);
class ProviderModule {
public:
    explicit ProviderModule(const std::filesystem::path&);
    std::unique_ptr<Provider> create(std::string_view backend) const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace s7::hardware
// Export from an external module:
// extern "C" s7::hardware::Provider* s7_hardware_provider_v1(const char* backend);
