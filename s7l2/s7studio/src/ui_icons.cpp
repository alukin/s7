// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "ui_icons.hpp"

#include <QApplication>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

#include <cmath>
#include <algorithm>
#include <utility>

namespace s7studio::ui {
namespace {
double luminance(const QColor& color)
{
    const auto linear = [](double c) { return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const auto x = luminance(a), y = luminance(b);
    return (std::max(x, y) + .05) / (std::min(x, y) + .05);
}

// Keep Run/Stop accents recognizable on either a light or dark KDE palette.
QColor visibleAccent(QColor color, const QColor& background)
{
    const QColor target = contrast(Qt::white, background) >= contrast(Qt::black, background) ? QColor(Qt::white) : QColor(Qt::black);
    for (int i = 0; i < 16 && contrast(color, background) < 3.; ++i)
        color = QColor::fromRgbF(.8 * color.redF() + .2 * target.redF(),
                                .8 * color.greenF() + .2 * target.greenF(),
                                .8 * color.blueF() + .2 * target.blueF());
    return color;
}

class PaletteIcon final : public QIconEngine {
public:
    explicit PaletteIcon(QString path, QColor accent = {}) : source_(std::move(path)), accent_(accent) {}
    QIconEngine* clone() const override { return new PaletteIcon(*this); }
    bool isNull() override { return source_.isNull(); }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    { return scaledPixmap(size, mode, state, 1.); }
    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        auto result = source_.pixmap(size, scale, QIcon::Normal, state);
        if (result.isNull()) return result;
        const auto palette = QApplication::palette();
        const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
        const auto color = mode == QIcon::Disabled || !accent_.isValid()
            ? palette.color(group, QPalette::ButtonText)
            : visibleAccent(accent_, palette.color(QPalette::Button));
        QPainter painter(&result);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(result.rect(), color);
        return result;
    }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State state) override
    { painter->drawPixmap(rect, scaledPixmap(rect.size(), mode, state, painter->device()->devicePixelRatioF())); }
private:
    QIcon source_;
    QColor accent_;
};
}

auto themedIcon(const QString& resourcePath, const QColor& color) -> QIcon
{
    return QIcon(new PaletteIcon(resourcePath, color));
}

auto icon(const QString& name) -> QIcon
{
    return QIcon(new PaletteIcon(QStringLiteral(":/icons/tabler/%1.svg").arg(name)));
}

auto icon(const QString& name, const QColor& color) -> QIcon
{
    return themedIcon(QStringLiteral(":/icons/tabler/%1.svg").arg(name), color);
}

} // namespace s7studio::ui
