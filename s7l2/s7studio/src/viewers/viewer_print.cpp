// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "viewers/viewer_print.hpp"

#include "printing/preview_dialog.hpp"
#include "viewers/line_plot_view.hpp"

#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QIcon>
#include <QStyle>
#include <QToolButton>
#include <QSettings>

#include <algorithm>

namespace s7studio::viewers {
namespace {

std::function<QString()>& contextProvider()
{
    static std::function<QString()> provider;
    return provider;
}

constexpr auto kPaperWidth = "printing/viewerPaperWidthMm";
constexpr auto kPaperHeight = "printing/viewerPaperHeightMm";
constexpr auto kPaperLandscape = "printing/viewerPaperLandscape";
constexpr auto kLastDirectory = "printing/lastDirectory";

QIcon& printIcon()
{
    static QIcon icon;
    return icon;
}

QIcon& freezeIcon()
{
    static QIcon icon;
    return icon;
}

void makeCompact(QToolButton* button, const QIcon& icon, const QString& fallback)
{
    button->setAutoRaise(true);
    button->setIconSize(QSize(18, 18));
    if (icon.isNull()) button->setText(fallback);
    else button->setIcon(icon);
}

} // namespace

std::vector<PlotSnapshot> visiblePlots(const QWidget* root)
{
    std::vector<std::pair<QPoint, const LinePlotView*>> found;
    for (const auto* view : root->findChildren<const LinePlotView*>()) {
        if (!view->isVisible() || view->width() < 8 || view->height() < 8) continue;
        found.emplace_back(view->mapTo(root, QPoint(0, 0)), view);
    }
    std::ranges::sort(found, [](const auto& a, const auto& b) { return a.first.y() != b.first.y() ? a.first.y() < b.first.y() : a.first.x() < b.first.x(); });
    std::vector<PlotSnapshot> plots;
    for (const auto& entry : found) plots.push_back(snapshotPlot(*entry.second));
    return plots;
}

printing::PrintDocument viewerDocument(const ViewerPrintSnapshot& snapshot, const QString& footer)
{
    printing::PrintDocument document;
    document.name = snapshot.title;
    document.footer = footer;
    printing::PrintPage page;
    page.title = snapshot.title;
    page.subtitle = printContext();
    page.notes = snapshot.notes;
    page.draw = [plots = snapshot.plots, draw = snapshot.draw](QPainter& painter, const QRectF& area) {
        const int count = static_cast<int>(plots.size()) + (draw ? 1 : 0);
        if (count == 0) return;
        constexpr double gap = 12;
        const double height = (area.height() - gap * (count - 1)) / count;
        double top = area.top();
        for (const auto& plot : plots) {
            paintPlot(painter, QRectF(area.left(), top, area.width(), height), plot);
            top += height + gap;
        }
        if (draw) draw(painter, QRectF(area.left(), top, area.width(), height));
    };
    document.pages.push_back(std::move(page));
    return document;
}

void setPrintContextProvider(std::function<QString()> provider)
{
    contextProvider() = std::move(provider);
}

QString printContext()
{
    return contextProvider() ? contextProvider()() : QString();
}

void rememberViewerPageLayout(const QPageLayout& layout)
{
    QSettings settings;
    const auto size = layout.pageSize().size(QPageSize::Millimeter);
    settings.setValue(kPaperWidth, size.width());
    settings.setValue(kPaperHeight, size.height());
    settings.setValue(kPaperLandscape, layout.orientation() == QPageLayout::Landscape);
}

QPageLayout viewerPageLayout()
{
    const QSettings settings;
    const double width = settings.value(kPaperWidth).toDouble(), height = settings.value(kPaperHeight).toDouble();
    if (width > 0 && height > 0) return printing::sheetLayout(width, height, settings.value(kPaperLandscape, true).toBool());
    auto layout = printing::defaultLayout();
    layout.setOrientation(QPageLayout::Landscape);
    return layout;
}

QToolButton* makePrintButton(QWidget* parent, const QString& objectPrefix, std::function<std::optional<ViewerPrintSnapshot>()> capture,
                             std::function<QString()> pdfName)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectPrefix + QStringLiteral(".print"));
    makeCompact(button, printIcon().isNull() ? QIcon::fromTheme(QStringLiteral("document-print")) : printIcon(), QStringLiteral("Print"));
    button->setToolTip(QStringLiteral("Print what this viewer shows now (opens a preview); the arrow saves a PDF"));
    button->setAccessibleName(QStringLiteral("Print"));
    button->setPopupMode(QToolButton::MenuButtonPopup);
    const auto take = [parent, capture]() -> std::optional<printing::DocumentBuilder> {
        const auto snapshot = capture();
        if (!snapshot || (snapshot->plots.empty() && !snapshot->draw)) {
            QMessageBox::information(parent, QStringLiteral("Print"), QStringLiteral("This viewer has nothing to print yet."));
            return std::nullopt;
        }
        return [shot = *snapshot, footer = printing::standardFooter()](const QPageLayout&) { return viewerDocument(shot, footer); };
    };
    const auto suggested = [pdfName] {
        const QSettings settings;
        return QDir(settings.value(kLastDirectory, QDir::homePath()).toString()).filePath(pdfName() + QStringLiteral(".pdf"));
    };
    const auto remember = [](const printing::PrintResult& result) {
        if (result.done() && result.message.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
            QSettings().setValue(kLastDirectory, QFileInfo(result.message).absolutePath());
    };
    // A click previews; Print… in the preview goes to the printer.
    QObject::connect(button, &QToolButton::clicked, button, [parent, take, suggested, remember] {
        const auto build = take();
        if (!build) return;
        printing::PreviewDialog dialog(*build, viewerPageLayout(), suggested(), printing::PreviewDialog::Mode::Preview, parent);
        dialog.exec();
        remember(dialog.result());
        rememberViewerPageLayout(dialog.pageLayout());
    });
    auto* menu = new QMenu(button);
    menu->addAction(QStringLiteral("Save as PDF…"), button, [parent, take, suggested, remember] {
        const auto build = take();
        if (!build) return;
        const auto result = printing::exportPdf(parent, *build, viewerPageLayout(), suggested());
        remember(result);
        if (result.outcome == printing::PrintResult::Outcome::Failed) QMessageBox::warning(parent, QStringLiteral("Save as PDF"), result.message);
    })->setObjectName(objectPrefix + QStringLiteral(".savePdf"));
    menu->addAction(QStringLiteral("Page Setup…"), button, [parent] {
        if (const auto chosen = printing::pageSetup(parent, viewerPageLayout())) rememberViewerPageLayout(*chosen);
    })->setObjectName(objectPrefix + QStringLiteral(".pageSetup"));
    button->setMenu(menu);
    return button;
}

QToolButton* makeFreezeButton(QWidget* parent, const QString& objectPrefix)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectPrefix + QStringLiteral(".freeze"));
    makeCompact(button, freezeIcon().isNull() ? parent->style()->standardIcon(QStyle::SP_MediaPause) : freezeIcon(), QStringLiteral("Freeze"));
    button->setCheckable(true);
    button->setToolTip(QStringLiteral("Freeze the display (acquisition continues); print or inspect the frozen view"));
    button->setAccessibleName(QStringLiteral("Freeze display"));
    QObject::connect(button, &QToolButton::toggled, button, [button](bool frozen) {
        button->setAccessibleName(frozen ? QStringLiteral("Unfreeze display") : QStringLiteral("Freeze display"));
    });
    return button;
}

void setViewerButtonIcons(QIcon print, QIcon freeze)
{
    printIcon() = std::move(print);
    freezeIcon() = std::move(freeze);
}

} // namespace s7studio::viewers
