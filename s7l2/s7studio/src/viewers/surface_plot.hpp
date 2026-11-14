// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#pragma once
#include "analysis/surface_grid.hpp"
#include "viewers/trajectory_plot.hpp"
#include <QColor>
#include <QImage>
#include <QWidget>
#include <functional>
#include <memory>
#include <optional>

class QQuickWidget;
namespace s7studio::viewers {
enum class SurfaceHeight { Magnitude, Real, Imaginary, LogMagnitude };
struct SurfaceMesh {
    QByteArray vertices, indices;
    TrajectoryBounds bounds;
    std::size_t triangles = 0;
    bool wireframe = false;
};
class SurfaceGeometry final : public QQuick3DGeometry {
    Q_OBJECT
  public:
    explicit SurfaceGeometry(QQuick3DObject *parent = nullptr) : QQuick3DGeometry(parent) {}
    void setGrid(const analysis::SurfaceGrid &grid, bool complex, SurfaceHeight mode, bool wireframe,
                 std::optional<TrajectoryBounds> fixed = {}, bool cyclicColors = false);
    static SurfaceMesh prepare(const analysis::SurfaceGrid &grid, bool complex, SurfaceHeight mode,
                               bool wireframe, std::optional<TrajectoryBounds> fixed = {},
                               bool cyclicColors = false, const std::function<bool()> &canceled = {});
    void apply(const SurfaceMesh &mesh);
    std::size_t triangleCount() const { return triangles_; }
    const TrajectoryBounds &bounds() const { return bounds_; }
    static double height(std::complex<double> value, bool complex, SurfaceHeight mode);
    static QColor color(std::complex<double> value, bool complex, double low, double high, SurfaceHeight mode,
                        bool cyclicColors = false);

  private:
    std::size_t triangles_ = 0;
    TrajectoryBounds bounds_;
};
class SurfaceBridge final : public ui::Trajectory3DBridge {
    Q_OBJECT
    Q_PROPERTY(QObject *surface READ surface CONSTANT)
  public:
    explicit SurfaceBridge(QObject *parent) : Trajectory3DBridge(parent) {}
    QObject *surface() { return &geometry; }
    SurfaceGeometry geometry;
};
// Shared by the full observer window and bounded dock preview. No runtime or
// recording mutation, and no hidden full window created to obtain a preview.
class SurfacePlotView final : public QWidget {
    Q_OBJECT
  public:
    explicit SurfacePlotView(bool complex, QWidget *parent = nullptr);
    ~SurfacePlotView() override;
    void setGrid(std::shared_ptr<const analysis::SurfaceGrid> grid);
    void setHeightMode(SurfaceHeight mode);
    void setPresentation(int mode); // 0 surface, 1 wireframe, 2 heatmap
    void setAxes(QStringList axes);
    void setContours(bool enabled);
    void setCyclicColors(bool enabled);
    void setSliceColumn(std::optional<std::size_t> column);
    void setLogY(bool enabled);
    void setFixedBounds(std::optional<TrajectoryBounds> bounds);
    void setEqualAspect(bool enabled);
    void setCompact(bool enabled);
    void setViewRotation(QVector3D rotation) { bridge_->setViewRotation(rotation); }
    void selectCell(int row, int column);
    SurfaceBridge *bridge() const { return bridge_; }
    bool isPreparing() const;
  signals:
    void inspected(const QString &text);
    void cellPicked(int row, int column);
    void presentationReady();

  protected:
    void showEvent(QShowEvent *) override;
    void hideEvent(QHideEvent *) override;

  private:
    void rebuild();
    void submitPreparation();
    void applyPrepared();
    struct Work;
    std::unique_ptr<Work> work_;
    bool complex_;
    bool compact_ = false;
    bool cyclicColors_ = false;
    bool contours_ = false, logY_ = false, equalAspect_ = false;
    std::optional<TrajectoryBounds> fixed_;
    std::optional<std::size_t> sliceColumn_;
    SurfaceHeight height_ = SurfaceHeight::Magnitude;
    int presentation_ = 0;
    std::shared_ptr<const analysis::SurfaceGrid> grid_;
    SurfaceBridge *bridge_;
    QQuickWidget *quick_ = nullptr;
    QWidget *heatmap_;
    QStringList axes_{"x (unit unspecified)", "y (unit unspecified)", "height (unit unspecified)"};
};
} // namespace s7studio::viewers
