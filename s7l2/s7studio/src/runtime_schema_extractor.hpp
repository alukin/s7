#pragma once

#include <boost/json.hpp>

namespace QSchematic {
class Scene;
}

namespace s7studio::diagram {

struct SerializationOptions;

class RuntimeSchemaExtractor
{
public:
    [[nodiscard]] auto extract(const QSchematic::Scene& scene,
                               const SerializationOptions& options) const -> boost::json::value;
};

} // namespace s7studio::diagram

