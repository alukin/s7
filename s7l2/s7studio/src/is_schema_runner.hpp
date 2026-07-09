#pragma once

#include <boost/json.hpp>

namespace s7studio {

class ISchemaRunner
{
public:
    virtual ~ISchemaRunner() = default;

    virtual void run(const boost::json::value& schemaJson, double dt, long ticks) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;

    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual bool isPaused() const = 0;
};

} // namespace s7studio
