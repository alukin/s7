// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "viewers/line_plot_view.hpp"
#include "phase_trajectory_3d_view_window.hpp"
#include <QApplication>
#include <QLineSeries>
#include <QQmlContext>
#include <QQmlEngine>
#include <QtQuickTest>
#include <cmath>

class ViewerSetup : public QObject {
    Q_OBJECT
    s7studio::viewers::LinePlotView plot_;
    s7studio::ui::Trajectory3DBridge trajectory_{nullptr};
public:
    ViewerSetup() {
        plot_.setAxisTitles("Model time (s)", "Value");
        plot_.setXRange(0, 10); plot_.setYRange(-2, 2);
        auto* series = plot_.addSeries("reference", {QColor("#d02020"), 3, false});
        QList<QPointF> points;
        QList<s7studio::viewers::TrajectoryPoint> trajectory;
        for (int i = 0; i <= 200; ++i) {
            const double t = i * .05;
            points.append({t, std::sin(t)});
            trajectory.append({std::cos(t), std::sin(t), t});
        }
        s7studio::viewers::LinePlotView::setPoints(series, points);
        const auto bounds = s7studio::viewers::TrajectoryBounds::of(trajectory);
        trajectory_.trajectoryGeometry().setTrajectory(trajectory, bounds, 1000);
        trajectory_.setHead(true, s7studio::viewers::toScene(trajectory.last(), bounds));
        trajectory_.setViewRotation({-25, 30, 0});
    }
    Q_INVOKABLE void resetPlot() { plot_.setXRange(0, 10); plot_.setYRange(-2, 2); }
    Q_INVOKABLE void setLegend(bool visible) { plot_.setLegendVisible(visible); }
    Q_INVOKABLE void rotateTrajectory() { trajectory_.setViewRotation({0, 0, 0}); }
public slots:
    void qmlEngineAvailable(QQmlEngine* engine) {
        engine->rootContext()->setContextProperty("plotFixture", plot_.bridge());
        engine->rootContext()->setContextProperty("trajectoryFixture", &trajectory_);
        engine->rootContext()->setContextProperty("fixture", this);
        engine->rootContext()->setContextProperty("artifactDirectory", qEnvironmentVariable("S7_GUI_ARTIFACT_DIR"));
    }
};
int main(int argc, char** argv) {
    QApplication app(argc, argv);
    ViewerSetup setup;
    return quick_test_main_with_setup(argc, argv, "studio_viewers", S7_GUI_QML_TEST_DIR, &setup);
}
#include "quick_test.moc"
