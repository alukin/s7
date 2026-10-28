// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#pragma once

#include <QString>
#include <QVariantMap>

class QQuickWidget;
class QWidget;

namespace s7studio::viewers {

// Creates the QQuickWidget for one viewer's QML scene. Each viewer instance
// gets its own widget and engine, so instances share no QML state. It is
// created without a parent: constructing it as the child of a visible widget
// processes events (re-entering a showEvent that creates the scene), so the
// caller stores the pointer first and then adds it to its layout.
auto createQuickWidget() -> QQuickWidget*;

// Scene renders of these widgets are paced on the GUI thread so plots do not
// all render in the same event-loop pass (see RenderPacer). This sets the
// shortest interval between one widget's renders, e.g. for small previews;
// 0, the default, leaves only the shared pacing. User input on the widget
// overrides the interval for a moment.
void setMinimumFrameInterval(QQuickWidget& widget, int milliseconds);

// Loads a scene (a file under qrc:/s7studio/viewers/) whose root declares the
// given required properties, e.g. the viewer's C++ bridge object. Separate
// from creation: loading may process events, so the caller stores the widget
// first. QML errors are reported on stderr; the widget then stays empty.
void loadQuickScene(QQuickWidget& widget, const QString& qmlFile, const QVariantMap& requiredProperties);

// Destroys the scene now, then the widget. Deleting a QQuickWidget alone
// leaves its QML objects to a deferred deletion, after the C++ objects their
// bindings refer to may be gone. Accepts nullptr.
void unloadQuickScene(QQuickWidget* widget);

} // namespace s7studio::viewers
