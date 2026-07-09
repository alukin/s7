#pragma once

#include "is_schema_runner.hpp"

#include <boost/json.hpp>

#include <QString>

#include <algorithm>
#include <cmath>

class QAction;
class QLabel;
class QSpinBox;

namespace s7studio::ui {

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

    void run(const boost::json::value& runtimeJson,
             double dt,
             const QSpinBox* hours,
             const QSpinBox* minutes,
             const QSpinBox* seconds) const;
    void pause() const;
    void resume() const;
    void stop() const;
    void reset() const;

    void updateActionStates(QAction* runAction,
                            QAction* pauseAction,
                            QAction* resumeAction,
                            QAction* stopAction,
                            QAction* resetAction) const;

    void setStatusLabel(QLabel* label, const QString& status) const;

private:
    s7studio::ISchemaRunner* runner_ = nullptr;
};

} // namespace s7studio::ui
