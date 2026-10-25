// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "observation_viewer.hpp"
#include "trajectory_geometry.hpp"
#include <QWidget>
#include <functional>
#include <optional>
#include <utility>
class QScatterSeries;
namespace s7studio::ui { class TrajectoryPlotView; }
namespace s7studio::viewers {
class LinePlotView;
struct PreviewInput { QString name, unit, element; int port = 0; bool operator==(const PreviewInput&) const = default; };
struct ObserverDescriptor {
    QString id, type;
    QVector<PreviewInput> inputs;
    bool operator==(const ObserverDescriptor&) const = default;
};
struct PreviewData {
    struct Trace { QString name; QList<QList<QPointF>> segments; int channel = 0; };
    QVector<Trace> traces;
    QList<TrajectoryPoint> trajectory;
    QStringList axes, details;
    QString status, summary;
    QString timeAxis = "Model time (s; recent interval)";
    std::optional<std::pair<double, double>> timeRange;
    std::uint64_t revision = 0, run = 0;
    std::size_t rawPositions = 0, vertices = 0;
};
struct PreviewRequest {
    ObserverDescriptor descriptor;
    QVector<int> channels;
    std::shared_ptr<const observation::ObservationSource> source{};
    std::shared_ptr<const observation::LiveAcquisition> live{};
    observation::TriggerSettings trigger{};
    std::uint64_t revision = 0;
};
// Pure bounded preparation, shared by hosts; never touches a QWidget or runtime.
PreviewData preparePreview(const PreviewRequest& request);
class PreviewWorker {
public:
    PreviewWorker();
    ~PreviewWorker();
    void submit(PreviewRequest request);
    void cancel();
    std::optional<PreviewData> takeResult();
    bool busy() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};

// Reusable Time / XY / 3D presentation surfaces, with no acquisition settings.
// Full viewers use the same LinePlotView and TrajectoryPlotView components.
class ObserverPresentation : public QWidget {
    Q_OBJECT
public:
    explicit ObserverPresentation(const QString& type, QWidget* parent = nullptr);
    void present(const PreviewData& data);
    static QColor channelColor(const QString& type, int channel);
    LinePlotView* linePlot() const { return line_; }
signals:
    void navigated();
private:
    QString type_;
    LinePlotView* line_ = nullptr;
    ui::TrajectoryPlotView* trajectory_ = nullptr;
    QList<QScatterSeries*> isolated_;
};
}
