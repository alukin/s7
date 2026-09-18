#pragma once

#include <QVector>
#include <Qt>
#include <QWidget>

class QCheckBox;
class QDoubleSpinBox;
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
    void trimVisibleBuffer(ChannelSeries& channel, double minTime);

    QString scopeId_;
    int channelCount_ = 1;
    bool recording_ = false;
    bool dirty_ = false;

    QVector<ChannelSeries> channels_;

    QCustomPlot* plot_ = nullptr;
    QDoubleSpinBox* windowSecondsSpin_ = nullptr;
    QCheckBox* autoScaleYCheck_ = nullptr;
    QPushButton* recordButton_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* configureButton_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui

