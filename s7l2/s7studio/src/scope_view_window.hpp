#pragma once

#include <QVector>

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

class ScopeViewWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ScopeViewWindow(QString scopeId,
                             int channelCount,
                             QWidget* parent = nullptr,
                             Qt::WindowFlags flags = Qt::WindowFlags());

    [[nodiscard]] auto scopeId() const -> QString;
    [[nodiscard]] auto channelCount() const -> int;
    void setChannelCount(int channelCount);

    void appendSample(int channel, double timeSeconds, double value);
    void clearSamples();
    // Samples the runtime produced but the UI dropped because it fell behind.
    void setDroppedSampleCount(std::uint64_t dropped);

    // Recording keeps at most this many samples per channel (16 bytes each),
    // then stops by itself.
    static constexpr int kMaxRecordedSamplesPerChannel = 2'000'000;

signals:
    void configureRequested(const QString& scopeId, int channelCount);

private slots:
    void clearBuffers();
    void exportRecordedCsv();
    void toggleRecording();
    void refreshPlot();

private:
    struct ChannelSeries {
        QVector<double> time;
        QVector<double> value;
        QVector<double> recordedTime;
        QVector<double> recordedValue;
    };

    void configurePlot();
    void setRecording(bool recording);
    void updateStatus();
    void trimVisibleBuffer(ChannelSeries& channel, double minTime);

    QString scopeId_;
    int channelCount_ = 1;
    bool recording_ = false;
    bool recordingLimitReached_ = false;
    std::uint64_t droppedSamples_ = 0;
    bool dirty_ = false;

    QVector<ChannelSeries> channels_;

    QCustomPlot* plot_ = nullptr;
    QDoubleSpinBox* windowSecondsSpin_ = nullptr;
    QCheckBox* autoScaleYCheck_ = nullptr;
    QPushButton* recordButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* configureButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui

