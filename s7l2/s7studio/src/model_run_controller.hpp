#pragma once

#include "is_schema_runner.hpp"

#include <boost/json.hpp>

#include <vector>

namespace s7studio::ui {

struct RunDuration {
    int hours = 0;
    int minutes = 1;
    int seconds = 0;
};

struct RunExecutionConfig {
    long runtimeSeconds = 60; // -1 means continuous until user stops
};

struct RunActionState {
    bool runEnabled = true;
    bool pauseEnabled = false;
    bool resumeEnabled = false;
    bool stopEnabled = false;
    bool resetEnabled = false;
};

class ModelRunController
{
public:
    explicit ModelRunController(s7studio::ISchemaRunner* runner);

    // Include only whole model steps at or before a finite duration boundary.
    [[nodiscard]] static long computeTicks(int hours, int minutes, int seconds, double dt);
    [[nodiscard]] static long computeTicks(long runtimeSeconds, double dt);

    void run(const boost::json::value& runtimeJson,
             double dt,
             const RunDuration& duration,
             std::vector<RunProbe> probes) const;
    void run(const boost::json::value& runtimeJson,
             double dt,
             const RunExecutionConfig& config,
             std::vector<RunProbe> probes) const;
    void pause() const;
    void resume() const;
    void stop() const;
    void reset() const;

    [[nodiscard]] auto actionState() const -> RunActionState;

private:
    s7studio::ISchemaRunner* runner_ = nullptr;
};

} // namespace s7studio::ui
