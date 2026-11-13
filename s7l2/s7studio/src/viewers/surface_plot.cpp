// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "surface_plot.hpp"
#include "quick_support.hpp"
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QQuickWidget>
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
        setMouseTracking(true);
        setMinimumSize(180, 150);
    }
    QImage image;
    TrajectoryBounds bounds;
    bool logarithmicY = false;
    QStringList axes;
    std::shared_ptr<const analysis::SurfaceGrid> grid;
    std::function<void(int, int)> select;
    QRectF area() const { return QRectF(rect()).adjusted(62, 16, -18, -52); }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::white);
        const auto a = area();
        p.drawImage(a, image);
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
        const auto a = area();
        if (grid && a.contains(e->position()) && a.width() > 0 && a.height() > 0 && select) {
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
void SurfaceGeometry::setGrid(const analysis::SurfaceGrid &g, bool complex, SurfaceHeight mode, bool wire,
                              std::optional<TrajectoryBounds> fixed, bool cyclicColors) {
    if (!g.values.empty() &&
        (g.columns() == 0 || g.rows() == 0 || g.columns() > 513 || g.rows() > 513 ||
         g.values.size() != g.columns() * g.rows() || g.present.size() != g.values.size()))
        throw std::invalid_argument("Invalid surface render shape/budget");
    clear();
    triangles_ = 0;
    QList<TrajectoryPoint> points;
    for (std::size_t i = 0; i < g.values.size(); ++i)
        if (g.finite(i)) {
            const double h = height(g.values[i], complex, mode);
            if (std::isfinite(h))
                points.push_back({g.x[i % g.columns()], g.y[i / g.columns()], h});
        }
    bounds_ = fixed.value_or(gridBounds(g, points));
    if (points.empty()) {
        update();
        return;
    }
    QByteArray vertices(qsizetype(g.values.size() * 7 * sizeof(float)), Qt::Uninitialized);
    auto *out = reinterpret_cast<float *>(vertices.data());
    std::vector<bool> valid(g.values.size(), false);
    for (std::size_t i = 0; i < g.values.size(); ++i) {
        const double h = height(g.values[i], complex, mode);
        valid[i] =
            g.finite(i) && std::isfinite(h) &&
            (!fixed || (h >= bounds_.min[2] && h <= bounds_.max[2] &&
                        g.x[i % g.columns()] >= bounds_.min[0] && g.x[i % g.columns()] <= bounds_.max[0] &&
                        g.y[i / g.columns()] >= bounds_.min[1] && g.y[i / g.columns()] <= bounds_.max[1]));
        const auto p =
            valid[i] ? position(g.x[i % g.columns()], g.y[i / g.columns()], h, bounds_) : QVector3D{};
        const auto c = valid[i]
                           ? color(g.values[i], complex, bounds_.min[2], bounds_.max[2], mode, cyclicColors)
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
        ++triangles_;
        if (wire)
            indices.insert(indices.end(),
                           {quint32(a), quint32(b), quint32(b), quint32(c), quint32(c), quint32(a)});
        else
            indices.insert(indices.end(), {quint32(a), quint32(b), quint32(c)});
    };
    for (std::size_t r = 0; r + 1 < g.rows(); ++r)
        for (std::size_t c = 0; c + 1 < g.columns(); ++c) {
            const auto a = r * g.columns() + c, b = a + 1, d = a + g.columns(), e = d + 1;
            triangle(a, d, b);
            triangle(b, d, e);
        }
    setStride(7 * sizeof(float));
    setPrimitiveType(wire ? PrimitiveType::Lines : PrimitiveType::Triangles);
    addAttribute(Attribute::PositionSemantic, 0, Attribute::F32Type);
    addAttribute(Attribute::ColorSemantic, 3 * sizeof(float), Attribute::F32Type);
    addAttribute(Attribute::IndexSemantic, 0, Attribute::U32Type);
    setVertexData(vertices);
    setIndexData(QByteArray(reinterpret_cast<const char *>(indices.data()),
                            qsizetype(indices.size() * sizeof(quint32))));
    setBounds({-100, -100, -100}, {100, 100, 100});
    update();
}
SurfacePlotView::SurfacePlotView(bool complex, QWidget *parent)
    : QWidget(parent), complex_(complex), bridge_(new SurfaceBridge(this)), heatmap_(new Heatmap(this)) {
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
}
SurfacePlotView::~SurfacePlotView() { unloadQuickScene(quick_); }
void SurfacePlotView::showEvent(QShowEvent *event) {
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
    const analysis::SurfaceGrid empty;
    auto display = grid_ ? std::make_shared<analysis::SurfaceGrid>(*grid_)
                         : std::make_shared<analysis::SurfaceGrid>(empty);
    if (logY_)
        for (auto &value : display->y)
            value = value > 0 ? std::log10(value) : std::numeric_limits<double>::quiet_NaN();
    const auto &g = *display;
    auto bounds = fixed_;
    if (equalAspect_ && !bounds) {
        QList<TrajectoryPoint> points;
        for (std::size_t i = 0; i < g.values.size(); ++i)
            if (g.finite(i))
                points.append({g.x[i % g.columns()], g.y[i / g.columns()],
                               SurfaceGeometry::height(g.values[i], complex_, height_)});
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
    bridge_->geometry.setGrid(g, complex_, height_, presentation_ == 1, bounds, cyclicColors_);
    const auto &b = bridge_->geometry.bounds();
    QList<TrajectoryPoint> slice;
    if (sliceColumn_ && *sliceColumn_ < g.columns())
        for (std::size_t r = 0; r < g.rows(); ++r) {
            const auto i = r * g.columns() + *sliceColumn_;
            if (g.finite(i))
                slice.append(
                    {g.x[*sliceColumn_], g.y[r], SurfaceGeometry::height(g.values[i], complex_, height_)});
        }
    bridge_->trajectoryGeometry().setTrajectory(slice, b, 513);
    QVariantList labels;
    auto label = [&](QVector3D p, QString s, bool title) {
        labels.push_back(QVariantMap{{"position", p}, {"text", s}, {"title", title}});
    };
    for (double t : {.1, .5, .9}) {
        const auto q = float(-100 + 200 * t);
        for (std::size_t a = 0; a < 3; ++a) {
            const auto value =
                double((1 - static_cast<long double>(t)) * b.min[a] + static_cast<long double>(t) * b.max[a]);
            label(a == 0   ? QVector3D(q, -112, 112)
                  : a == 1 ? QVector3D(112, -112, -q)
                           : QVector3D(-112, q, 112),
                  QString::number(logY_ && a == 1 ? std::pow(10.0, value) : value, 'g', 4), false);
        }
    }
    label({0, -135, 135}, axes_.value(0), true);
    label({135, -135, 0}, axes_.value(1), true);
    const auto title = !complex_ ? (height_ == SurfaceHeight::LogMagnitude ? "log10(" + axes_.value(2) + ")"
                                                                           : axes_.value(2))
                       : height_ == SurfaceHeight::Real         ? "Re(w)"
                       : height_ == SurfaceHeight::Imaginary    ? "Im(w)"
                       : height_ == SurfaceHeight::LogMagnitude ? "log(1 + |w|)"
                                                                : "|w|";
    label({-135, 112, 112}, title, true);
    bridge_->setLabels(labels);
    auto *heat = static_cast<Heatmap *>(heatmap_);
    heat->bounds = b;
    heat->grid = display;
    heat->axes = axes_;
    heat->logarithmicY = logY_;
    heat->image = QImage(513, 513, QImage::Format_ARGB32);
    heat->image.fill(QColor("#dddddd"));
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
        for (int py = 0; py < 513; ++py)
            for (int px = 0; px < 513; ++px) {
                const auto x = b.min[0] + (b.max[0] - b.min[0]) * px / 512,
                           y = b.max[1] - (b.max[1] - b.min[1]) * py / 512;
                if (x < g.x.front() || x > g.x.back() || y < g.y.front() || y > g.y.back())
                    continue;
                const auto c = nearest(g.x, x), r = nearest(g.y, y);
                const auto i = r * g.columns() + c;
                if (g.finite(i) &&
                    (!fixed_ || (SurfaceGeometry::height(g.values[i], complex_, height_) >= b.min[2] &&
                                 SurfaceGeometry::height(g.values[i], complex_, height_) <= b.max[2])))
                    heat->image.setPixelColor(px, py,
                                              SurfaceGeometry::color(g.values[i], complex_, b.min[2],
                                                                     b.max[2], height_, cyclicColors_));
            }
        if (contours_) {
            QPainter painter(&heat->image);
            painter.setPen(QPen(QColor(30, 30, 30, 160), .8));
            for (int level = 1; level < 9; ++level) {
                const double h = b.min[2] + (b.max[2] - b.min[2]) * level / 9;
                for (std::size_t r = 0; r + 1 < g.rows(); ++r)
                    for (std::size_t c = 0; c + 1 < g.columns(); ++c) {
                        std::array<std::size_t, 4> ids{r * g.columns() + c, r * g.columns() + c + 1,
                                                       (r + 1) * g.columns() + c + 1,
                                                       (r + 1) * g.columns() + c};
                        QList<QPointF> crossings;
                        bool valid = true;
                        for (auto i : ids)
                            valid &= g.finite(i);
                        if (!valid)
                            continue;
                        for (std::size_t edge = 0; edge < 4; ++edge) {
                            const auto i = ids[edge], j = ids[(edge + 1) % 4];
                            const auto a = SurfaceGeometry::height(g.values[i], complex_, height_),
                                       z = SurfaceGeometry::height(g.values[j], complex_, height_);
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
    heat->update();
    auto *bar = static_cast<Legend *>(layout()->itemAt(layout()->count() - 1)->widget());
    bar->complex = complex_ || cyclicColors_;
    bar->title = bar->complex ? QString("Phase (rad)") : title;
    bar->low = b.min[2];
    bar->high = b.max[2];
    bar->mode = height_;
    bar->update();
}
void SurfacePlotView::selectCell(int row, int column) {
    if (!grid_ || row < 0 || column < 0 || std::size_t(row) >= grid_->rows() ||
        std::size_t(column) >= grid_->columns())
        return;
    const auto i = std::size_t(row) * grid_->columns() + std::size_t(column);
    if (!grid_->finite(i)) {
        bridge_->setHead(false, {});
        emit inspected("Missing or nonfinite cell");
        return;
    }
    const auto w = grid_->values[i];
    bridge_->setHead(true,
                     position(grid_->x[std::size_t(column)],
                              (logY_ ? std::log10(grid_->y[std::size_t(row)]) : grid_->y[std::size_t(row)]),
                              SurfaceGeometry::height(w, complex_, height_), bridge_->geometry.bounds()));
    QString text = QString("row %1, column %2 | x=%3, y=%4 | ")
                       .arg(row)
                       .arg(column)
                       .arg(grid_->x[std::size_t(column)], 0, 'g', 12)
                       .arg(grid_->y[std::size_t(row)], 0, 'g', 12);
    text += complex_
                ? QString("w=(%1, %2), |w|=%3, phase=%4")
                      .arg(w.real(), 0, 'g', 12)
                      .arg(w.imag(), 0, 'g', 12)
                      .arg(std::abs(w), 0, 'g', 12)
                      .arg(w == std::complex<double>{} ? "undefined" : QString::number(std::arg(w), 'g', 12))
                : QString::number(w.real(), 'g', 17);
    emit inspected(text);
}
} // namespace s7studio::viewers
