#pragma once

#include <boost/json.hpp>

#include <QString>

namespace QSchematic {
class Scene;
}

namespace s7ui::ui {

struct SceneSerializationOptions
{
    QString schemaName;
    double dt = 0.001;
    bool includeDiagram = true;
    QString sheetStandard = "A4";
    QString sheetOrientation = "landscape";
    double sheetWidthMm = 297.0;
    double sheetHeightMm = 210.0;
};

class SceneSerializer
{
public:
    [[nodiscard]] boost::json::value serialize(const QSchematic::Scene& scene,
                                               const SceneSerializationOptions& options) const;
};

} // namespace s7ui::ui

