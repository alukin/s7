#pragma once
#include <QString>
#include <functional>
class QWidget;
namespace QSchematic { class Scene; class View; }
namespace s7ui::ui {
struct SheetSizeMm {
    double width  = 297.0;
    double height = 210.0;
    const char* label = "A4 landscape";
};
/// Manages all sheet-related concerns:
/// paper size presets, graphical paper overlay, PDF export and printing.
/// Single Responsibility: knows about paper geometry and its visual representation.
class SheetPresenter
{
public:
    enum class Preset { A4Landscape, A3Landscape, A2Landscape, A1Landscape };
    explicit SheetPresenter(QSchematic::Scene* scene);
    void applyPreset(Preset preset, const std::function<void(const QString&)>& logCallback = {});
    [[nodiscard]] Preset      currentPreset()       const { return preset_; }
    [[nodiscard]] SheetSizeMm currentSize()         const { return sizeForPreset(preset_); }
    [[nodiscard]] QString     currentStandardName() const;
    void setSchemaName(const QString& name);
    void rebuildOverlay();
    void fitInView(QSchematic::View* view) const;
    void fitSelectionInView(QSchematic::View* view) const;
    void exportPdf(const QString& suggestedPath, QWidget* errorParent) const;
    void printSheet(QWidget* parent) const;
    [[nodiscard]] static SheetSizeMm sizeForPreset(Preset preset);
    [[nodiscard]] static const char* standardName(Preset preset);
    [[nodiscard]] static Preset      presetFromStandard(const QString& standard);
private:
    QSchematic::Scene* scene_  = nullptr;
    Preset             preset_ = Preset::A4Landscape;
    QString            schemaName_;
};
} // namespace s7ui::ui
