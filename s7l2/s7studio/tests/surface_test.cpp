// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission
#include "analysis/result_file.hpp"
#include "analysis/surface_grid.hpp"
#include "local_runtime_v2.hpp"
#include "viewers/surface_window.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMouseEvent>
#include <QQuickWidget>
#include <QTemporaryDir>
#include <QThread>
#include <boost/json.hpp>
#include <cmath>
#include <complex>
#include <fstream>
#include <iostream>
#include <limits>
#include <numbers>
#include <s7/core.hpp>
#include <s7/observe.hpp>
#include <stdexcept>

using namespace s7studio;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
void close(double actual, double expected) {
    require(std::abs(actual - expected) < 2e-12 * (1 + std::abs(expected)), "Reference mismatch");
}
template <class F> void rejects(F f) {
    bool rejected = false;
    try {
        f();
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    require(rejected, "Invalid grid accepted");
}
std::complex<double> reference(const std::string &name, double x, double y) {
    const std::complex<double> z{x, y};
    if (name == "plane")
        return {x + 2 * y, 0};
    if (name == "bowl")
        return {x * x + y * y, 0};
    if (name == "saddle")
        return {x * x - y * y, 0};
    if (name == "gaussian")
        return {std::exp(-x * x - y * y), 0};
    if (name == "wave")
        return {std::sin(x) * std::cos(y), 0};
    if (name == "sinc") {
        const auto r = std::hypot(x, y);
        return {r == 0 ? 1 : std::sin(r) / r, 0};
    }
    if (name == "identity")
        return z;
    if (name == "square")
        return z * z;
    if (name == "inverse")
        return 1.0 / z;
    return (z - 1.0) / (z + 1.0);
}
void invalidGrids() {
    analysis::SurfaceGrid grid;
    require(grid.append({-2, 3, 0, 3, 2, 1, 4, 0}), "First cell");
    require(!grid.append({-2, 3, 0, 3, 2, 0, 4, 0}), "Inactive hold created a cell");
    rejects([&] { grid.append({-2, 3, 0, 3, 2, 1, 4, 0}); });
    rejects([&] { grid.append({1, 3, 1, 4, 2, 1, 4, 0}); });
    rejects([&] { grid.append({1, 4, 1, 3, 2, 1, 4, 0}); });
    rejects([&] { grid.append({1, 3, .5, 3, 2, 1, 4, 0}); });
    rejects([&] { grid.append({1, 3, 1, 514, 2, 1, 4, 0}); });
    analysis::SurfaceGrid hole;
    for (int i = 0; i < 9; ++i)
        if (i != 4)
            hole.append({double(i % 3), double(i / 3), double(i), 3, 3, 1, double(i), 0});
    require(!hole.complete() && hole.received == 8, "Missing cell completeness");
    viewers::SurfaceGeometry geometry;
    geometry.setGrid(hole, false, viewers::SurfaceHeight::Real, false);
    require(geometry.triangleCount() == 2, "Triangles incident to a missing vertex must be removed");
    hole.append({1, 1, 4, 3, 3, 1, 4, 0});
    geometry.setGrid(hole, false, viewers::SurfaceHeight::Real, false);
    require(geometry.triangleCount() == 8, "Complete rectangular mesh topology");
    for (std::size_t r = 0; r < 3; ++r)
        hole.present[r * 3] = false;
    geometry.setGrid(hole, false, viewers::SurfaceHeight::Real, false);
    require(geometry.bounds().min[0] == 0 && geometry.bounds().max[0] == 2,
            "Validity masks must not shrink the declared coordinate domain");
    const auto a = viewers::SurfaceGeometry::color(std::polar(1.0, std::numbers::pi - 1e-8), true, 0, 1,
                                                   viewers::SurfaceHeight::Magnitude);
    const auto b = viewers::SurfaceGeometry::color(std::polar(1.0, -std::numbers::pi + 1e-8), true, 0, 1,
                                                   viewers::SurfaceHeight::Magnitude);
    require(std::abs(a.redF() - b.redF()) < 1e-5 && std::abs(a.greenF() - b.greenF()) < 1e-5,
            "Cyclic phase color seam");
}
void heatmapHover(bool complex) {
    viewers::SurfacePlotView plot(complex);
    plot.setPresentation(2);
    auto *heatmap = plot.findChild<QWidget *>("surface.heatmap");
    require(heatmap, "Surface heatmap missing");
    heatmap->resize(400, 300);
    int picks = 0;
    QObject::connect(&plot, &viewers::SurfacePlotView::cellPicked, &plot,
                     [&](int, int) { ++picks; });
    auto hover = [&] {
        QMouseEvent event(QEvent::MouseMove, QPointF(200, 150), QPointF(200, 150),
                          Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(heatmap, &event);
    };
    hover();
    require(picks == 0, "Empty heatmap must not pick a cell");
    auto grid = std::make_shared<analysis::SurfaceGrid>();
    for (int i = 0; i < 4; ++i)
        grid->append({double(i % 2), double(i / 2), double(i), 2, 2, 1, double(i), 0});
    plot.setGrid(grid);
    hover();
    require(picks == 1, "Populated heatmap must pick a cell");
    plot.setGrid(std::make_shared<analysis::SurfaceGrid>());
    hover();
    require(picks == 1, "Cleared heatmap must not pick a cell");
    plot.setGrid({});
    hover();
    require(picks == 1, "Reset heatmap must not pick a cell");
}
void examples(bool render) {
    for (const std::string name : {"plane", "bowl", "saddle", "gaussian", "wave", "sinc", "identity",
                                   "square", "inverse", "rational"}) {
        const bool complex =
            name == "identity" || name == "square" || name == "inverse" || name == "rational";
        const auto path = std::string(S7_SURFACE_EXAMPLE_DIR) +
                          (complex ? "/complex_functions/" : "/surfaces/") + name + ".s7diagram";
        std::ifstream in(path);
        require(in.good(), "Example file missing");
        const std::string contents((std::istreambuf_iterator<char>(in)), {});
        auto root = boost::json::parse(contents).as_object();
        auto schema = runtime::buildSchema(root.at("schema"));
        schema.set_rate(s7::Rate::from_hz(1000));
        const auto validation = s7::Validator::validate(schema);
        if (!validation.ok()) {
            for (const auto &e : validation.errors)
                std::cerr << e << '\n';
            throw std::runtime_error("Invalid surface diagram");
        }
        s7::NodeRuntime node(s7::Compiler::compile(schema));
        auto source = std::make_shared<observation::ObservationSource>(1);
        const auto roles = viewers::SurfaceWindow::roles(complex);
        for (const auto &role : roles)
            source->channel("surface/" + role, "F64");
        analysis::SurfaceGrid expected;
        std::size_t cells = 0;
        for (std::int64_t step = 1;; ++step) {
            node.step();
            std::array<double, 8> sample{};
            for (std::size_t p = 0; p < 6; ++p)
                sample[p] = node.real_output_of("grid", p);
            sample[6] = node.real_output_of("function", 0);
            if (complex)
                sample[7] = node.real_output_of("function", 1);
            if (sample[5] == 0)
                break;
            const auto value = reference(name, sample[0], sample[1]);
            close(sample[6], value.real());
            close(sample[7], value.imag());
            expected.append(sample);
            ++cells;
            for (int c = 0; c < roles.size(); ++c)
                require(source->append(
                            {observation::ChannelId(c), step, double(step) / 1000, sample[std::size_t(c)]}),
                        "Capture append");
        }
        require(source->finish(), "Capture flush");
        viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
        const QString type = complex ? "ComplexSurface3D" : "Surface3D";
        manager.present(source, 0, [type](const QString &) { return type; });
        auto *viewer = dynamic_cast<viewers::SurfaceWindow *>(manager.ensureObserver(type, "surface"));
        require(viewer, "Surface viewer registration");
        while (viewer->refreshSource()) {
        }
        require(viewer->error().isEmpty() && viewer->grid().complete() && viewer->grid().received == cells,
                "Late observer source replay");
        for (std::size_t i = 0; i < cells; ++i)
            require(viewer->grid().values[i] == expected.values[i], "Viewer/source parity");
        if (render && name == "saddle") {
            viewer->show();
            viewer->refreshPlot();
            QElapsedTimer timer;
            timer.start();
            while (timer.elapsed() < 800) {
                QApplication::processEvents();
                QThread::msleep(10);
            }
            auto *quick = viewer->findChild<QQuickWidget *>();
            require(quick && quick->status() == QQuickWidget::Ready, "Surface QML scene load");
            const auto image = quick->grabFramebuffer();
            require(!image.isNull(), "Surface framebuffer");
            std::size_t colored = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const auto c = image.pixelColor(x, y);
                    colored += (c.red() - c.blue() > 40 || c.blue() - c.red() > 40);
                }
            require(colored > 1000, "Surface must render colored pixels");
            if (const auto output = qEnvironmentVariable("S7_SURFACE_PNG"); !output.isEmpty())
                require(image.save(output), "Surface screenshot");
            const auto before = image;
            viewer->plot()->setViewRotation({-90, 0, 0});
            timer.restart();
            while (timer.elapsed() < 300) {
                QApplication::processEvents();
                QThread::msleep(10);
            }
            require(quick->grabFramebuffer() != before, "Camera projection did not change rendered surface");
            viewer->plot()->setPresentation(2);
            QApplication::processEvents();
            require(!viewer->grab().isNull(), "Heatmap presentation");
        }
        QTemporaryDir savedDirectory;
        const auto savedPath = savedDirectory.filePath("surface.s7analysis");
        viewer->saveResult(savedPath);
        auto saved = analysis::ResultFile::open(savedPath);
        saved.csv(savedDirectory.filePath("surface.csv"));
        const auto csv = analysis::ResultFile::open(savedDirectory.filePath("surface.csv"));
        require(csv.values == saved.values, "CSV round-trip");
        viewer->openResult(savedPath);
        require(viewer->grid().values == expected.values, "Surface result round-trip");
        viewer->clearSamples();
        require(viewer->grid().received == 0 && source->status().samples > 0, "Clear must preserve source");
        viewer->saveResult(savedPath);
        require(analysis::ResultFile::open(savedPath).values == expected.values,
                "Save must read authoritative capture after clear");
        delete viewer;
        auto *reopened = dynamic_cast<viewers::SurfaceWindow *>(manager.ensureObserver(type, "surface"));
        while (reopened->refreshSource()) {
        }
        require(reopened->grid().complete(), "Reopening lost captured surface");
        manager.beginRun("old", false);
        require(reopened->grid().received == 0, "New run leaked old cells");
        std::cout << name << ": " << cells << " reference cells passed\n";
    }
}
} // namespace
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        invalidGrids();
        heatmapHover(false);
        heatmapHover(true);
        examples(qEnvironmentVariableIsSet("S7_SURFACE_RENDER"));
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
