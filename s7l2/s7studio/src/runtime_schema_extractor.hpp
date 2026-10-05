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

// A connected wire: the output it carries (element id and port of its
// source) and the input it feeds.
struct WireSource {
    QSchematic::Items::Wire* wire{};
    std::string element;
    int port{};
    std::string toElement;
    int toPort{};
};

class RuntimeSchemaExtractor
{
public:
    // wireSources, if given, receives every connected wire, in the order of
    // the schema's connections.
    [[nodiscard]] auto extract(const QSchematic::Scene& scene,
                               const SerializationOptions& options,
                               std::vector<WireSource>* wireSources = nullptr) const -> boost::json::value;
};

} // namespace s7studio::diagram

