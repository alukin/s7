// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once

#include "printing/print_document.hpp"
#include "viewers/plot_print.hpp"

#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

class QIcon;
class QMenu;
class QToolButton;
class QWidget;

namespace s7studio::viewers {

// One coherent picture of a viewer for one print job. It is captured when
// the user chooses a print action and never changes afterwards: live
// acquisition, a running model and the viewer itself keep going.
struct ViewerPrintSnapshot {
    QString title;      // e.g. "Scope · states"
    QStringList notes;  // range, rate, completeness and decimation, snapshot time
    std::vector<PlotSnapshot> plots;  // stacked top to bottom
    // Content other than plots (an image, a custom canvas); drawn below the plots.
    std::function<void(QPainter& painter, const QRectF& area)> draw;
};

// The LinePlotViews shown under `root` (hidden tabs excluded), top to bottom.
[[nodiscard]] std::vector<PlotSnapshot> visiblePlots(const QWidget* root);

// The printed document of a snapshot: one page, provenance header.
[[nodiscard]] printing::PrintDocument viewerDocument(const ViewerPrintSnapshot& snapshot, const QString& footer);

// The diagram name and file shown under each viewer printout's title; set
// once by the main window.
void setPrintContextProvider(std::function<QString()> provider);
[[nodiscard]] QString printContext();

// A small icon-only print button. A click opens the print preview (print
// from there or close); its arrow menu has Save as PDF… (no preview: a PDF
// wastes no paper) and Page Setup…. `capture` runs once per action, so a
// print shows the viewer as it is at that moment (freeze it first to choose
// the moment); an empty result is reported, not printed blank. Callers may
// add options to the menu. Object names: <prefix>.print, <prefix>.savePdf,
// <prefix>.pageSetup.
QToolButton* makePrintButton(QWidget* parent, const QString& objectPrefix, std::function<std::optional<ViewerPrintSnapshot>()> capture,
                             std::function<QString()> pdfName);

// A small checkable "freeze display" button (<prefix>.freeze) for viewers
// that update while a model runs. Frozen, the viewer stops redrawing; its
// data source keeps acquiring or recording.
QToolButton* makeFreezeButton(QWidget* parent, const QString& objectPrefix);

// Icons for these buttons, set once by the main window (palette aware).
void setViewerButtonIcons(QIcon print, QIcon freeze);

// The paper for viewer printouts: the last Page Setup choice, else the
// system default paper in landscape.
[[nodiscard]] QPageLayout viewerPageLayout();
void rememberViewerPageLayout(const QPageLayout& layout);

} // namespace s7studio::viewers
