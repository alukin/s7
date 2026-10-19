#include "viewers/quick_support.hpp"

#include <QQmlEngine>
#include <QQmlError>
#include <QQuickWidget>
#include <QUrl>

#include <iostream>

namespace s7studio::viewers {

auto createQuickWidget() -> QQuickWidget*
{
    auto* widget = new QQuickWidget();
    widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    return widget;
}

void loadQuickScene(QQuickWidget& widget, const QString& qmlFile, const QVariantMap& requiredProperties)
{
    widget.setInitialProperties(requiredProperties);
    widget.setSource(QUrl(QStringLiteral("qrc:/s7studio/viewers/") + qmlFile));
    for (const auto& error : widget.errors()) {
        std::cerr << "s7studio: viewer QML: " << error.toString().toStdString() << '\n';
    }
}

void unloadQuickScene(QQuickWidget* widget)
{
    if (!widget) return;
    widget->setSource(QUrl());
    delete widget;
}

} // namespace s7studio::viewers
