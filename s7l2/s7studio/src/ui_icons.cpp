#include "ui_icons.hpp"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

#include <vector>

namespace s7studio::ui {

auto themedIcon(const QString& resourcePath, const QColor& color) -> QIcon
{
    QIcon source(resourcePath);
    if (source.isNull()) {
        return source;
    }

    QIcon themed;
    const std::vector<int> sizes{16, 18, 20, 24, 28, 32};
    for (const int size : sizes) {
        QPixmap base = source.pixmap(size, size);
        if (base.isNull()) {
            continue;
        }

        QPixmap normal = base;
        {
            QPainter painter(&normal);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(normal.rect(), color);
        }
        themed.addPixmap(normal, QIcon::Normal, QIcon::Off);
        themed.addPixmap(normal, QIcon::Active, QIcon::Off);
        themed.addPixmap(normal, QIcon::Selected, QIcon::Off);

        QPixmap disabled = base;
        {
            QColor disabledColor = color;
            disabledColor.setAlpha(110);
            QPainter painter(&disabled);
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(disabled.rect(), disabledColor);
        }
        themed.addPixmap(disabled, QIcon::Disabled, QIcon::Off);
    }

    return themed.isNull() ? source : themed;
}

auto icon(const QString& name) -> QIcon
{
    return themedIcon(QStringLiteral(":/icons/tabler/%1.svg").arg(name), QApplication::palette().color(QPalette::ButtonText));
}

auto icon(const QString& name, const QColor& color) -> QIcon
{
    return themedIcon(QStringLiteral(":/icons/tabler/%1.svg").arg(name), color);
}

} // namespace s7studio::ui
