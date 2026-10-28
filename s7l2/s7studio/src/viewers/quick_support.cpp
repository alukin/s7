// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/quick_support.hpp"

#include <QQmlEngine>
#include <QApplication>
#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QQmlError>
#include <QQuickWidget>
#include <QUrl>
#include <QWheelEvent>

#include <iostream>
#include <limits>

namespace s7studio::viewers {
namespace {
// Paces scene renders of all viewer widgets on the GUI thread, so one event
// loop pass no longer renders every plot back to back. A render's cost d runs
// from its start until the top-level repaint it causes has run (a marker posted
// behind that low-priority UpdateRequest); the next render then waits d/2, so
// rendering takes at most about two thirds of the thread and input and timers
// run between frames. Due widgets render in the order they became due; a widget under user
// interaction goes first. Scene data is never dropped: a withheld frame shows
// the latest state when it renders.
class RenderPacer final : public QObject {
public:
    static auto instance() -> RenderPacer& {
        static QPointer<RenderPacer> pacer;
        if (!pacer) pacer = new RenderPacer(QCoreApplication::instance());
        return *pacer;
    }
    [[nodiscard]] auto now() const -> qint64 { return clock_.nsecsElapsed(); }
    auto mayRender(const QObject* widget, qint64 dueAt, bool interactive) -> bool {
        const auto time = now();
        if (time < dueAt) return false;
        auto& entry = due_[widget]; if (!entry.since) entry.since = time; entry.asked = time;
        const auto since = entry.since;  // erasing below may move entries
        // Without a running event loop the marker never arrives; do not stall for it.
        if (settling_ && time - renderStarted_ > kStaleNs) settling_ = false;
        if (settling_ || time < gateOpensAt_) return false;
        if (interactive) return true;
        for (auto it = due_.begin(); it != due_.end();) {
            // A widget that stopped asking (its timer no longer runs) must not hold the others back.
            if (time - it->asked > kStaleNs) { it = due_.erase(it); continue; }
            if (it.key() != widget && it->since < since) return false;
            ++it;
        }
        return true;
    }
    void rendered(const QObject* widget, qint64 started) {
        due_.remove(widget); renderStarted_ = started; settling_ = true;
        QCoreApplication::postEvent(this, new QEvent(QEvent::User), Qt::LowEventPriority);
    }
    void forget(const QObject* widget) { due_.remove(widget); }

protected:
    bool event(QEvent* event) override {
        if (event->type() != QEvent::User) return QObject::event(event);
        const auto time = now(); settling_ = false; gateOpensAt_ = time + (time - renderStarted_) / 2;
        return true;
    }

private:
    static constexpr qint64 kStaleNs = 1'000'000'000;
    struct Due { qint64 since = 0, asked = 0; };
    explicit RenderPacer(QObject* parent) : QObject(parent) { clock_.start(); }
    QElapsedTimer clock_;
    qint64 gateOpensAt_ = 0, renderStarted_ = 0;
    bool settling_ = false;
    QHash<const QObject*, Due> due_;
};

class ObservationQuickWidget final : public QQuickWidget {
public:
    ~ObservationQuickWidget() override { RenderPacer::instance().forget(this); }
    void setMinimumFrameInterval(int milliseconds) { minimumIntervalNs_ = qint64{milliseconds} * 1'000'000; }

protected:
    bool event(QEvent* event) override {
        if (event->isInputEvent()) lastInput_ = RenderPacer::instance().now();
        return QQuickWidget::event(event);
    }
    void hideEvent(QHideEvent* event) override { RenderPacer::instance().forget(this); QQuickWidget::hideEvent(event); }
    // QQuickWidget renders from a 5 ms update timer that it stops only in
    // timerEvent(); a withheld event therefore returns 5 ms later. With
    // SizeRootObjectToView its only other timer (resize) is never started.
    void timerEvent(QTimerEvent* event) override {
        auto& pacer = RenderPacer::instance();
        if (!isVisible()) { pacer.forget(this); QQuickWidget::timerEvent(event); return; }
        const auto time = pacer.now();
        const bool interactive = time - lastInput_ < kInteractiveNs;
        if (!pacer.mayRender(this, interactive ? 0 : lastRender_ + minimumIntervalNs_, interactive)) return;
        const QPointer<ObservationQuickWidget> alive(this);
        QQuickWidget::timerEvent(event);
        if (!alive) return;
        lastRender_ = time; pacer.rendered(this, time);
    }

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
    static constexpr qint64 kInteractiveNs = 250'000'000;
    static constexpr qint64 kLongAgo = std::numeric_limits<qint64>::min() / 2;
    bool forwarding_ = false;
    qint64 minimumIntervalNs_ = 0, lastRender_ = kLongAgo, lastInput_ = kLongAgo;
};
}

auto createQuickWidget() -> QQuickWidget*
{
    auto* widget = new ObservationQuickWidget();
    widget->setResizeMode(QQuickWidget::SizeRootObjectToView);
    return widget;
}

void setMinimumFrameInterval(QQuickWidget& widget, int milliseconds)
{
    if (auto* paced = dynamic_cast<ObservationQuickWidget*>(&widget)) paced->setMinimumFrameInterval(milliseconds);
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
