// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "diagram_print.hpp"

#include "diagram/diagram_scene.hpp"
#include "diagram/items.hpp"
#include "printing/print_actions.hpp"
#include "scene_serializer.hpp"
#include "wire_type_labels.hpp"

#include <qschematic/background.hpp>
#include <qschematic/settings.hpp>

#include <QPainter>

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace s7studio::diagram {
namespace {

constexpr double kSceneUnitsPerMm = printing::kPrintDpi / 25.4;
constexpr double kSelectionMargin = 10;   // scene units around a selection
constexpr double kTileOverlapMm = 10;     // paper overlap between neighbouring tiles
constexpr int kMaxNestingDepth = 32;      // diagram files are untrusted

QRectF sheetRect(const DiagramPageLayout& sheet)
{
    const double longSide = std::max(sheet.widthMm, sheet.heightMm) * kSceneUnitsPerMm;
    const double shortSide = std::min(sheet.widthMm, sheet.heightMm) * kSceneUnitsPerMm;
    return sheet.orientation == PageOrientation::Portrait ? QRectF(0, 0, shortSide, longSide) : QRectF(0, 0, longSide, shortSide);
}

QRectF itemBounds(const QGraphicsItem& item)
{
    return item.sceneBoundingRect() | item.mapRectToScene(item.childrenBoundingRect());
}

// A printing copy: the same content, no grid, wire type labels as in the editor.
std::shared_ptr<DiagramScene> copyOf(const DiagramDocument& document)
{
    auto scene = std::make_shared<DiagramScene>();
    QSchematic::Settings settings;
    settings.showGrid = false;
    scene->setSettings(settings);
    SceneSerializer{}.deserialize(document, *scene);
    static_cast<void>(updateWireTypeLabels(*scene));
    scene->clearSelection();
    return scene;
}

DiagramDocument nestedDocument(const boost::json::value& schema)
{
    if (schema.as_object().contains("diagram")) return DiagramDocument::fromJson(schema);
    DiagramDocument document;
    document.runtimeSchema = schema;
    return document;
}

QString percent(double scale)
{
    return QStringLiteral("%1 %").arg(QString::number(scale * 100, 'f', scale * 100 < 10 ? 1 : 0));
}

} // namespace

DiagramPrintJob::DiagramPrintJob(const DiagramScene& scene, const DiagramPageLayout& sheet, QString title, QString levelPath, QString file)
    : title_(std::move(title)), file_(std::move(file)), selection_(selectedBounds(scene))
{
    SerializationOptions options;
    options.sheetOrientation = sheet.orientation;
    options.sheetWidthMm = sheet.widthMm;
    options.sheetHeightMm = sheet.heightMm;
    auto document = SceneSerializer{}.serialize(scene, options);
    document.pageLayout = sheet;
    levels_.push_back({std::move(levelPath), sheet, copyOf(document)});
    if (!selection_.isEmpty()) selection_.adjust(-kSelectionMargin, -kSelectionMargin, kSelectionMargin, kSelectionMargin);
    try {
        addSubsystems(*levels_.front().scene, levels_.front().path, 1);
    } catch (const std::exception& error) {
        subsystemError_ = QString::fromUtf8(error.what());
        levels_.resize(1);
    }
}

DiagramPrintJob::~DiagramPrintJob() = default;

void DiagramPrintJob::addSubsystems(const DiagramScene& scene, QString path, int depth)
{
    if (depth > kMaxNestingDepth) throw std::runtime_error("Subsystems are nested more than 32 levels deep.");
    for (const auto& node : scene.elementNodes()) {
        const auto& schema = node->element().schema;
        if (!schema.is_object()) continue;
        const auto childPath = (path.isEmpty() ? QString() : path + QLatin1Char('/')) + QString::fromStdString(node->id());
        try {
            auto document = nestedDocument(schema);
            Level level{childPath, document.pageLayout, copyOf(document)};
            levels_.push_back(std::move(level));
        } catch (const std::exception& error) {
            throw std::runtime_error("Subsystem " + childPath.toStdString() + ": " + error.what());
        }
        addSubsystems(*levels_.back().scene, childPath, depth + 1);
    }
}

QPageLayout DiagramPrintJob::sheetLayout() const
{
    const auto& sheet = levels_.front().sheet;
    return printing::sheetLayout(sheet.widthMm, sheet.heightMm, sheet.orientation == PageOrientation::Landscape);
}

QRectF DiagramPrintJob::diagramBounds(const DiagramScene& scene, const DiagramPageLayout& sheet)
{
    QRectF bounds = sheetRect(sheet);
    for (const auto* item : scene.QGraphicsScene::items()) {
        if (item->parentItem() || !item->isVisible() || dynamic_cast<const QSchematic::Background*>(item)) continue;
        bounds |= itemBounds(*item);
    }
    return bounds;
}

QRectF DiagramPrintJob::selectedBounds(const DiagramScene& scene)
{
    QRectF bounds;
    for (const auto* item : scene.QGraphicsScene::selectedItems()) {
        if (dynamic_cast<const QSchematic::Background*>(item)) continue;
        bounds = bounds.isEmpty() ? itemBounds(*item) : bounds | itemBounds(*item);
    }
    return bounds;
}

printing::PrintDocument DiagramPrintJob::build(const QPageLayout& layout, const DiagramPrintOptions& options, const QString& footer) const
{
    printing::PrintDocument document;
    document.name = title_;
    document.footer = footer;
    const auto levelCount = options.includeSubsystems ? static_cast<std::size_t>(subsystemCount()) + 1 : 1;
    const bool selection = options.area == DiagramPrintOptions::Area::Selection && !selection_.isEmpty();
    for (std::size_t index = 0; index < levelCount; ++index) {
        const auto& level = levels_[index];
        const auto scene = level.scene;
        // A selection belongs to the printed level; nested levels print whole.
        const auto region = selection && index == 0 ? selection_ : diagramBounds(*scene, level.sheet);
        printing::PrintPage page;
        page.title = level.path.isEmpty() ? title_ : QStringLiteral("%1 ▸ %2").arg(title_, level.path);
        QStringList subtitle;
        if (!file_.isEmpty()) subtitle << file_;
        subtitle << (level.path.isEmpty() ? QStringLiteral("Top level") : QStringLiteral("Subsystem %1").arg(level.path));
        page.subtitle = subtitle.join(QStringLiteral(" · "));
        const QString what = selection && index == 0 ? QStringLiteral("Selection") : QStringLiteral("Whole diagram");
        const auto area = printing::contentArea(layout, page);

        if (!options.tile) {
            const double scale = std::min(area.width() / region.width(), area.height() / region.height());
            const QSizeF size = region.size() * scale;
            const QRectF target(area.left() + (area.width() - size.width()) / 2, area.top() + (area.height() - size.height()) / 2,
                                size.width(), size.height());
            page.detail = QStringLiteral("%1 · fit to page, scale %2").arg(what, percent(scale));
            page.draw = [scene, target, region](QPainter& painter, const QRectF&) {
                scene->render(&painter, target, region, Qt::KeepAspectRatio);
            };
            document.pages.push_back(std::move(page));
            continue;
        }

        const double scale = std::clamp(options.tileScale, 0.05, 10.0);
        const QSizeF tile = area.size() / scale;
        const double overlap = std::min(kTileOverlapMm * kSceneUnitsPerMm / scale, std::min(tile.width(), tile.height()) / 4);
        const auto count = [overlap](double extent, double step) {
            return extent <= step ? 1 : static_cast<int>(std::ceil((extent - overlap) / (step - overlap) - 1e-9));
        };
        const int columns = count(region.width(), tile.width());
        const int rows = count(region.height(), tile.height());
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const QRectF source(region.left() + column * (tile.width() - overlap), region.top() + row * (tile.height() - overlap),
                                    tile.width(), tile.height());
                auto tilePage = page;
                tilePage.detail = QStringLiteral("%1 · tile %2 of %3 (row %4, column %5) · scale %6")
                    .arg(what).arg(row * columns + column + 1).arg(rows * columns).arg(row + 1).arg(column + 1).arg(percent(scale));
                const QRectF target(area.topLeft(), source.size() * scale);
                tilePage.draw = [scene, target, source](QPainter& painter, const QRectF&) {
                    scene->render(&painter, target, source, Qt::IgnoreAspectRatio);
                };
                document.pages.push_back(std::move(tilePage));
            }
        }
    }
    return document;
}

} // namespace s7studio::diagram
