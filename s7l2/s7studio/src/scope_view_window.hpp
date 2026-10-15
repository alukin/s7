#pragma once

#include <QVector>
#include <QMap>

#include <cstdint>
#include <Qt>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QCustomPlot;

namespace s7studio::ui {

// A time-series view for one Studio Scope observer. Each connected observer
// input is a separate plotted channel; observation is outside the runtime graph.
class ScopeViewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeViewWindow(QString probeName,
                             QWidget* parent = nullptr,
                             Qt::WindowFlags flags = Qt::WindowFlags());

    [[nodiscard]] auto probeName() const -> QString;

    void appendSample(double timeSeconds, double value);
    void appendSample(const QString& channel, double timeSeconds, double value);
    void clearSamples();
    // Samples the runtime produced but the UI dropped because it fell behind.
    void setDroppedSampleCount(std::uint64_t dropped);

    // Recording keeps at most this many samples (16 bytes each), then stops by itself.
    static constexpr int kMaxRecordedSamples = 2'000'000;

private slots:
    void clearBuffers();
    void exportRecordedCsv();
    void toggleRecording();
    void refreshPlot();

private:
    struct Series {
        int graphIndex = -1;
        QVector<double> time;
        QVector<double> value;
        QVector<double> recordedTime;
        QVector<double> recordedValue;
    };

    void configurePlot();
    void setRecording(bool recording);
    void updateStatus();
    void trimVisibleBuffer(Series& series, double minTime);

    QString probeName_;
    bool recording_ = false;
    bool recordingLimitReached_ = false;
    std::uint64_t droppedSamples_ = 0;
    bool dirty_ = false;

    QMap<QString, Series> series_;

    QCustomPlot* plot_ = nullptr;
    QDoubleSpinBox* windowSecondsSpin_ = nullptr;
    QCheckBox* autoScaleYCheck_ = nullptr;
    QPushButton* recordButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui
