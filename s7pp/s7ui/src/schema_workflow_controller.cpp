#include "schema_workflow_controller.hpp"

#include "schema_file_service.hpp"

#include <qschematic/scene.hpp>

#include <stdexcept>

namespace s7ui::ui {

SchemaWorkflowController::SchemaWorkflowController(QSchematic::Scene* const scene,
                                                   SchemaFileService* const fileService)
    : scene_(scene)
    , fileService_(fileService)
{
}

LoadedSchemaResult SchemaWorkflowController::loadFromFile(SchemaDocumentController& documentController,
                                                          const QString& path) const
{
    return documentController.loadFromFile(path);
}

void SchemaWorkflowController::saveToFile(const QString& path,
                                          const QString& schemaName,
                                          const double dt,
                                          const QString& sheetStandard,
                                          const double sheetWidthMm,
                                          const double sheetHeightMm) const
{
    if (!scene_ || !fileService_) {
        throw std::runtime_error("Schema workflow controller is not initialized");
    }

    const auto schemaJson = fileService_->buildSchemaJson(
        *scene_, schemaName, dt, sheetStandard, sheetWidthMm, sheetHeightMm);
    fileService_->saveSchemaJsonToFile(path, schemaJson);
}

boost::json::value SchemaWorkflowController::buildRuntimeJson(const QString& schemaName,
                                                              const double dt) const
{
    if (!scene_ || !fileService_) {
        throw std::runtime_error("Schema workflow controller is not initialized");
    }

    return fileService_->buildRuntimeJson(*scene_, schemaName, dt);
}

} // namespace s7ui::ui

