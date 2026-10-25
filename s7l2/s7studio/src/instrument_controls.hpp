// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include <QWidget>
#include <QStringList>
class QDoubleSpinBox;
class QDial;
class QLabel;
class QToolButton;
class QProgressBar;
class QVBoxLayout;
namespace s7studio::ui {
// A shared right column: scrollable settings, stable hover area, bottom status.
class InstrumentSidebar : public QWidget {
public:
    explicit InstrumentSidebar(const QString& name, QWidget* parent = nullptr);
    void addSection(QWidget* section);
    QLabel* readout() const { return readout_; }
    QLabel* status() const { return status_; }
    QProgressBar* busy() const { return busy_; }
private:
    QVBoxLayout* settings_;
    QLabel *readout_, *status_;
    QProgressBar* busy_;
};

QDoubleSpinBox* compactNumber(QWidget* parent);

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

// A coarse dial never owns the floating-point value. Numeric edits remain exact.
class RotaryValueControl : public QWidget {
public:
    enum class Mapping { LogGain, RelativeOffset };
    RotaryValueControl(Mapping mapping, const QString& name, QWidget* parent = nullptr);
    QDoubleSpinBox* number() const { return number_; }
    void setOffsetStep(double step);
private:
    void updateDial();
    Mapping mapping_;
    QDoubleSpinBox* number_;
    QDial* dial_;
    double offsetStep_ = .01;
    int previousPosition_ = 500;
};
}
