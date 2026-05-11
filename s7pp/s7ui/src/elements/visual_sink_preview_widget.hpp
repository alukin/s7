#pragma once

#include <QWidget>
#include <QVector>

class QCustomPlot;
class QCPGraph;
class QTimer;

namespace s7ui::elements {

class VisualSinkPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    enum class SignalMode
    {
        Flat,
        Synthetic
    };

    explicit VisualSinkPreviewWidget(bool compact, QWidget* parent = nullptr);

    void setInputCount(int count);
    int inputCount() const;

    void setGain(double gain);
    void setTimeWindow(double seconds);
    void setSignalMode(SignalMode mode);
    void setStaticLevels(const QVector<double>& levels);

Q_SIGNALS:
    void doubleClicked();

protected:
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void rebuildGraphs();
    void appendSample();

    QCustomPlot* plot_ = nullptr;
    QTimer* timer_ = nullptr;
    QList<QCPGraph*> graphs_;

    int inputCount_ = 2;
    double gain_ = 1.0;
    double timeWindowSec_ = 4.0;
    double timeSec_ = 0.0;
    bool compact_ = true;
    SignalMode signalMode_ = SignalMode::Flat;
    QVector<double> staticLevels_;
};

} // namespace s7ui::elements

