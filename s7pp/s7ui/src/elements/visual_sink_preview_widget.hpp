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
        Flat,       ///< No animation; displays static levels
        Synthetic,  ///< Animated demo waveform (for prototyping)
        Live        ///< Real data fed via updateChannelData()
    };

    explicit VisualSinkPreviewWidget(bool compact, QWidget* parent = nullptr);

    void setInputCount(int count);
    int inputCount() const;

    void setGain(double gain);
    void setTimeWindow(double seconds);
    void setSignalMode(SignalMode mode);
    void setStaticLevels(const QVector<double>& levels);

    /// Push a new batch of samples for 'channel'.  The widget keeps the last
    /// timeWindow / sample_dt samples and redraws.  Call from the GUI thread.
    void updateChannelData(int channel, const QVector<double>& samples);

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

    // Per-channel sample cache for Y-axis auto-ranging in Live mode.
    std::vector<std::vector<double>> liveSamples_;
};

} // namespace s7ui::elements

