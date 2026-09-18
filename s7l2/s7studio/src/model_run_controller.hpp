#pragma once

#include "is_schema_runner.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <cmath>

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

    [[nodiscard]] static long computeTicks(int hours, int minutes, int seconds, double dt)
    {
        const long totalSeconds = std::max<long>(1, static_cast<long>(hours) * 3600L
                                                      + static_cast<long>(minutes) * 60L
                                                      + static_cast<long>(seconds));
        return std::max<long>(1, static_cast<long>(
                std::llround(static_cast<double>(totalSeconds) / std::max(1e-12, dt))));
    }

    [[nodiscard]] static long computeTicks(const long runtimeSeconds, const double dt)
    {
        if (runtimeSeconds < 0) {
            return -1;
        }
        const long clampedSeconds = std::max<long>(1, runtimeSeconds);
        return std::max<long>(1, static_cast<long>(
                std::llround(static_cast<double>(clampedSeconds) / std::max(1e-12, dt))));
    }

    void run(const boost::json::value& runtimeJson,
             double dt,
             const RunDuration& duration) const;
    void run(const boost::json::value& runtimeJson,
             double dt,
             const RunExecutionConfig& config) const;
    void pause() const;
    void resume() const;
    void stop() const;
    void reset() const;

    [[nodiscard]] auto actionState() const -> RunActionState;

private:
    s7studio::ISchemaRunner* runner_ = nullptr;
};

} // namespace s7studio::ui
