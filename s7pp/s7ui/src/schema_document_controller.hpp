#pragma once
#include "s7ui_json_dto.hpp"
#include <boost/json.hpp>
#include <QObject>
#include <QString>
namespace QSchematic { class Scene; }
namespace s7ui { class ISchemaRunner; }
namespace s7ui::ui {
class NodeFactory;
class SheetPresenter;
/// Metadata returned after successfully loading a schema.
struct LoadedSchemaResult
{
    QString schemaName      = "s7ui_schema";
    double  dt              = 0.001;
    QString sheetStandard   = "A4";
    QString sheetOrientation = "landscape";
    int     nextNodeCounter = 0;   ///< Use to reset createdNodeCounter_ in MainWindow
    int     nodeCount       = 0;
    int     wireCount       = 0;
};
/// Owns the load/save document lifecycle.
/// Translates between JSON documents and the live QSchematic scene.
///
/// SRP: sole responsibility is schema document I/O and scene population.
class SchemaDocumentController : public QObject
{
    Q_OBJECT
public:
    explicit SchemaDocumentController(QSchematic::Scene*     scene,
                                      NodeFactory*           nodeFactory,
                                      SheetPresenter*        sheetPresenter,
                                      s7ui::ISchemaRunner*   runner,
                                      QObject*               parent = nullptr);
    /// Open file, parse JSON, populate scene.  Throws on errors.
    LoadedSchemaResult loadFromFile(const QString& path);
    /// Parse JSON and populate scene.  Throws on errors.
    LoadedSchemaResult loadFromJson(const boost::json::value& root,
                                    const QString&            sourcePath);
signals:
    void logged(const QString& message);
private:
    QSchematic::Scene*   scene_         = nullptr;
    NodeFactory*         nodeFactory_   = nullptr;
    SheetPresenter*      sheetPresenter_= nullptr;
    s7ui::ISchemaRunner* runner_        = nullptr;
};
} // namespace s7ui::ui
