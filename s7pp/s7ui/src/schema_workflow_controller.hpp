#pragma once

#include "schema_document_controller.hpp"

#include <boost/json.hpp>

class QString;

namespace QSchematic {
class Scene;
}

namespace s7ui::ui {

class SchemaFileService;

/// Coordinates schema file workflow operations across document and file services.
class SchemaWorkflowController
{
public:
    explicit SchemaWorkflowController(QSchematic::Scene* scene,
                                      SchemaFileService* fileService);

    [[nodiscard]] LoadedSchemaResult loadFromFile(SchemaDocumentController& documentController,
                                                  const QString& path) const;

    void saveToFile(const QString& path,
                    const QString& schemaName,
                    double dt,
                    const QString& sheetStandard,
                    double sheetWidthMm,
                    double sheetHeightMm) const;

    [[nodiscard]] boost::json::value buildRuntimeJson(const QString& schemaName,
                                                      double dt) const;

private:
    QSchematic::Scene* scene_ = nullptr;
    SchemaFileService* fileService_ = nullptr;
};

} // namespace s7ui::ui

