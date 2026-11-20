// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <filesystem>
#include <stdexcept>
#include <string>

namespace s7tests {
inline std::filesystem::path education_diagram_path(const std::filesystem::path& labs,
                                                   const std::string& filename) {
    std::filesystem::path found;
    for (const auto& lab : std::filesystem::directory_iterator(labs)) {
        if (!lab.is_directory() || !std::filesystem::is_regular_file(lab.path() / filename)) continue;
        if (!found.empty()) throw std::runtime_error("Duplicate educational diagram: " + filename);
        found = lab.path() / filename;
    }
    if (found.empty()) throw std::runtime_error("Missing educational diagram: " + filename);
    return found;
}
}
