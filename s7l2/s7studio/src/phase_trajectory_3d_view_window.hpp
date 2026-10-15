#pragma once

#include <QVector>
#include <QWidget>

#include <cstdint>
#include <map>
#include <optional>

class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;
class QCustomPlot;
class QCPCurve;

namespace s7studio::ui {

// A 3D phase trajectory shown as three orthogonal QCustomPlot projections.
class PhaseTrajectory3DViewWindow : public QWidget
{
    Q_OBJECT

public:
    PhaseTrajectory3DViewWindow(QString xProbeName,
                                QString yProbeName,
                                QString zProbeName,
                                QWidget* parent = nullptr,
                                Qt::WindowFlags flags = Qt::WindowFlags());

    [[nodiscard]] auto xProbeName() const -> QString { return xProbeName_; }
    [[nodiscard]] auto yProbeName() const -> QString { return yProbeName_; }
    [[nodiscard]] auto zProbeName() const -> QString { return zProbeName_; }

    void appendXSample(long step, double value);
    void appendYSample(long step, double value);
    void appendZSample(long step, double value);
    void clearSamples();
    void setDroppedSampleCount(std::uint64_t dropped);

    static constexpr int kMaxPoints = 2'000'000;

private slots:
    void clearBuffers();
    void exportCsv();
    void refreshPlots();

private:
    struct Point3D {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
    };
    struct PendingPoint {
        Point3D point;
        bool hasX = false;
        bool hasY = false;
        bool hasZ = false;
    };

    void addCoordinate(long step, double value, int axis);
    void updateStatus();

    QString xProbeName_;
    QString yProbeName_;
    QString zProbeName_;
    std::map<long, PendingPoint> pending_;
    QVector<Point3D> points_;
    std::uint64_t droppedSamples_ = 0;
    std::uint64_t incompletePointsDropped_ = 0;
    bool dirty_ = false;

    QCheckBox* autoScaleCheck_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTimer* repaintTimer_ = nullptr;
    QCustomPlot* xyPlot_ = nullptr;
    QCustomPlot* xzPlot_ = nullptr;
    QCustomPlot* yzPlot_ = nullptr;
    QCPCurve* xyCurve_ = nullptr;
    QCPCurve* xzCurve_ = nullptr;
    QCPCurve* yzCurve_ = nullptr;
};

} // namespace s7studio::ui
