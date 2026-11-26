// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace s7studio {

// One named probe value, for hosts that deliver samples to the viewers
// themselves (observation::ObservationSession, ViewerManager::deliver). Local
// runs do not use it: their viewers read the run's recording directly.
struct ScopeSample {
    std::string probeName;
    long step = 0;
    double timeSeconds = 0.0;
    double value = 0.0;
    std::uint64_t runId = 0;
};

struct ScopeSampleBatch {
    std::vector<ScopeSample> samples; // in production order
    std::uint64_t dropped = 0;        // lost before delivery, counted as capture loss
    std::uint64_t runId = 0;
};

} // namespace s7studio
