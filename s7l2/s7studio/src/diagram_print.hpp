// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include "diagram_document.hpp"
#include "printing/print_document.hpp"

#include <QRectF>
#include <QString>

#include <memory>
#include <vector>

namespace s7studio::diagram {

class DiagramScene;

struct DiagramPrintOptions {
    enum class Area { Diagram, Selection };
    Area area = Area::Diagram;
    // Fit the area on one page, or tile it across pages at `tileScale`
    // (1.0 = 100 %: one scene unit is 1/96 inch on paper).
    bool tile = false;
    double tileScale = 1.0;
    // Append every nested subsystem level, each on its own page(s).
    bool includeSubsystems = false;
};

// Diagram printing (P01). The job holds an immutable copy of the printed
// level and, on request, of its nested subsystems: editing, running or
// closing the diagram afterwards does not change what prints, and building
// the job never changes the diagram.
class DiagramPrintJob {
public:
    // Copies `scene` (the level shown in a diagram tab). `title` names the
    // diagram, `levelPath` the level ("" for the top level, "arm/accel" for a
    // nested one), `file` the saved file (may be empty). The selection bounds
    // are taken from `scene` now. Nested subsystems are copied as well; if one
    // cannot be laid out, subsystemError() says why and none are offered.
    DiagramPrintJob(const DiagramScene& scene, const DiagramPageLayout& sheet, QString title, QString levelPath, QString file);
    ~DiagramPrintJob();
    DiagramPrintJob(const DiagramPrintJob&) = delete;
    DiagramPrintJob& operator=(const DiagramPrintJob&) = delete;

    [[nodiscard]] bool hasSelection() const { return !selection_.isEmpty(); }
    [[nodiscard]] QRectF selectionBounds() const { return selection_; }
    // Nested subsystem levels below the printed one (all depths).
    [[nodiscard]] int subsystemCount() const { return static_cast<int>(levels_.size()) - 1; }
    [[nodiscard]] const QString& subsystemError() const { return subsystemError_; }
    // The paper of the diagram's own sheet (size and orientation).
    [[nodiscard]] QPageLayout sheetLayout() const;

    // The pages for `layout`. `footer` is the page footer's left text.
    [[nodiscard]] printing::PrintDocument build(const QPageLayout& layout, const DiagramPrintOptions& options, const QString& footer) const;

    // Scene rectangle printed for a level in "whole diagram" mode: its sheet
    // and everything drawn on it, including items outside the sheet.
    [[nodiscard]] static QRectF diagramBounds(const DiagramScene& scene, const DiagramPageLayout& sheet);
    // Bounds of the selected items, empty when nothing is selected.
    [[nodiscard]] static QRectF selectedBounds(const DiagramScene& scene);

private:
    struct Level {
        QString path;
        DiagramPageLayout sheet;
        std::shared_ptr<DiagramScene> scene;  // shared with the pages that draw it
    };
    void addSubsystems(const DiagramScene& scene, QString path, int depth);  // path by value: levels_ grows

    QString title_;
    QString file_;
    QRectF selection_;
    QString subsystemError_;
    std::vector<Level> levels_;  // [0] is the printed level, then nested levels depth first
};

} // namespace s7studio::diagram
