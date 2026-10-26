// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/quick_support.hpp"

#include <QQmlEngine>
#include <QApplication>
#include <QPointer>
#include <QQmlError>
#include <QQuickWidget>
#include <QUrl>
#include <QWheelEvent>

#include <iostream>

namespace s7studio::viewers {
namespace {
class ObservationQuickWidget final : public QQuickWidget {
protected:
    void wheelEvent(QWheelEvent* event) override {
        if (forwarding_) { event->ignore(); return; }
        // Focus fallback or an ongoing scroll gesture can deliver an event to
        // this scene after the pointer entered the sidebar. Its local position
        // may be stale; use global position to route to the actual widget.
        auto* host = window();
        const auto position = host->mapFromGlobal(event->globalPosition());
        if (!QRectF(host->rect()).contains(position)) { event->ignore(); return; }
        // Only hit-test this viewer's widget tree; desktop-wide hit testing is
        // unnecessary and can depend on the window system's global geometry.
        auto* target = host->childAt(position.toPoint());
        if (!target) target = host;
        auto* modal = QApplication::activeModalWidget();
        auto* popup = QApplication::activePopupWidget();
        if ((modal && modal->window() != host) ||
            (popup && popup->window() != host)) { event->ignore(); return; }
        if (target == this || isAncestorOf(target)) {
            QQuickWidget::wheelEvent(event); return;
        }
        const QPointer<ObservationQuickWidget> alive(this);
        QPointer<QWidget> receiver(target);
        bool accepted = false;
        forwarding_ = true;
        // sendEvent is non-spontaneous: explicitly bubble through the widget
        // parents, so a label/blank content area reaches its scroll viewport.
        while (receiver && alive) {
            const QPointer<QWidget> parent(receiver->isWindow() ? nullptr : receiver->parentWidget());
            if (!receiver->isEnabled()) { receiver = parent; continue; }
            QWheelEvent forwarded(receiver->mapFromGlobal(event->globalPosition()), event->globalPosition(),
                event->pixelDelta(), event->angleDelta(), event->buttons(), event->modifiers(), event->phase(),
                event->inverted(), event->source(), event->pointingDevice());
            forwarded.setTimestamp(event->timestamp());
            forwarded.setAccepted(false);
            QApplication::sendEvent(receiver, &forwarded);
            if (forwarded.isAccepted()) { accepted = true; break; }
            receiver = parent;
        }
        if (alive) forwarding_ = false;
        event->setAccepted(accepted);
    }
private:
    bool forwarding_ = false;
};
}

auto createQuickWidget() -> QQuickWidget*
{
    auto* widget = new ObservationQuickWidget();
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
