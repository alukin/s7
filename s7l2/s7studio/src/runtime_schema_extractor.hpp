#pragma once

#include <boost/json.hpp>

#include <string>
#include <vector>

namespace QSchematic {
class Scene;
namespace Items {
class Wire;
}
}

namespace s7studio::diagram {

struct SerializationOptions;

// The output a wire carries: element id and output port of its source.
struct WireSource {
    QSchematic::Items::Wire* wire{};
    std::string element;
    int port{};
};

class RuntimeSchemaExtractor
{
public:
    // wireSources, if given, receives the source endpoint of every connected wire.
    [[nodiscard]] auto extract(const QSchematic::Scene& scene,
                               const SerializationOptions& options,
                               std::vector<WireSource>* wireSources = nullptr) const -> boost::json::value;
};

} // namespace s7studio::diagram

