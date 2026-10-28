// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <QWidget>
#include <QElapsedTimer>
#include <QStringList>
#include <QStyle>
#include <QTimer>
#include <functional>
class QDoubleSpinBox;
class QDial;
class QLabel;
class QToolButton;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class QWheelEvent;
namespace s7studio::ui {
// Keep unhandled/at-limit wheel events inside the instrument controls instead
// of allowing the focused plot to take them as navigation.
class InstrumentPanel : public QWidget {
public:
    using QWidget::QWidget;
protected:
    void wheelEvent(QWheelEvent* event) override;
};
// A shared right column: scrollable settings, stable hover area, bottom status.
class InstrumentSidebar : public InstrumentPanel {
public:
    explicit InstrumentSidebar(const QString& name, QWidget* parent = nullptr);
    void addSection(QWidget* section);
    QLabel* readout() const { return readout_; }
    QLabel* status() const { return status_; }
    QProgressBar* busy() const { return busy_; }
    void setReadoutLines(int lines);
private:
    QVBoxLayout* settings_;
    QScrollArea* readoutArea_;
    QLabel *readout_, *status_;
    QProgressBar* busy_;
};

QDoubleSpinBox* compactNumber(QWidget* parent);
void compactActionButton(QPushButton* button, QStyle::StandardPixmap icon,
                         const QString& name, const QString& help);
// For editors created by compactNumber: synchronize the inactive readout too.
void setCompactNumberValueSilently(QDoubleSpinBox* number, double value);

// Runs an update at most once per interval. The first request runs at once;
// later ones within the interval coalesce into one trailing run, so the latest
// state always shows. In a window with Qt Quick plots every widget repaint
// re-composites the whole window, so viewers update status text that follows
// the 60 Hz data refresh through this at 10 Hz; user actions update it at once.
class Throttle : public QObject {
public:
    Throttle(std::function<void()> update, QObject* parent, int intervalMs = 100);
    void request();
    // Runs a pending update now, e.g. before its result is read.
    void flush();
private:
    void run();
    std::function<void()> update_;
    QElapsedTimer last_;
    QTimer trailing_;
    int intervalMs_;
};

// Labelled, keyboard-accessible alternatives to a short enum combo box.
class ChoiceButtons : public QWidget {
    Q_OBJECT
public:
    ChoiceButtons(const QStringList& labels, const QString& name, QWidget* parent = nullptr);
    int currentIndex() const;
    void setCurrentIndex(int index);
signals:
    void currentIndexChanged(int index);
private:
    QList<QToolButton*> buttons_;
};

// The content keeps its state when collapsed; each section is independent.
class CollapsibleSection : public QWidget {
public:
    CollapsibleSection(const QString& title, const QString& name, QWidget* content, bool expanded, QWidget* parent = nullptr);
};

// Settings cells wrap with the available plot width, including on hidden tabs.
class WrappingSettings : public QWidget {
public:
    explicit WrappingSettings(QWidget* parent = nullptr);
    void addField(const QString& label, QWidget* control);
    void addControl(QWidget* control);
};

// Scale dials step in engineering values; numeric edits remain exact.
class RotaryValueControl : public QWidget {
public:
    enum class Mapping { LogScale, RelativeOffset, LogTime };
    RotaryValueControl(Mapping mapping, const QString& name, QWidget* parent = nullptr);
    QDoubleSpinBox* number() const { return number_; }
    // Model/view updates must synchronize the dial without emitting an edit.
    void setValueSilently(double value);
    void setOffsetStep(double step);
private:
    void updateDial();
    void stepScale(int steps);
    QList<double> scaleStops() const;
    Mapping mapping_;
    QDoubleSpinBox* number_;
    QDial* dial_;
    QToolButton* fine_ = nullptr;
    double offsetStep_ = .01;
    int previousPosition_ = 500;
};
}
