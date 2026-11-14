// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "surface_plot.hpp"
#include "analysis/display_worker.hpp"
#include "quick_support.hpp"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWidget>
#include <QThread>
#include <QTimer>
#include <QVariantMap>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <numbers>

namespace s7studio::viewers {
namespace {
class Heatmap final : public QWidget {
  public:
    explicit Heatmap(QWidget *parent) : QWidget(parent) {
        setObjectName("surface.heatmap");
        setMouseTracking(true);
        setMinimumSize(180, 150);
    }
    QImage image;
    TrajectoryBounds bounds;
    bool logarithmicY = false;
    QStringList axes;
    std::shared_ptr<const analysis::SurfaceGrid> grid;
    std::optional<std::pair<int, int>> selected;
    std::function<void(int, int)> select;
    QRectF area() const { return QRectF(rect()).adjusted(62, 16, -18, -52); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        const auto a = area();
        // Area filtering on reduction keeps narrow events visible. Nearest
        // sampling can skip an entire bright impulse column in a small dock.
        const auto pixels = (a.size() * devicePixelRatioF()).toSize();
        if (!image.isNull() && pixels.width() > 0 && pixels.height() > 0 &&
            (pixels.width() < image.width() || pixels.height() < image.height()))
            p.drawImage(a, image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        else
            p.drawImage(a, image);
        if (selected && grid && std::size_t(selected->first) < grid->rows() &&
            std::size_t(selected->second) < grid->columns()) {
            const auto x = grid->x[std::size_t(selected->second)], y = grid->y[std::size_t(selected->first)];
            if (x >= bounds.min[0] && x <= bounds.max[0] && y >= bounds.min[1] && y <= bounds.max[1]) {
                const QPointF point(a.left() + a.width() * (x - bounds.min[0]) / (bounds.max[0] - bounds.min[0]),
                                    a.bottom() - a.height() * (y - bounds.min[1]) / (bounds.max[1] - bounds.min[1]));
                p.save();
                p.setClipRect(a);
                p.setPen(QPen(QColor(30, 30, 30, 150), 1, Qt::DashLine));
                p.drawLine(QPointF(a.left(), point.y()), QPointF(a.right(), point.y()));
                p.drawLine(QPointF(point.x(), a.top()), QPointF(point.x(), a.bottom()));
                p.setPen(QPen(QColor("#d62728"), 2));
                p.drawEllipse(point, 4, 4);
                p.restore();
            }
        }
        p.setPen(QColor("#555555"));
        p.drawRect(a);
        if (grid && !grid->x.empty()) {
            p.drawText(QRectF(a.left(), a.bottom() + 4, 120, 20), QString::number(bounds.min[0], 'g', 5));
            p.drawText(QRectF(a.right() - 120, a.bottom() + 4, 120, 20), Qt::AlignRight,
                       QString::number(bounds.max[0], 'g', 5));
            p.drawText(QRectF(0, a.top(), 58, 20), Qt::AlignRight,
                       QString::number(logarithmicY ? std::pow(10.0, bounds.max[1]) : bounds.max[1], 'g', 5));
            p.drawText(QRectF(0, a.bottom() - 20, 58, 20), Qt::AlignRight,
                       QString::number(logarithmicY ? std::pow(10.0, bounds.min[1]) : bounds.min[1], 'g', 5));
        }
        p.drawText(QRectF(a.left(), height() - 25, a.width(), 20), Qt::AlignCenter, axes.value(0));
        p.save();
        p.translate(15, height() / 2);
        p.rotate(-90);
        p.drawText(QRectF(-height() / 2, 0, height(), 20), Qt::AlignCenter, axes.value(1));
        p.restore();
    }
    void mouseMoveEvent(QMouseEvent *e) override {
        if (!grid || grid->x.empty() || grid->y.empty())
            return;
        const auto a = area();
        if (a.contains(e->position()) && a.width() > 0 && a.height() > 0 && select) {
            auto nearest = [](const std::vector<double> &values, double t) {
                auto it = std::lower_bound(values.begin(), values.end(), t);
                auto i = std::size_t(it - values.begin());
                if (i == values.size())
                    --i;
                if (i && t - values[i - 1] < values[i] - t)
                    --i;
                return int(i);
            };
            const auto c = nearest(grid->x, bounds.min[0] + (bounds.max[0] - bounds.min[0]) *
                                                                (e->position().x() - a.left()) / a.width());
            const auto r =
                nearest(grid->y, bounds.min[1] + (bounds.max[1] - bounds.min[1]) *
                                                     (a.bottom() - e->position().y()) / a.height());
            select(r, c);
        }
    }
};
class Legend final : public QWidget {
  public:
    explicit Legend(QWidget *p) : QWidget(p) { setFixedWidth(130); }
    bool complex = false;
    QString title = "Height";
    double low = -1, high = 1;
    SurfaceHeight mode = SurfaceHeight::Magnitude;
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        const int top = 60, span = std::max(1, height() - 130);
        for (int i = 0; i < span; ++i) {
            const double t = 1 - double(i) / double(span);
            const double h = low + t * (high - low);
            const auto value =
                complex ? std::polar(1.0, (t * 2 - 1) * std::numbers::pi) : std::complex<double>(h, 0);
            p.setPen(SurfaceGeometry::color(value, complex, low, high, complex ? mode : SurfaceHeight::Real));
            p.drawLine(8, top + i, 28, top + i);
        }
        p.setPen(Qt::black);
        p.drawText(QRect(4, 2, width() - 8, 50), Qt::AlignLeft | Qt::TextWordWrap, title);
        p.drawText(33, top + 10, complex ? "+π" : QString::number(high, 'g', 5));
        p.drawText(33, top + span / 2, complex ? "0" : QString::number(low / 2 + high / 2, 'g', 5));
        p.drawText(33, top + span, complex ? "−π" : QString::number(low, 'g', 5));
        if (complex)
            p.drawText(QRect(0, top + span + 12, 100, 48), Qt::TextWordWrap, "Gray: zero / undefined phase");
    }
};
TrajectoryBounds gridBounds(const analysis::SurfaceGrid &grid, const QList<TrajectoryPoint> &points) {
    auto bounds = TrajectoryBounds::of(points);
    for (std::size_t axis = 0; axis < 2; ++axis) {
        const auto &coordinates = axis == 0 ? grid.x : grid.y;
        double low = INFINITY, high = -INFINITY;
        for (auto value : coordinates)
            if (std::isfinite(value)) {
                low = std::min(low, value);
                high = std::max(high, value);
            }
        if (low < high) {
            bounds.min[axis] = low;
            bounds.max[axis] = high;
        } else if (low == high && std::isfinite(low)) {
            const auto half = std::max(1e-12, low == 0 ? .5 : std::abs(low) * .05);
            bounds.min[axis] = std::isfinite(low - half) ? low - half : low;
            bounds.max[axis] = std::isfinite(high + half) ? high + half : high;
        }
    }
    return bounds;
}
QVector3D position(double x, double y, double z, const TrajectoryBounds &b) {
    auto scale = [&](double v, std::size_t axis) {
        return float((2 * (static_cast<long double>(v) - b.min[axis]) /
                          (static_cast<long double>(b.max[axis]) - b.min[axis]) -
                      1) *
                     kTrajectoryHalfExtent);
    };
    return {scale(x, 0), scale(z, 2), -scale(y, 1)};
}
} // namespace

double SurfaceGeometry::height(std::complex<double> v, bool complex, SurfaceHeight mode) {
    if (mode == SurfaceHeight::LogMagnitude)
        return complex ? std::log1p(std::abs(v)) : (v.real() > 0 ? std::log10(v.real()) : NAN);
    if (!complex || mode == SurfaceHeight::Real)
        return v.real();
    if (mode == SurfaceHeight::Imaginary)
        return v.imag();
    if (mode == SurfaceHeight::LogMagnitude)
        return std::log1p(std::abs(v));
    return std::abs(v);
}
QColor SurfaceGeometry::color(std::complex<double> v, bool complex, double low, double high,
                              SurfaceHeight mode, bool cyclicColors) {
    if (cyclicColors && !complex) {
        v = {std::cos(v.real()), std::sin(v.real())};
        complex = true;
    }
    if (complex) {
        if (v == std::complex<double>{})
            return QColor("#888888");
        double hue = (std::arg(v) + std::numbers::pi) / (2 * std::numbers::pi);
        return QColor::fromHsvF(hue == 1 ? 0 : hue, .85, .9);
    }
    const double h = height(v, false, mode);
    double t = high > low
                   ? double((static_cast<long double>(h) - low) / (static_cast<long double>(high) - low))
                   : .5;
    if (low < 0 && high > 0)
        t = h < 0 ? .5 * (1 - h / low) : .5 + .5 * h / high;
    t = std::clamp(t, 0.0, 1.0);
    return t < .5 ? QColor::fromRgbF(2 * t, 2 * t, 1) : QColor::fromRgbF(1, 2 * (1 - t), 2 * (1 - t));
}
SurfaceMesh SurfaceGeometry::prepare(const analysis::SurfaceGrid &g, bool complex, SurfaceHeight mode,
                                     bool wire, std::optional<TrajectoryBounds> fixed, bool cyclicColors,
                                     const std::function<bool()> &canceled) {
    if (!g.values.empty() &&
        (g.columns() == 0 || g.rows() == 0 || g.columns() > 513 || g.rows() > 513 ||
         g.values.size() != g.columns() * g.rows() || g.present.size() != g.values.size()))
        throw std::invalid_argument("Invalid surface render shape/budget");
    SurfaceMesh mesh;
    mesh.wireframe = wire;
    QList<TrajectoryPoint> points;
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        if ((i % 513) == 0 && canceled && canceled())
            return {};
        if (g.finite(i)) {
            const double h = height(g.values[i], complex, mode);
            if (std::isfinite(h))
                points.push_back({g.x[i % g.columns()], g.y[i / g.columns()], h});
        }
    }
    mesh.bounds = fixed.value_or(gridBounds(g, points));
    if (points.empty()) {
        return mesh;
    }
    QByteArray vertices(qsizetype(g.values.size() * 7 * sizeof(float)), Qt::Uninitialized);
    auto *out = reinterpret_cast<float *>(vertices.data());
    std::vector<bool> valid(g.values.size(), false);
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        if ((i % 513) == 0 && canceled && canceled())
            return {};
        const double h = height(g.values[i], complex, mode);
        valid[i] =
            g.finite(i) && std::isfinite(h) &&
            (!fixed ||
             (h >= mesh.bounds.min[2] && h <= mesh.bounds.max[2] &&
              g.x[i % g.columns()] >= mesh.bounds.min[0] && g.x[i % g.columns()] <= mesh.bounds.max[0] &&
              g.y[i / g.columns()] >= mesh.bounds.min[1] && g.y[i / g.columns()] <= mesh.bounds.max[1]));
        const auto p =
            valid[i] ? position(g.x[i % g.columns()], g.y[i / g.columns()], h, mesh.bounds) : QVector3D{};
        const auto c =
            valid[i] ? color(g.values[i], complex, mesh.bounds.min[2], mesh.bounds.max[2], mode, cyclicColors)
                     : QColor(Qt::gray);
        *out++ = p.x();
        *out++ = p.y();
        *out++ = p.z();
        *out++ = float(c.redF());
        *out++ = float(c.greenF());
        *out++ = float(c.blueF());
        *out++ = 1;
    }
    std::vector<quint32> indices;
    indices.reserve((g.columns() - 1) * (g.rows() - 1) * (wire ? 12 : 6));
    auto triangle = [&](std::size_t a, std::size_t b, std::size_t c) {
        if (!valid[a] || !valid[b] || !valid[c])
            return;
        if (cyclicColors && (std::abs(g.values[a].real() - g.values[b].real()) > std::numbers::pi ||
                             std::abs(g.values[b].real() - g.values[c].real()) > std::numbers::pi ||
                             std::abs(g.values[c].real() - g.values[a].real()) > std::numbers::pi))
            return;
        ++mesh.triangles;
        if (wire)
            indices.insert(indices.end(),
                           {quint32(a), quint32(b), quint32(b), quint32(c), quint32(c), quint32(a)});
        else
            indices.insert(indices.end(), {quint32(a), quint32(b), quint32(c)});
    };
    for (std::size_t r = 0; r + 1 < g.rows(); ++r) {
        if (canceled && canceled())
            return {};
        for (std::size_t c = 0; c + 1 < g.columns(); ++c) {
            const auto a = r * g.columns() + c, b = a + 1, d = a + g.columns(), e = d + 1;
            triangle(a, d, b);
            triangle(b, d, e);
        }
    }
    mesh.vertices = std::move(vertices);
    mesh.indices = QByteArray(reinterpret_cast<const char *>(indices.data()),
                              qsizetype(indices.size() * sizeof(quint32)));
    return mesh;
}
void SurfaceGeometry::apply(const SurfaceMesh &mesh) {
    Q_ASSERT(QThread::currentThread() == thread());
    clear();
    bounds_ = mesh.bounds;
    triangles_ = mesh.triangles;
    setStride(7 * sizeof(float));
    setPrimitiveType(mesh.wireframe ? PrimitiveType::Lines : PrimitiveType::Triangles);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, 3 * sizeof(float), Attribute::F32Type);
    addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);
    setVertexData(mesh.vertices);
    setIndexData(mesh.indices);
    setBounds({-100, -100, -100}, {100, 100, 100});
    update();
}
void SurfaceGeometry::setGrid(const analysis::SurfaceGrid &g, bool complex, SurfaceHeight mode, bool wire,
                              std::optional<TrajectoryBounds> fixed, bool cyclicColors) {
    apply(prepare(g, complex, mode, wire, fixed, cyclicColors));
}
struct SurfaceRequest {
    std::shared_ptr<const analysis::SurfaceGrid> grid;
    bool complex, compact, cyclicColors, contours, logY, equalAspect;
    std::optional<TrajectoryBounds> fixed;
    std::optional<std::size_t> sliceColumn;
    SurfaceHeight height;
    int presentation;
    QStringList axes;
};
struct SurfaceFrame {
    SurfaceRequest request{};
    std::shared_ptr<const analysis::SurfaceGrid> display;
    SurfaceMesh mesh;
    TrajectoryData slice;
    QVariantList labels;
    QImage image;
    QString title;
};
struct SurfacePlotView::Work {
    analysis::DisplayWorker<SurfaceFrame> worker;
    std::shared_ptr<const SurfaceFrame> displayed;
    std::optional<std::pair<int, int>> selection;
    bool scheduled = false, dirty = false, suspended = false, awaiting = false;
};
static SurfaceFrame prepareSurface(const SurfaceRequest &request, const std::function<bool()> &canceled) {
    SurfaceFrame frame;
    frame.request = request;
    const analysis::SurfaceGrid empty;
    auto display = request.grid ? std::make_shared<analysis::SurfaceGrid>(*request.grid)
                                : std::make_shared<analysis::SurfaceGrid>(empty);
    if (request.logY)
        for (auto &value : display->y)
            value = value > 0 ? std::log10(value) : std::numeric_limits<double>::quiet_NaN();
    const auto &g = *display;
    auto bounds = request.fixed;
    if (request.equalAspect && !bounds) {
        QList<TrajectoryPoint> points;
        for (std::size_t i = 0; i < g.values.size(); ++i)
            if (g.finite(i))
                points.append({g.x[i % g.columns()], g.y[i / g.columns()],
                               SurfaceGeometry::height(g.values[i], request.complex, request.height)});
        auto b = gridBounds(g, points);
        double span = 0;
        for (std::size_t a = 0; a < 3; ++a)
            span = std::max(span, b.max[a] - b.min[a]);
        for (std::size_t a = 0; a < 3; ++a) {
            const auto middle = b.min[a] / 2 + b.max[a] / 2;
            b.min[a] = middle - span / 2;
            b.max[a] = middle + span / 2;
        }
        bounds = b;
    }
    if (canceled())
        return {};
    frame.mesh = SurfaceGeometry::prepare(g, request.complex, request.height, request.presentation == 1,
                                          bounds, request.cyclicColors, canceled);
    const auto &b = frame.mesh.bounds;
    QList<TrajectoryPoint> slice;
    if (request.sliceColumn && *request.sliceColumn < g.columns())
        for (std::size_t r = 0; r < g.rows(); ++r) {
            const auto i = r * g.columns() + *request.sliceColumn;
            if (g.finite(i))
                slice.append({g.x[*request.sliceColumn], g.y[r],
                              SurfaceGeometry::height(g.values[i], request.complex, request.height)});
        }
    frame.slice = prepareTrajectory(slice, b, 513);
    QVariantList labels;
    auto label = [&](QVector3D p, QString s, bool title) {
        labels.push_back(QVariantMap{{"position", p}, {"text", s}, {"title", title}});
    };
    for (double t : {.1, .5, .9}) {
        if (request.compact && t != .5)
            continue;
        const auto q = float(-100 + 200 * t);
        for (std::size_t a = 0; a < 3; ++a) {
            const auto value =
                double((1 - static_cast<long double>(t)) * b.min[a] + static_cast<long double>(t) * b.max[a]);
            label(a == 0   ? QVector3D(q, -112, 112)
                  : a == 1 ? QVector3D(112, -112, -q)
                           : QVector3D(-112, q, 112),
                  QString::number(request.logY && a == 1 ? std::pow(10.0, value) : value, 'g', 4), false);
        }
    }
    label({0, -135, 135}, request.compact ? "X" : request.axes.value(0), true);
    label({135, -135, 0}, request.compact ? "Y" : request.axes.value(1), true);
    const auto title = !request.complex ? (request.height == SurfaceHeight::LogMagnitude
                                               ? "log10(" + request.axes.value(2) + ")"
                                               : request.axes.value(2))
                       : request.height == SurfaceHeight::Real         ? "Re(w)"
                       : request.height == SurfaceHeight::Imaginary    ? "Im(w)"
                       : request.height == SurfaceHeight::LogMagnitude ? "log(1 + |w|)"
                                                                       : "|w|";
    label({-135, 112, 112}, request.compact ? "Z" : title, true);
    frame.labels = std::move(labels);
    frame.display = display;
    frame.image = QImage(513, 513, QImage::Format_ARGB32);
    frame.image.fill(QColor("#dddddd"));
    if (!g.x.empty() && !g.y.empty() && std::isfinite(g.x.front()) && std::isfinite(g.x.back()) &&
        std::isfinite(g.y.front()) && std::isfinite(g.y.back())) {
        auto nearest = [](const std::vector<double> &axis, double value) {
            auto it = std::lower_bound(axis.begin(), axis.end(), value);
            auto i = std::size_t(it - axis.begin());
            if (i == axis.size())
                --i;
            if (i && value - axis[i - 1] < axis[i] - value)
                --i;
            return i;
        };
        for (int py = 0; py < 513; ++py) {
            if (canceled())
                return {};
            for (int px = 0; px < 513; ++px) {
                const auto x = b.min[0] + (b.max[0] - b.min[0]) * px / 512,
                           y = b.max[1] - (b.max[1] - b.min[1]) * py / 512;
                if ((g.columns() > 1 && (x < g.x.front() || x > g.x.back())) ||
                    (g.rows() > 1 && (y < g.y.front() || y > g.y.back())))
                    continue;
                const auto c = nearest(g.x, x), r = nearest(g.y, y);
                const auto i = r * g.columns() + c;
                if (g.finite(i) &&
                    std::isfinite(SurfaceGeometry::height(g.values[i], request.complex, request.height)) &&
                    (!request.fixed ||
                     (SurfaceGeometry::height(g.values[i], request.complex, request.height) >= b.min[2] &&
                      SurfaceGeometry::height(g.values[i], request.complex, request.height) <= b.max[2])))
                    frame.image.setPixelColor(px, py,
                                              SurfaceGeometry::color(g.values[i], request.complex, b.min[2],
                                                                     b.max[2], request.height,
                                                                     request.cyclicColors));
            }
        }
        if (request.contours) {
            QPainter painter(&frame.image);
            painter.setPen(QPen(QColor(30, 30, 30, 160), .8));
            for (int level = 1; level < 9; ++level) {
                if (canceled())
                    return {};
                const double h = b.min[2] + (b.max[2] - b.min[2]) * level / 9;
                for (std::size_t r = 0; r + 1 < g.rows(); ++r) {
                    if (canceled())
                        return {};
                    for (std::size_t c = 0; c + 1 < g.columns(); ++c) {
                        std::array<std::size_t, 4> ids{r * g.columns() + c, r * g.columns() + c + 1,
                                                       (r + 1) * g.columns() + c + 1,
                                                       (r + 1) * g.columns() + c};
                        QList<QPointF> crossings;
                        bool valid = true;
                        for (auto i : ids)
                            valid &= g.finite(i) && std::isfinite(SurfaceGeometry::height(
                                                        g.values[i], request.complex, request.height));
                        if (!valid)
                            continue;
                        for (std::size_t edge = 0; edge < 4; ++edge) {
                            const auto i = ids[edge], j = ids[(edge + 1) % 4];
                            const auto a = SurfaceGeometry::height(g.values[i], request.complex,
                                                                   request.height),
                                       z = SurfaceGeometry::height(g.values[j], request.complex,
                                                                   request.height);
                            if ((a < h) == (z < h))
                                continue;
                            const auto t = (h - a) / (z - a);
                            const auto x = (1 - t) * g.x[i % g.columns()] + t * g.x[j % g.columns()],
                                       y = (1 - t) * g.y[i / g.columns()] + t * g.y[j / g.columns()];
                            crossings.append({512 * (x - b.min[0]) / (b.max[0] - b.min[0]),
                                              512 * (b.max[1] - y) / (b.max[1] - b.min[1])});
                        }
                        for (qsizetype i = 0; i + 1 < crossings.size(); i += 2)
                            painter.drawLine(crossings[i], crossings[i + 1]);
                    }
                }
            }
        }
    }
    frame.title = title;
    return frame;
}
SurfacePlotView::SurfacePlotView(bool complex, QWidget *parent)
    : QWidget(parent), work_(std::make_unique<Work>()), complex_(complex), bridge_(new SurfaceBridge(this)),
      heatmap_(new Heatmap(this)) {
    setObjectName("viewer.surfacePresentation");
    setMinimumSize(180, 150);
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(heatmap_, 1);
    heatmap_->hide();
    auto *legend = new Legend(this);
    legend->setObjectName("surface.legend");
    root->addWidget(legend);
    static_cast<Heatmap *>(heatmap_)->select = [this](int r, int c) {
        selectCell(r, c);
        emit cellPicked(r, c);
    };
    bridge_->setViewRotation({-30, 35, 0});
    auto *timer = new QTimer(this);
    timer->setInterval(25);
    connect(timer, &QTimer::timeout, this, &SurfacePlotView::applyPrepared);
    timer->start();
}
SurfacePlotView::~SurfacePlotView() {
    work_->worker.cancel();
    unloadQuickScene(quick_);
}
void SurfacePlotView::showEvent(QShowEvent *event) {
    work_->suspended = false;
    submitPreparation();
    const QPointer<SurfacePlotView> alive(this);
    if (!quick_) {
        auto *created = createQuickWidget();
        if (!alive) {
            unloadQuickScene(created);
            return;
        }
        quick_ = created;
        setMinimumFrameInterval(*quick_, 100);
        static_cast<QHBoxLayout *>(layout())->insertWidget(0, quick_, 1);
        if (!alive)
            return;
        loadQuickScene(*quick_, "Surface3D.qml", {{"surfaceView", QVariant::fromValue<QObject *>(bridge_)}});
        quick_->setVisible(presentation_ != 2);
    }
    if (alive)
        QWidget::showEvent(event);
}
void SurfacePlotView::setSliceColumn(std::optional<std::size_t> column) {
    sliceColumn_ = column;
    rebuild();
}
void SurfacePlotView::setCyclicColors(bool enabled) {
    cyclicColors_ = enabled;
    rebuild();
}
void SurfacePlotView::setContours(bool enabled) {
    contours_ = enabled;
    rebuild();
}
void SurfacePlotView::setLogY(bool enabled) {
    logY_ = enabled;
    rebuild();
}
void SurfacePlotView::setEqualAspect(bool enabled) {
    equalAspect_ = enabled;
    rebuild();
}
void SurfacePlotView::setCompact(bool enabled) {
    compact_ = enabled;
    layout()->itemAt(layout()->count() - 1)->widget()->setVisible(!enabled);
    bridge_->setAntialiased(!enabled);
    rebuild();
}
void SurfacePlotView::setFixedBounds(std::optional<TrajectoryBounds> bounds) {
    if (bounds)
        for (std::size_t i = 0; i < 3; ++i)
            if (!std::isfinite(bounds->min[i]) || !std::isfinite(bounds->max[i]) ||
                !(bounds->min[i] < bounds->max[i]))
                throw std::invalid_argument("Fixed ranges must be finite and increasing");
    fixed_ = bounds;
    rebuild();
}
void SurfacePlotView::setGrid(std::shared_ptr<const analysis::SurfaceGrid> grid) {
    bridge_->setHead(false, {});
    static_cast<Heatmap *>(heatmap_)->selected.reset();
    work_->selection.reset();
    grid_ = std::move(grid);
    rebuild();
}
void SurfacePlotView::setAxes(QStringList axes) {
    axes_ = std::move(axes);
    rebuild();
}
void SurfacePlotView::setHeightMode(SurfaceHeight mode) {
    height_ = mode;
    rebuild();
}
void SurfacePlotView::setPresentation(int mode) {
    presentation_ = mode;
    if (quick_)
        quick_->setVisible(mode != 2);
    heatmap_->setVisible(mode == 2);
    rebuild();
}
void SurfacePlotView::rebuild() {
    work_->worker.cancel();
    work_->awaiting = false;
    work_->dirty = true;
    if (!grid_ || grid_->values.empty()) {
        work_->dirty = false;
        work_->displayed.reset();
        bridge_->geometry.apply({});
        bridge_->trajectoryGeometry().apply({});
        auto *heat = static_cast<Heatmap *>(heatmap_);
        heat->grid.reset();
        heat->image = {};
        heat->selected.reset();
        heat->update();
        return;
    }
    if (!work_->scheduled) {
        work_->scheduled = true;
        QTimer::singleShot(0, this, [this] {
            work_->scheduled = false;
            submitPreparation();
        });
    }
}
void SurfacePlotView::submitPreparation() {
    if (!work_->dirty || work_->suspended)
        return;
    work_->dirty = false;
    work_->awaiting = true;
    const SurfaceRequest request{grid_,        complex_, compact_,     cyclicColors_, contours_,     logY_,
                                 equalAspect_, fixed_,   sliceColumn_, height_,       presentation_, axes_};
    work_->worker.submit([request](const auto &canceled) { return prepareSurface(request, canceled); });
}
bool SurfacePlotView::isPreparing() const { return work_->dirty || work_->scheduled || work_->awaiting; }
void SurfacePlotView::applyPrepared() {
    auto ready = work_->worker.take();
    if (!ready)
        return;
    work_->awaiting = false;
    if (!ready->error.isEmpty()) {
        emit inspected("Display unavailable: " + ready->error);
        return;
    }
    work_->displayed = std::move(ready->value);
    const auto &frame = *work_->displayed;
    bridge_->geometry.apply(frame.mesh);
    bridge_->trajectoryGeometry().apply(frame.slice);
    bridge_->setLabels(frame.labels);
    auto *heat = static_cast<Heatmap *>(heatmap_);
    heat->bounds = frame.mesh.bounds;
    heat->grid = frame.display;
    heat->axes = frame.request.axes;
    heat->logarithmicY = frame.request.logY;
    heat->image = frame.image;
    heat->selected.reset();
    heat->update();
    auto *bar = static_cast<Legend *>(layout()->itemAt(layout()->count() - 1)->widget());
    bar->complex = frame.request.complex || frame.request.cyclicColors;
    bar->title = bar->complex ? QString("Phase (rad)") : frame.title;
    bar->low = frame.mesh.bounds.min[2];
    bar->high = frame.mesh.bounds.max[2];
    bar->mode = frame.request.height;
    bar->update();
    if (work_->selection)
        selectCell(work_->selection->first, work_->selection->second);
    emit presentationReady();
}
void SurfacePlotView::hideEvent(QHideEvent *event) {
    const bool pending = isPreparing();
    work_->suspended = true;
    work_->worker.cancel();
    work_->awaiting = false;
    work_->dirty = pending && grid_ && !grid_->values.empty();
    QWidget::hideEvent(event);
}
void SurfacePlotView::selectCell(int row, int column) {
    work_->selection = std::pair(row, column);
    if (!work_->displayed)
        return;
    const auto &request = work_->displayed->request;
    const auto &grid = request.grid;
    if (!grid || row < 0 || column < 0 || std::size_t(row) >= grid->rows() ||
        std::size_t(column) >= grid->columns())
        return;
    const auto i = std::size_t(row) * grid->columns() + std::size_t(column);
    if (!grid->finite(i)) {
        static_cast<Heatmap *>(heatmap_)->selected.reset();
        heatmap_->update();
        bridge_->setHead(false, {});
        emit inspected("Missing or nonfinite cell");
        return;
    }
    const auto w = grid->values[i];
    static_cast<Heatmap *>(heatmap_)->selected = std::pair(row, column);
    heatmap_->update();
    const auto h = SurfaceGeometry::height(w, request.complex, request.height);
    if (std::isfinite(h) && (!request.logY || grid->y[std::size_t(row)] > 0))
        bridge_->setHead(
            true, position(grid->x[std::size_t(column)],
                           (request.logY ? std::log10(grid->y[std::size_t(row)]) : grid->y[std::size_t(row)]),
                           h, bridge_->geometry.bounds()));
    else
        bridge_->setHead(false, {});
    QString text = QString("row %1, column %2 | x=%3, y=%4 | ")
                       .arg(row)
                       .arg(column)
                       .arg(grid->x[std::size_t(column)], 0, 'g', 12)
                       .arg(grid->y[std::size_t(row)], 0, 'g', 12);
    text += request.complex
                ? QString("w=(%1, %2), |w|=%3, phase=%4")
                      .arg(w.real(), 0, 'g', 12)
                      .arg(w.imag(), 0, 'g', 12)
                      .arg(std::abs(w), 0, 'g', 12)
                      .arg(w == std::complex<double>{} ? "undefined" : QString::number(std::arg(w), 'g', 12))
                : QString::number(w.real(), 'g', 17);
    emit inspected(text);
}
} // namespace s7studio::viewers
