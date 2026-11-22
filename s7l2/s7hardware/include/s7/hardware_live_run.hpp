// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#pragma once

#include <s7/hardware.hpp>

namespace s7::hardware {

// Optional entry point of the public audio element plugin. Hosts in the same
// C++ SDK ABI can use it without linking to any hardware library. The plugin
// must stay loaded until all its LiveRuns are destroyed.
class LiveRun {
public:
    virtual ~LiveRun() = default;
    virtual NodeRuntime& runtime() noexcept = 0;
    virtual std::string prepare() = 0;
    virtual void execute(std::uint64_t frames, std::stop_token, const Run::StepObserver&) = 0;
};

// Returns null and leaves graph intact for a graph without hardware.
// Otherwise consumes the graph, validates its profile and creates an offline
// run. Call prepare, execute and destroy on the same execution thread.
inline constexpr auto live_run_symbol = "s7_create_live_run_v1";
using CreateLiveRun = LiveRun* (*)(ExecutableGraph&, Rate);

} // namespace s7::hardware
