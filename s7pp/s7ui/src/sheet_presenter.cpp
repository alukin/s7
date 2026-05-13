#include "sheet_presenter.hpp"
#include "scene_roles.hpp"
#include <qschematic/scene.hpp>
#include <qschematic/view.hpp>
#include <QColor>
#include <QFileDialog>
#include <QFont>
#include <QGraphicsLineItem>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QMessageBox>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPen>
#include <QPrintDialog>
#include <QPrinter>
#include <QRectF>
#include <cmath>
namespace s7ui::ui {
namespace {
constexpr double kPaperMarginMm      = 10.0;
constexpr double kTitleBlockWidthMm  = 90.0;
constexpr double kTitleBlockHeightMm = 28.0;
constexpr double kRulerTickStepMm    = 10.0;
constexpr double kRulerMajorStepMm   = 50.0;
} // namespace
SheetPresenter::SheetPresenter(QSchematic::Scene* scene)
    : scene_(scene)
{
}
// -------------------------------------------------------------------------
// Static helpers
// -------------------------------------------------------------------------
SheetSizeMm SheetPresenter::sizeForPreset(Preset preset)
{
    switch (preset) {
    case Preset::A1Landscape: return SheetSizeMm{841.0, 594.0, "A1 landscape"};
    case Preset::A2Landscape: return SheetSizeMm{594.0, 420.0, "A2 landscape"};
    case Preset::A3Landscape: return SheetSizeMm{420.0, 297.0, "A3 landscape"};
    case Preset::A4Landscape:
    default:                  return SheetSizeMm{297.0, 210.0, "A4 landscape"};
    }
}
const char* SheetPresenter::standardName(Preset preset)
{
    switch (preset) {
    case Preset::A1Landscape: return "A1";
    case Preset::A2Landscape: return "A2";
    case Preset::A3Landscape: return "A3";
    case Preset::A4Landscape:
    default:                  return "A4";
    }
}
SheetPresenter::Preset SheetPresenter::presetFromStandard(const QString& standard)
{
    const auto s = standard.trimmed().toUpper();
    if (s == "A1") return Preset::A1Landscape;
    if (s == "A2") return Preset::A2Landscape;
    if (s == "A3") return Preset::A3Landscape;
    return Preset::A4Landscape;
}
QString SheetPresenter::currentStandardName() const
{
    return QString::fromLatin1(standardName(preset_));
}
// -------------------------------------------------------------------------
// Preset application
// -------------------------------------------------------------------------
void SheetPresenter::applyPreset(Preset preset,
                                  const std::function<void(const QString&)>& logCallback)
{
    preset_ = preset;
    const auto sheet = sizeForPreset(preset_);
    if (scene_) {
        scene_->setSceneRect(0.0, 0.0, sheet.width, sheet.height);
    }
    rebuildOverlay();
    if (logCallback) {
        logCallback(QString("[sheet] %1 (%2 x %3 mm), units=mm")
            .arg(sheet.label)
            .arg(sheet.width,  0, 'f', 0)
            .arg(sheet.height, 0, 'f', 0));
    }
}
void SheetPresenter::setSchemaName(const QString& name)
{
    schemaName_ = name;
    rebuildOverlay();
}
// -------------------------------------------------------------------------
// Paper overlay
// -------------------------------------------------------------------------
void SheetPresenter::rebuildOverlay()
{
    if (!scene_) return;
    // Remove previous paper overlay primitives.
    const auto rawItems = scene_->QGraphicsScene::items();
    for (auto* item : rawItems) {
        if (!item) continue;
        if (item->data(kRolePaperOverlay).toInt() == 1) {
            scene_->QGraphicsScene::removeItem(item);
            delete item;
        }
    }
    const QRectF sheet = scene_->sceneRect();
    if (sheet.isEmpty()) return;
    const auto mkOverlay = [](QGraphicsItem* item) {
        item->setData(kRolePaperOverlay, 1);
        item->setAcceptedMouseButtons(Qt::NoButton);
        item->setFlag(QGraphicsItem::ItemIsSelectable, false);
        item->setFlag(QGraphicsItem::ItemIsMovable,    false);
        item->setZValue(-500.0);
    };
    scene_->setBackgroundBrush(QColor(170, 170, 170));
    // White paper fill
    auto* paperFill = new QGraphicsRectItem(sheet);
    paperFill->setPen(Qt::NoPen);
    paperFill->setBrush(QColor(250, 250, 250));
    mkOverlay(paperFill);
    paperFill->setZValue(-650.0);
    scene_->QGraphicsScene::addItem(paperFill);
    // Sheet border
    auto* border = new QGraphicsRectItem(sheet);
    border->setPen(QPen(QColor(30, 30, 30), 0.7));
    border->setBrush(Qt::NoBrush);
    mkOverlay(border);
    scene_->QGraphicsScene::addItem(border);
    // Print margin dashes
    const QRectF printMargin = sheet.adjusted(kPaperMarginMm, kPaperMarginMm,
                                               -kPaperMarginMm, -kPaperMarginMm);
    auto* marginRect = new QGraphicsRectItem(printMargin);
    QPen marginPen(QColor(90, 90, 90), 0.4);
    marginPen.setStyle(Qt::DashLine);
    marginRect->setPen(marginPen);
    marginRect->setBrush(Qt::NoBrush);
    mkOverlay(marginRect);
    scene_->QGraphicsScene::addItem(marginRect);
    // Horizontal ruler (top edge)
    for (double x = 0.0; x <= sheet.width() + 0.001; x += kRulerTickStepMm) {
        const bool major = std::fmod(x, kRulerMajorStepMm) < 0.001;
        const double tickLen = major ? 6.0 : 3.0;
        auto* tick = new QGraphicsLineItem(x, sheet.top(), x, sheet.top() + tickLen);
        tick->setPen(QPen(QColor(75, 75, 75), 0));
        mkOverlay(tick);
        tick->setZValue(-470.0);
        scene_->QGraphicsScene::addItem(tick);
        if (major) {
            auto* lbl = new QGraphicsSimpleTextItem(QString::number(static_cast<int>(x)));
            QFont f = lbl->font(); f.setPointSizeF(3.0); lbl->setFont(f);
            lbl->setBrush(QBrush(QColor(70, 70, 70)));
            lbl->setPos(x + 1.5, sheet.top() + tickLen + 0.5);
            mkOverlay(lbl); lbl->setZValue(-465.0);
            scene_->QGraphicsScene::addItem(lbl);
        }
    }
    // Vertical ruler (left edge)
    for (double y = 0.0; y <= sheet.height() + 0.001; y += kRulerTickStepMm) {
        const bool major = std::fmod(y, kRulerMajorStepMm) < 0.001;
        const double tickLen = major ? 6.0 : 3.0;
        auto* tick = new QGraphicsLineItem(sheet.left(), y, sheet.left() + tickLen, y);
        tick->setPen(QPen(QColor(75, 75, 75), 0));
        mkOverlay(tick);
        tick->setZValue(-470.0);
        scene_->QGraphicsScene::addItem(tick);
        if (major) {
            auto* lbl = new QGraphicsSimpleTextItem(QString::number(static_cast<int>(y)));
            QFont f = lbl->font(); f.setPointSizeF(3.0); lbl->setFont(f);
            lbl->setBrush(QBrush(QColor(70, 70, 70)));
            lbl->setPos(sheet.left() + tickLen + 0.5, y - 3.0);
            mkOverlay(lbl); lbl->setZValue(-465.0);
            scene_->QGraphicsScene::addItem(lbl);
        }
    }
    // Title block (bottom-right)
    const QRectF titleBlock(
        sheet.right()  - kPaperMarginMm - kTitleBlockWidthMm,
        sheet.bottom() - kPaperMarginMm - kTitleBlockHeightMm,
        kTitleBlockWidthMm,
        kTitleBlockHeightMm);
    auto* titleRect = new QGraphicsRectItem(titleBlock);
    titleRect->setPen(QPen(QColor(45, 45, 45), 0.5));
    titleRect->setBrush(Qt::NoBrush);
    mkOverlay(titleRect);
    scene_->QGraphicsScene::addItem(titleRect);
    const auto sheetInfo = sizeForPreset(preset_);
    auto* titleText = new QGraphicsSimpleTextItem(
        QString("%1\n%2\nunits: mm")
            .arg(schemaName_.trimmed().isEmpty() ? QStringLiteral("untitled") : schemaName_.trimmed())
            .arg(sheetInfo.label));
    QFont font = titleText->font();
    font.setPointSizeF(4.0);
    titleText->setFont(font);
    titleText->setBrush(QBrush(QColor(20, 20, 20)));
    titleText->setPos(titleBlock.left() + 3.0, titleBlock.top() + 2.0);
    mkOverlay(titleText);
    titleText->setZValue(-490.0);
    scene_->QGraphicsScene::addItem(titleText);
}
// -------------------------------------------------------------------------
// View helpers
// -------------------------------------------------------------------------
void SheetPresenter::fitInView(QSchematic::View* view) const
{
    if (view) view->fitInView();
}
void SheetPresenter::fitSelectionInView(QSchematic::View* view) const
{
    if (!view || !scene_) return;
    const auto selected = scene_->QGraphicsScene::selectedItems();
    if (!selected.empty()) {
        QRectF bounds;
        for (auto* item : selected) {
            bounds = bounds.united(item->sceneBoundingRect());
        }
        if (!bounds.isEmpty()) {
            static_cast<QGraphicsView*>(view)->fitInView(bounds, Qt::KeepAspectRatio);
            return;
        }
    }
    fitInView(view);
}
// -------------------------------------------------------------------------
// Export / print
// -------------------------------------------------------------------------
void SheetPresenter::exportPdf(const QString& suggestedPath, QWidget* errorParent) const
{
    if (!scene_ || suggestedPath.isEmpty()) return;
    const auto sheet = sizeForPreset(preset_);
    QPdfWriter writer(suggestedPath);
    writer.setResolution(300);
    writer.setPageSize(QPageSize(QSizeF(sheet.width, sheet.height),
                                 QPageSize::Millimeter, "s7_sheet"));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
    QPainter painter(&writer);
    if (!painter.isActive()) {
        QMessageBox::warning(errorParent, "Export PDF Failed",
                             "Cannot create PDF painter for output file.");
        return;
    }
    scene_->render(&painter,
                   QRectF(0, 0, writer.width(), writer.height()),
                   scene_->sceneRect(),
                   Qt::IgnoreAspectRatio);
    painter.end();
}
void SheetPresenter::printSheet(QWidget* parent) const
{
    if (!scene_) return;
    const auto sheet = sizeForPreset(preset_);
    QPrinter printer(QPrinter::HighResolution);
    printer.setPageSize(QPageSize(QSizeF(sheet.width, sheet.height),
                                  QPageSize::Millimeter, "s7_sheet"));
    printer.setFullPage(true);
    QPrintDialog dialog(&printer, parent);
    if (dialog.exec() != QDialog::Accepted) return;
    QPainter painter(&printer);
    if (!painter.isActive()) {
        QMessageBox::warning(parent, "Print Failed",
                             "Cannot initialize printer painter.");
        return;
    }
    scene_->render(&painter,
                   QRectF(0, 0, printer.width(), printer.height()),
                   scene_->sceneRect(),
                   Qt::IgnoreAspectRatio);
    painter.end();
}
} // namespace s7ui::ui
