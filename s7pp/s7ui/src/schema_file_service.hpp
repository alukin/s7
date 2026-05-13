#pragma once

#include "scene_serializer.hpp"

#include <boost/json.hpp>

class QString;

namespace QSchematic {
class Scene;
}

namespace s7ui::ui {

/// Handles schema/runtime JSON generation and atomic schema file writes.
class SchemaFileService
{
public:
    [[nodiscard]] boost::json::value buildRuntimeJson(const QSchematic::Scene& scene,
                                                      const QString& schemaName,
                                                      double dt) const;
    [[nodiscard]] boost::json::value buildSchemaJson(const QSchematic::Scene& scene,
                                                     const QString& schemaName,
                                                     double dt,
                                                     const QString& sheetStandard,
                                                     double sheetWidthMm,
                                                     double sheetHeightMm) const;

    void saveSchemaJsonToFile(const QString& path, const boost::json::value& schemaJson) const;

private:
    SceneSerializer serializer_;
};

} // namespace s7ui::ui

