#include "schema_file_service.hpp"

#include <qschematic/scene.hpp>

#include <QByteArray>
#include <QIODevice>
#include <QSaveFile>
#include <QString>

#include <stdexcept>

namespace s7ui::ui {

boost::json::value SchemaFileService::buildRuntimeJson(const QSchematic::Scene& scene,
                                                       const QString& schemaName,
                                                       const double dt) const
{
    SceneSerializationOptions opts;
    opts.schemaName = schemaName;
    opts.dt = dt;
    opts.includeDiagram = false;
    return serializer_.serialize(scene, opts);
}

boost::json::value SchemaFileService::buildSchemaJson(const QSchematic::Scene& scene,
                                                      const QString& schemaName,
                                                      const double dt,
                                                      const QString& sheetStandard,
                                                      const double sheetWidthMm,
                                                      const double sheetHeightMm) const
{
    SceneSerializationOptions opts;
    opts.schemaName = schemaName;
    opts.dt = dt;
    opts.includeDiagram = true;
    opts.sheetStandard = sheetStandard;
    opts.sheetWidthMm = sheetWidthMm;
    opts.sheetHeightMm = sheetHeightMm;
    return serializer_.serialize(scene, opts);
}

void SchemaFileService::saveSchemaJsonToFile(const QString& path,
                                             const boost::json::value& schemaJson) const
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        throw std::runtime_error(QString("Cannot open for writing: %1").arg(path).toStdString());
    }

    const auto payload = QByteArray::fromStdString(boost::json::serialize(schemaJson));
    if (file.write(payload) != payload.size()) {
        throw std::runtime_error(QString("Failed to write: %1").arg(path).toStdString());
    }

    if (!file.commit()) {
        throw std::runtime_error(QString("Failed to commit: %1").arg(path).toStdString());
    }
}

} // namespace s7ui::ui

