#pragma once

#include <boost/json.hpp>

#include <string>
#include <vector>

namespace s7studio {

// A probe to sample while running (DiagramProbe, without the display-only
// unit): a read-only tap on an existing element's output port, named for
// routing samples back to whatever is showing them (ADR-011/012: never a
// Functional Graph element).
struct RunProbe {
    std::string name;
    std::string element;
    int port = 0;
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

    [[nodiscard]] virtual bool isRunning() const = 0;
    [[nodiscard]] virtual bool isPaused() const = 0;
};

} // namespace s7studio
