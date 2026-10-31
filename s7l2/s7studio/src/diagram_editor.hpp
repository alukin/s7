// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include "diagram_document.hpp"

#include <QWidget>
#include <boost/json.hpp>
#include <functional>
#include <memory>
#include <optional>

class QGraphicsItem;
class QGraphicsRectItem;
class QWidget;

namespace QSchematic {
class View;
}

namespace s7studio::diagram {

class DiagramRuler;
class DiagramScene;

// Minimal diagram editor widget
// Embeds a QSchematic scene for visual editing of the schema diagram
class DiagramEditor : public QWidget {
    Q_OBJECT

public:
    explicit DiagramEditor(QWidget* parent = nullptr);
    ~DiagramEditor() override;

    void clearDiagram();
    // Both throw std::runtime_error when the document cannot be laid out;
    // the editor is then empty.
    void loadDocument(const DiagramDocument& document, const std::function<void(int)>& progress = {});
    void loadRuntimeSchema(const boost::json::value& schemaJson);
    // Selects the item and scrolls it into the middle of the view.
    void showItem(QGraphicsItem* item);
    void fitToContents();
    void fitPageInView();
    // Zooms so that the diagram's elements, text frames and wires fill the view (the page if empty).
    void fitDiagramInView();
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void copySelection();
    void cutSelection();
    void pasteSelection();
    void selectAll();
    void deleteSelection();
    void editSelectedElement();
    // Shared placement for library double-click and canvas drops. A missing
    // position uses the normal free-spot suggestion; supplied positions snap.
    bool addLibraryElement(const QString& identifier, std::optional<QPointF> scenePosition = std::nullopt);
    // The diagram's rate in Hz, recorded as the design rate of rate-designed
    // elements (FIR/IIR coefficients) when they are added (ADR-022).
    void setDesignRateSource(std::function<double()> rateHz) { designRateHz_ = std::move(rateHz); }
    void setPageLayout(const DiagramPageLayout& pageLayout);
    [[nodiscard]] auto pageLayout() const -> DiagramPageLayout;

    // A free spot for a new item of the given size: grid-aligned, not overlapping an existing node and
    // inside the part of the page currently visible in the view (the whole page if nothing is visible).
    [[nodiscard]] auto suggestedNewItemPosition(QSizeF itemSize) const -> QPointF;

    [[nodiscard]] DiagramScene* scene();
    [[nodiscard]] const DiagramScene* scene() const;

signals:
    void elementNodeDoubleClicked(const QString& elementId, const QString& elementType, int channelCount);
    void elementPropertiesRequested(const QString& elementId);
    void libraryElementAdded(const QString& elementId);

private:
    void scheduleInitialPageFit();
    void rebuildPageFrame();
    void updateSceneRectFromPage();
    void updateRulerLayout();
    void updateRulers();
    void zoomBy(double factor);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    std::function<double()> designRateHz_;
    std::unique_ptr<DiagramScene> _scene;
    QSchematic::View* _view = nullptr;
    bool _pendingPageFit = false;
    DiagramPageLayout pageLayout_;
    QGraphicsRectItem* pageFrame_ = nullptr;
    DiagramRuler* horizontalRuler_ = nullptr;
    DiagramRuler* verticalRuler_ = nullptr;
    QWidget* rulerCorner_ = nullptr;
};

} // namespace s7studio::diagram
