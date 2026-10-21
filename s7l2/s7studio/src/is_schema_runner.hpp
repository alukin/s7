#pragma once

#include <boost/json.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace s7studio {

// A probe to sample while running, retaining its display-only unit: a read-only tap on an existing element's output port, named for
// routing samples back to whatever is showing them (ADR-011/012: never a
// Functional Graph element).
struct RunProbe {
    std::string name;
    std::string element;
    int port = 0;
    std::string unit{};
    bool operator==(const RunProbe&) const = default;
};

enum class RunState {
    Idle, Preparing, Prepared, Running, Pausing, Paused, Stopping, Stopped, Completed, Error
};

[[nodiscard]] inline bool runIsActive(RunState state)
{
    return state == RunState::Preparing || state == RunState::Prepared
        || state == RunState::Running || state == RunState::Pausing
        || state == RunState::Paused || state == RunState::Stopping;
}

[[nodiscard]] inline const char* runStateName(RunState state)
{
    switch (state) {
    case RunState::Idle: return "idle";
    case RunState::Preparing: return "preparing";
    case RunState::Prepared: return "prepared";
    case RunState::Running: return "running";
    case RunState::Pausing: return "pausing";
    case RunState::Paused: return "paused";
    case RunState::Stopping: return "stopping";
    case RunState::Stopped: return "stopped";
    case RunState::Completed: return "completed";
    case RunState::Error: return "error";
    }
    return "error";
}

// A run owns this copy independently of subsequent editor/configuration edits.
// The owner/UI thread may retain a const snapshot after completion or reset.
struct RunConfiguration {
    std::uint64_t id;
    boost::json::value schemaJson;
    double dt;
    long ticks;
    std::vector<RunProbe> probes;
};

class ISchemaRunner
{
public:
    virtual ~ISchemaRunner() = default;

    virtual void run(const boost::json::value& schemaJson, double dt, long ticks, std::vector<RunProbe> probes) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;

    [[nodiscard]] virtual RunState state() const = 0;
    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual bool isPaused() const = 0;
};

} // namespace s7studio
