#pragma once

#include <QVector>
#include <QMap>
#include <QSharedPointer>

#include <cstdint>
#include <Qt>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class QVBoxLayout;
class QCustomPlot;
class QTemporaryFile;
class QScrollBar;

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

    static constexpr int kMaxInMemorySamplesPerSeries = 20'000;

private slots:
    void clearBuffers();
    void exportRecordedCsv();
    void refreshPlot();

private:
    struct Series {
        struct SpillIndexEntry {
            double timeSeconds = 0.0;
            qint64 byteOffset = 0;
        };
        int graphIndex = -1;
        QVector<double> time;
        QVector<double> value;
        QVector<SpillIndexEntry> spillIndex;
        QSharedPointer<QTemporaryFile> spillFile;
        qint64 spilledSamples = 0;
        double firstTime = 0.0;
        double lastTime = 0.0;
        bool hasSamples = false;
    };

    void configurePlot();
    auto spillSeries(Series& series) -> bool;
    auto appendToSpill(Series& series, double timeSeconds, double value) -> bool;
    void decimatePlotBuffer(Series& series);
    void setVisibleGraphData(Series& series, double lowerTime, double upperTime, bool fitFullRun);
    void updateStatus();
    void updateTimeScrollBar();
    void scrollToTimePosition(int position);
    QString probeName_;
    std::uint64_t droppedSamples_ = 0;
    QString temporaryDirectory_;
    QString temporaryWriteError_;
    bool dirty_ = false;
    bool updatingTimeScrollBar_ = false;
    bool hasTimeRange_ = false;
    double dataMinTime_ = 0.0;
    double dataMaxTime_ = 0.0;

    QMap<QString, Series> series_;

    QCustomPlot* plot_ = nullptr;
    QCheckBox* autoScaleYCheck_ = nullptr;
    QCheckBox* fitAllTimeCheck_ = nullptr;
    QScrollBar* timeScrollBar_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
};

} // namespace s7studio::ui
