#pragma once

#include <QVector>

#include <cstdint>
#include <optional>
#include <Qt>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class QCustomPlot;

namespace s7studio::ui {

// Two probes plotted against each other (VISUAL_NUMERICAL_TESTS_PLAN.md
// section 3.2: position vs. velocity, an I/Q constellation, ...), the X-Y
// scope mode of an analog computer (section 1.0). Samples for the two probes
// arrive independently through appendXSample()/appendYSample(); a point is
// plotted only once both probes have a sample for the same step (they are
// always sampled together, once per runtime step, so this always catches up).
class PhasePortraitViewWindow : public QWidget
{
    Q_OBJECT

public:
    PhasePortraitViewWindow(QString xProbeName,
                            QString yProbeName,
                            QWidget* parent = nullptr,
                            Qt::WindowFlags flags = Qt::WindowFlags());

    [[nodiscard]] auto xProbeName() const -> QString { return xProbeName_; }
    [[nodiscard]] auto yProbeName() const -> QString { return yProbeName_; }

    void appendXSample(long step, double value);
    void appendYSample(long step, double value);
    void clearSamples();
    void setDroppedSampleCount(std::uint64_t dropped);

    // The trajectory keeps at most this many points, then drops the oldest.
    static constexpr int kMaxPoints = 2'000'000;

private slots:
    void clearBuffers();
    void exportRecordedCsv();
    void refreshPlot();

private:
    void configurePlot();
    void updateStatus();
    void emitPoint(double x, double y);

    QString xProbeName_;
    QString yProbeName_;
    std::uint64_t droppedSamples_ = 0;
    bool dirty_ = false;

    std::optional<long> pendingXStep_;
    double pendingXValue_ = 0.0;
    std::optional<long> pendingYStep_;
    double pendingYValue_ = 0.0;

    QVector<double> xValues_;
    QVector<double> yValues_;

    QCustomPlot* plot_ = nullptr;
    QCheckBox* autoScaleCheck_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui
