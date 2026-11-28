// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "diagram_document.hpp"
#include "local_schema_runner.hpp"
#include "scene_serializer.hpp"
#include "viewers/array_print.hpp"
#include "viewers/video_frame.hpp"
#include "viewers/viewer_manager.hpp"
#include "viewers/viewer_registry.hpp"
#include "viewers/waterfall_history.hpp"
#include "wire_type_labels.hpp"

#include <s7/audio.hpp>
#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/plugin_loader.hpp>
#include <s7/schema_library.hpp>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QPainter>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPrinter>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace s7studio::diagram;
using boost::json::array;
using boost::json::object;
using boost::json::value;

void require(bool condition, std::string_view reason)
{
    if (!condition) throw std::runtime_error(std::string(reason));
}

void saveViewerPdf(QWidget* viewer, const QString& artifact, const std::function<void()>& onDialog = {})
{
    auto* action = viewer->findChild<QAction*>(QStringLiteral("arrayViewer.savePdf"));
    require(action, "viewer has no PDF action");
    QTemporaryDir temporary;
    require(temporary.isValid(), "could not create a PDF test directory");
    const auto artifacts = qEnvironmentVariable("S7_GUI_ARTIFACT_DIR");
    const auto path = QDir(artifacts.isEmpty() ? temporary.path() : artifacts).filePath(artifact);
    const auto runDialog = [&](bool accepted) {
        bool handled = false;
        QTimer timer;
        timer.setInterval(25);
        QObject::connect(&timer, &QTimer::timeout, [&] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (!dialog || !dialog->isVisible()) return;
            timer.stop();
            auto* fileName = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
            handled = !accepted || fileName;
            if (!accepted || !fileName) {
                dialog->reject();
                return;
            }
            fileName->setText(path);
            if (onDialog) onDialog();
            QTimer::singleShot(0, dialog, [dialog] { QMetaObject::invokeMethod(dialog, "accept"); });
        });
        timer.start();
        action->trigger();
        require(handled, "viewer PDF dialog did not open");
    };

    QFile output(path);
    if (output.exists()) require(output.remove(), "could not remove the old PDF test artifact");
    runDialog(false);
    require(!output.exists(), "cancelled PDF dialog published a file");
    runDialog(true);
    require(output.open(QIODevice::ReadOnly) && output.read(5) == "%PDF-" && output.size() > 1000,
            "viewer PDF action did not write a complete PDF");
}

void checkPdfError()
{
    QTemporaryDir temporary;
    require(temporary.isValid(), "could not create a PDF error test directory");
    const s7studio::viewers::ArrayPrintSnapshot page{QStringLiteral("test"), {}, {},
        [](QPainter& painter, const QRect& bounds) { painter.fillRect(bounds, Qt::black); }};
    require(!s7studio::viewers::saveArrayPdf(page, QDir(temporary.path()).filePath("missing/test.pdf")).isEmpty(),
            "PDF export reported success for a missing destination directory");
    QPrinter printer;
    printer.setOutputFormat(QPrinter::PdfFormat);
    const auto path = QDir(temporary.path()).filePath("printer.pdf");
    printer.setOutputFileName(path);
    require(s7studio::viewers::printArraySnapshot(page, printer).isEmpty() && QFileInfo(path).size() > 1000,
            "Qt printer backend could not write the test page");
}

value fixture()
{
    return boost::json::parse(R"({
      "id": "mixed_ports",
      "dt": 0.01,
      "schema": {
        "name": "mixed_ports",
        "elements": [
          {"id":"source","type":"Schema","schema":{
            "name":"source","elements":[{"id":"signal","type":"Constant"}],
            "exposed_outputs":[
              {"name":"frame","type":"U8","shape":[2,4,3],"element":"signal","port":0},
              {"name":"status","type":"F64","element":"signal","port":0}
            ]
          }},
          {"id":"sink","type":"Schema","schema":{
            "name":"sink","elements":[{"id":"signal","type":"Gain"}],
            "exposed_inputs":[
              {"name":"frame","type":"U8","shape":[2,4,3],"element":"signal","port":0},
              {"name":"status","type":"F64","element":"signal","port":0}
            ]
          }}
        ],
        "connections":[
          {"from_element":"source","from_port":0,"to_element":"sink","to_port":0},
          {"from_element":"source","from_port":1,"to_element":"sink","to_port":1}
        ]
      }
    })");
}

void check_save_open()
{
    SerializationOptions options;
    options.schemaName = QStringLiteral("mixed_ports");
    options.dt = 0.01;
    DiagramScene scene;
    SceneSerializer serializer;
    const auto initial = fixture();
    serializer.deserialize(DiagramDocument::fromJson(initial), scene);
    const auto first = serializer.serialize(scene, options).toJson();
    QTemporaryDir directory;
    require(directory.isValid(), "unable to create a temporary diagram directory");
    QFile file(directory.filePath(QStringLiteral("mixed_ports.s7diagram")));
    const auto bytes = boost::json::serialize(first);
    require(file.open(QIODevice::WriteOnly) &&
                file.write(bytes.data(), static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()),
            "failed to save the test diagram");
    file.close();
    require(file.open(QIODevice::ReadOnly), "failed to reopen the test diagram");
    const auto loaded = file.readAll();
    DiagramScene reopened;
    serializer.deserialize(DiagramDocument::fromJson(boost::json::parse(
        std::string_view(loaded.constData(), static_cast<std::size_t>(loaded.size())))), reopened);
    const auto second = serializer.serialize(reopened, options).toJson();
    require(first == second, "save/open/save changed the diagram");
    require(first.as_object().at("schema").as_object().at("connections") ==
                initial.as_object().at("schema").as_object().at("connections"),
            "round trip changed the exposed wire endpoints");
    for (const auto& element : first.as_object().at("schema").as_object().at("elements").as_array()) {
        const auto& definition = element.as_object();
        const bool source = definition.at("id") == "source";
        const auto& nested = definition.at("schema").as_object();
        const auto ports = s7::parse_exposed_ports(nested, source ? "exposed_outputs" : "exposed_inputs");
        const std::array<std::size_t, 3> expected{2, 4, 3};
        require(ports.size() == 2 && ports[0].array && ports[0].type == s7::SignalType::U8 &&
                    std::ranges::equal(ports[0].array->extents(), expected) && ports[0].element == "signal" &&
                    ports[0].port == 0 && !ports[1].array && ports[1].type == s7::SignalType::F64,
                "persistence changed the array descriptor, alias or scalar port");
    }
}

void check_rejected_metadata()
{
    for (const auto* key : {"shape", "layout", "format"}) {
        auto input = fixture();
        auto& exposed = input.as_object().at("schema").as_object().at("elements").as_array()[0].as_object()
                            .at("schema").as_object().at("exposed_outputs").as_array()[0].as_object();
        if (std::string_view(key) == "shape") exposed[key] = array{};
        else exposed[key] = "unsupported";
        try { static_cast<void>(DiagramDocument::fromJson(input)); }
        catch (const std::invalid_argument& error) {
            require(std::string_view(error.what()).contains(key), error.what());
            continue;
        }
        throw std::runtime_error("unsupported exposed port declaration opened successfully");
    }
}

void check_wiring()
{
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    DiagramScene scene;
    const auto add = [&](const char* id, const char* type, object params, int x) {
        ElementNode::Element element;
        element.id = id;
        element.type = type;
        element.params = std::move(params);
        return scene.addElement(std::move(element), QPointF(x, 0));
    };
    const auto source = add("vector", "VectorConstant", {{"values", array{1, 2, 3}}}, 0);
    const auto wrong_size = add("wrong_size", "UnpackVector", {{"components", 2}}, 600);
    const auto wrong_type = add("wrong_type", "UnpackVector", {{"components", 3}, {"type", "F64"}}, 900);
    const auto target = add("unpack", "UnpackVector", {{"components", 3}}, 300);
    const auto scope = add("scope", "Scope", {}, 1200);
    const auto out = WireEnd::onPort(source, PortDirection::Output, 0);
    const auto input = [](const auto& node) { return WireEnd::onPort(node, PortDirection::Input, 0); };
    require(!scene.connectEnds(out, input(wrong_size)).isEmpty(), "Studio accepted mismatched vector lengths");
    require(!scene.connectEnds(out, input(wrong_type)).isEmpty(), "Studio accepted mismatched vector numeric types");
    require(!scene.connectEnds(out, input(scope)).isEmpty(), "Studio allowed an array into a scalar observer");
    require(scene.connectEnds(out, input(target)).isEmpty(), "Studio rejected a matching vector");
    QString error;
    const auto labels = updateWireTypeLabels(scene, &error);
    require(error.isEmpty() && labels.size() == 1 && labels.front().type == QStringLiteral("F32[3]"),
            "Studio wire label did not show the configured vector type and shape");
    require(target->inputs().front()->toolTip().contains(QStringLiteral("F32[3]")),
            "Studio input tooltip did not show the configured vector shape");
    static_cast<void>(add("unresolved", "UnavailableFixture", {}, 1500));
    require(!scene.connectEnds(out, input(wrong_size)).isEmpty(),
            "an unrelated unresolved element bypassed the vector shape check");

    const auto waterfall = add("waterfall", "Waterfall", {}, 1800);
    const auto video = add("video", "VideoPlayer", {}, 2100);
    const auto rgb = add("rgb", "TestPatternFrame", {{"width", 4}, {"height", 2}, {"format", "rgb8"}}, 2400);
    const auto constant = add("scalar", "Constant", {}, 2700);
    const auto scalar = WireEnd::onPort(constant, PortDirection::Output, 0);
    const auto frame = WireEnd::onPort(rgb, PortDirection::Output, 0);
    require(!scene.connectEnds(scalar, input(waterfall)).isEmpty(), "Waterfall accepted a scalar");
    require(!scene.connectEnds(out, input(video)).isEmpty(), "Video Player accepted an F32 vector");
    require(!scene.connectEnds(frame, input(waterfall)).isEmpty(), "Waterfall accepted an RGB frame");
    require(scene.connectEnds(out, input(waterfall)).isEmpty(), "Waterfall rejected an F32 vector descriptor");
    require(scene.connectEnds(frame, input(video)).isEmpty(), "Video Player rejected a matching RGB frame");

    SceneSerializer serializer;
    const auto document = serializer.serialize(scene, {});
    DiagramScene restored;
    serializer.deserialize(DiagramDocument::fromJson(document.toJson()), restored);
    const NetModel graph(restored);
    require(graph.connections().size() == 1 && graph.observationConnections().size() == 2 &&
                restored.elementNode("waterfall") && restored.elementNode("video"),
            "Viewer bindings were lost on reopen or entered the functional graph");
}

void check_delivery()
{
    using namespace s7studio;
    const auto model = boost::json::parse(R"({"elements":[
        {"id":"gray","type":"TestPatternFrame","params":{"width":4,"height":2,"format":"gray8"}},
        {"id":"rgb","type":"TestPatternFrame","params":{"width":4,"height":2,"format":"rgb8","source_id":2}}
    ]})");
    QTemporaryDir directory;
    require(directory.isValid(), "could not create an isolated recording directory");
    observation::StorageOptions storage;
    storage.temporaryDirectory = directory.path();
    LocalSchemaRunner runner(nullptr, storage);
    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    auto* viewer = manager.openObserver("VideoPlayer", "video");
    require(viewer, "could not open Video Player");
    const QString artifacts = qEnvironmentVariable("S7_GUI_ARTIFACT_DIR");
    const auto status = [&] {
        return viewer->widget()->findChild<QLabel*>("arrayViewer.status")->text();
    };
    const auto wait = [&] {
        QElapsedTimer timeout;
        timeout.start();
        while (runner.isRunning() && timeout.elapsed() < 5000) {
            QCoreApplication::processEvents();
            QThread::msleep(1);
        }
    };
    const auto probe = [](const char* modelId) {
        return RunProbe{"video/frame", modelId, 0, {}, false, {}, ArrayObserverKind::VideoPlayer};
    };
    runner.run(model, 0.01, 6, {probe("gray"),
        {"mirror/frame", "gray", 0, {}, false, {}, ArrayObserverKind::VideoPlayer},
        {"sequence", "gray", 1}});
    wait();
    require(runner.state() == RunState::Completed && runner.completedTicks() == 6 && runner.recordedSource(),
            "the observed graph or scalar recording failed");
    const auto recorded = runner.recordedSource()->read(0);
    require(recorded.samples.size() == 6 && recorded.samples.last().value == 6,
            "subscribing to the frame changed scalar sequence capture");
    const auto sources = runner.arraySources();
    const auto held = sources.value("mirror/frame").mailbox->take();
    require(held && held->value.origin && held->value.origin->sequence == 6, "fan-out missed the newest frame");
    manager.presentArrays(sources);
    manager.refreshViews();
    require(status().contains("Committed domain step 6") && status().contains("Replaced 5"),
            "Video Player did not show the latest frame and replacement count");
    if (!artifacts.isEmpty())
        require(viewer->widget()->grab().save(QDir(artifacts).filePath("video-player-delivery.png")),
                "could not capture the viewer");

    viewer->widget()->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    require(!manager.find("VideoPlayer", "video"), "closed viewer was still registered");
    viewer = manager.openObserver("VideoPlayer", "video");
    require(viewer && status().contains("Committed domain step 6"), "reopening lost the last committed frame");

    runner.run(model, 0.01, 2, {probe("rgb")});
    wait();
    require(runner.state() == RunState::Completed && !runner.recordedSource() &&
            !sources.value("video/frame").mailbox->latest() &&
            held->value.lease.values<s7::SignalType::U8>().size() == 8,
            "rebinding should close stale mailboxes and preserve already retained reads");
    manager.presentArrays(runner.arraySources());
    manager.refreshViews();
    const auto rgb = runner.arraySources().value("video/frame").mailbox->latest();
    require(rgb && rgb->value.image && rgb->value.image->pixel_format == s7::ImagePixelFormat::RGB8 &&
            status().contains("Committed domain step 2"), "the window did not bind to the new source");

    auto invalid = probe("gray");
    invalid.port = 1;
    runner.run(model, 0.01, 2, {invalid});
    require(runner.state() == RunState::Error && runner.arraySources().isEmpty(),
            "runner accepted a scalar as a frame");

    runner.run(model, 0.01, -1, {probe("gray")});
    manager.presentArrays(runner.arraySources());
    const auto waitUntil = [&](auto done) {
        QElapsedTimer timer;
        timer.start();
        while (!done() && runner.isRunning() && timer.elapsed() < 5000) QThread::msleep(1);
        require(done(), "continuous frame producer did not advance");
    };
    waitUntil([&] { return runner.completedTicks() >= 6; });
    manager.refreshViews();
    auto* pause = viewer->widget()->findChild<QPushButton*>("arrayViewer.pause");
    require(pause, "Video Player has no pause control");
    pause->click();
    const auto pausedStep = status().section('\n', 0, 0);
    const auto modelAtPause = runner.completedTicks();
    waitUntil([&] { return runner.completedTicks() >= modelAtPause + 100; });
    manager.refreshViews();
    require(status().section('\n', 0, 0) == pausedStep && runner.state() == RunState::Running,
            "presentation pause held the model or advanced the displayed frame");
    runner.stop();
    wait();
    pause->click();
    require(status().section('\n', 0, 0) != pausedStep, "resuming did not present the newest admitted frame");
}

void check_spectrum_routing()
{
    using namespace s7studio;
    const s7::ArrayDescriptor bins(s7::SignalType::F32, std::array<std::int64_t, 1>{4});
    s7::ArrayPool pool(bins, 1, 2, std::make_shared<s7::ArrayMemoryBudget>(3 * bins.byte_size()));
    auto admission = pool.make_retention(2, 2 * bins.byte_size());
    auto write = pool.try_acquire().value();
    const std::array<float, 4> powers{1, 0.01f, 0, 0.001f};
    std::ranges::copy(powers, write.values<s7::SignalType::F32>().begin());
    const auto buffer = std::move(write).seal();
    const auto rate = s7::Rate::from_hz(100);
    const auto feed = std::make_shared<s7::ArrayMailbox>();

    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    auto* waterfall = manager.openObserver("Waterfall", "waterfall");
    require(waterfall, "could not open Waterfall");
    const auto* status = waterfall->widget()->findChild<QLabel*>("arrayViewer.status");
    require(status, "Waterfall has no status label");
    manager.presentArrays({{"waterfall/spectrum", {ArrayObserverKind::VideoPlayer, feed}}});
    require(status->text().contains("No array source"), "Waterfall attached a Video Player subscription");
    manager.presentArrays({{"waterfall/spectrum", {ArrayObserverKind::Waterfall, feed}}});
    feed->offer(s7::CommittedArraySample{rate, 1, {admission.try_retain(buffer).value()}});
    manager.refreshViews();
    require(status->text().contains("no spectrum metadata"), "Waterfall accepted an unlabelled vector");
    const auto offer = [&](std::uint64_t step) {
        feed->offer(s7::CommittedArraySample{rate, step, {admission.try_retain(buffer).value(), {}, {},
            s7::ArraySpectrum{s7::SpectrumUnits::PowerPerBin, s7::SpectrumWindow::PeriodicHann, s7::Rate{800, 1}}}});
    };
    offer(2);
    manager.refreshViews();
    require(status->text().contains("Committed domain step 2"), "Waterfall did not receive a valid spectrum");
    auto* pause = waterfall->widget()->findChild<QPushButton*>("arrayViewer.pause");
    auto* plot = waterfall->widget()->findChild<QWidget*>("waterfall.plot");
    auto* palette = waterfall->widget()->findChild<QComboBox*>("waterfall.palette");
    auto* minimum = waterfall->widget()->findChild<QDoubleSpinBox*>("waterfall.minimumDb");
    require(pause && plot && palette && minimum, "Waterfall controls or plot are missing");
    pause->click();
    for (const auto step : {3U, 4U, 5U}) offer(step);
    manager.refreshViews();
    require(status->text().contains("Committed domain step 2") && status->text().contains("Replaced 2"),
            "Waterfall pause consumed pending rows or hid replacements");
    pause->click();
    require(status->text().contains("Committed domain step 5"), "Waterfall did not resume at the newest row");
    const auto heat = plot->grab().toImage();
    const auto hasColor = [](const QImage& image, QRgb color) {
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (image.pixel(x, y) == color) return true;
        return false;
    };
    require(hasColor(heat, viewers::WaterfallHistory::gapColor) && hasColor(heat, qRgb(255, 255, 102)),
            "Waterfall did not render the gap stripe and known heat color");
    palette->setCurrentIndex(1);
    const auto gray = plot->grab().toImage();
    require(hasColor(gray, viewers::WaterfallHistory::gapColor) && hasColor(gray, qRgb(204, 204, 204)) && gray != heat,
            "Changing the palette did not recolor retained rows");
    minimum->setValue(-50);
    require(hasColor(plot->grab().toImage(), qRgb(153, 153, 153)), "Changing the range did not recolor retained rows");
    minimum->setValue(-100);
    palette->setCurrentIndex(0);
    saveViewerPdf(waterfall->widget(), QStringLiteral("waterfall.pdf"), [&] {
        offer(6);
        manager.refreshViews();
    });
    const QString artifacts = qEnvironmentVariable("S7_GUI_ARTIFACT_DIR");
    if (!artifacts.isEmpty()) {
        minimum->setValue(-100);
        palette->setCurrentIndex(0);
        require(waterfall->widget()->grab().save(QDir(artifacts).filePath("waterfall-rendered.png")),
                "could not capture the viewer");
    }
}

void check_saved_views(const QString& fileName, bool spectral)
{
    using namespace s7studio;
    const QDir examples(qEnvironmentVariable("S7_ARRAY_TEST_EXAMPLES_DIR", QStringLiteral(S7_ARRAY_VIEW_EXAMPLES)));
    QFile file(examples.filePath(fileName));
    require(file.open(QIODevice::ReadOnly), "cannot open saved observer fixture");
    const auto bytes = file.readAll();
    const auto document = DiagramDocument::fromJson(boost::json::parse(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
    DiagramScene scene;
    SceneSerializer{}.deserialize(document, scene);
    QString typeError;
    static_cast<void>(updateWireTypeLabels(scene, &typeError));
    require(typeError.isEmpty(), typeError.toStdString());
    const NetModel graph(scene);
    require(graph.observationConnections().size() == (spectral ? 1U : 2U), "saved viewer bindings were lost");

    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    LocalSchemaRunner runner;
    std::vector<RunProbe> probes;
    for (const auto& wire : graph.observationConnections()) {
        const auto node = scene.elementNode(wire.to.element);
        const auto* viewer = viewers::ViewerRegistry::instance().find(QString::fromStdString(node->element().type));
        require(viewer && wire.to.port == 0, "saved observer type or input could not be resolved");
        probes.push_back({wire.to.element + "/" + viewer->inputSuffixes.front().toStdString(),
            wire.from.element, wire.from.port, {}, false, {}, viewer->arrayKind});
    }
    runner.run(document.runtimeSchema, document.dt, 6, probes);
    QElapsedTimer timeout;
    timeout.start();
    while (runner.isRunning() && timeout.elapsed() < 5000) QThread::msleep(1);
    require(runner.state() == RunState::Completed && runner.completedTicks() == 6, "saved array graph did not complete");
    manager.presentArrays(runner.arraySources());
    const auto artifacts = qEnvironmentVariable("S7_GUI_ARTIFACT_DIR");
    for (const auto& probe : probes) {
        const auto name = QString::fromStdString(probe.name);
        const auto id = viewers::ViewerManager::observerIdOf(name);
        const auto type = QString::fromStdString(std::string(arrayObserverType(probe.arrayObserver)));
        auto* window = manager.openObserver(type, id);
        require(window, "saved observer did not open");
        auto* widget = window->widget();
        const auto* status = widget->findChild<QLabel*>("arrayViewer.status");
        require(status && status->text().contains("Committed domain step 6"), "saved observer did not receive final step");
        const auto mailbox = runner.arraySources().value(name).mailbox;
        const auto sample = mailbox->latest();
        require(sample.has_value(), "latest sample was not available for the pixel oracle");
        if (!artifacts.isEmpty()) {
            const auto name = QFileInfo(fileName).fileName() + "-" + id + ".png";
            require(widget->grab().save(QDir(artifacts).filePath(name)), "cannot save viewer capture");
        }
        if (spectral) {
            require(sample->value.spectrum && sample->value.spectrum->sample_rate == s7::Rate{1600, 1},
                    "saved PSD graph lost the independent physical frequency axis");
            // The example's complex tone e^{j*pi*n/2} sits in bin 2 (400 Hz) of
            // 8. A periodic Hann window spreads it to bins 1..3 with DFT
            // magnitudes 2, 4, 2; sum(w^2) = 3, so PSD = |X|^2 / (3 * 1600 Hz).
            const auto power = sample->value.lease.values<s7::SignalType::F32>();
            constexpr std::array<double, 8> expected{0, 1.0 / 1200, 1.0 / 300, 1.0 / 1200, 0, 0, 0, 0};
            require(power.size() == expected.size(), "saved PSD graph has the wrong bin count");
            for (std::size_t bin = 0; bin < expected.size(); ++bin)
                require(std::abs(power[bin] - expected[bin]) < 1e-8,
                        "saved PSD graph did not reproduce the Hann-window tone oracle");
        } else {
            const auto frame = viewers::makeVideoFrame(*sample);
            auto incompatible = *sample;
            incompatible.value.image = s7::ArrayImage{id == "gray_view" ? s7::ImagePixelFormat::RGB8 : s7::ImagePixelFormat::Gray8};
            bool rejected = false;
            try { static_cast<void>(viewers::makeVideoFrame(incompatible)); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Video Player accepted inconsistent pixel-format metadata");
            require(frame.domainStep == 6 && frame.origin->sequence == 6 && frame.image.size() == QSize(5, 3),
                    "image adapter lost frame identity or geometry");
            require(frame.image.bytesPerLine() == (id == "gray_view" ? 8 : 16), "Qt row alignment lost an odd-width source stride");
            for (int y = 0; y < 3; ++y) {
                for (int x = 0; x < 5; ++x) {
                    const int gray = (x / 2 + y / 2) % 2 ? 208 : 32;
                    const auto expected = x == 0 ? (id == "gray_view" ? qRgb(255, 255, 255) : qRgb(255, 0, 0))
                                                 : qRgb(gray, gray, gray);
                    require(frame.image.pixel(x, y) == expected, "saved checkerboard pixels differ from the independent oracle");
                }
            }
            auto* display = widget->findChild<QWidget*>("videoPlayer.frame");
            require(display, "Video Player has no display canvas");
            const QImage actual = display->grab().toImage();
            const auto fitted = viewers::fitVideoFrame(frame.image.size(), actual.rect());
            require(actual.pixel(fitted.center()) == frame.image.pixel(2, 1) && actual.pixel(0, 0) == qRgb(0, 0, 0),
                    "Video Player pixels or aspect-fit bars did not render");
            saveViewerPdf(widget, QStringLiteral("video-") + id + QStringLiteral(".pdf"));
            require(runner.completedTicks() == 6, "printing advanced the model");
            auto* pause = widget->findChild<QPushButton*>("arrayViewer.pause");
            require(pause, "Video Player has no presentation pause");
            pause->click();
            mailbox->offer(s7::CommittedArraySample{sample->rate, 7, sample->value});
            manager.refreshViews();
            require(status->text().contains("Committed domain step 6") && runner.state() == RunState::Completed,
                    "Video Player pause advanced the presentation or affected the model");
            pause->click();
            require(status->text().contains("Committed domain step 7"), "Video Player did not resume at the newest frame");
        }
    }
}
void checkSavedReplay(const QString& diagramPath)
{
    using namespace s7studio;
    QFile file(diagramPath);
    require(file.open(QIODevice::ReadOnly), "cannot open saved MJPEG replay diagram");
    const auto bytes = file.readAll();
    const auto document = DiagramDocument::fromJson(boost::json::parse(
        std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))));
    DiagramScene scene;
    SceneSerializer{}.deserialize(document, scene);
    QString typeError;
    static_cast<void>(updateWireTypeLabels(scene, &typeError));
    require(typeError.isEmpty(), typeError.toStdString());
    const NetModel graph(scene);
    const auto connections = graph.observationConnections();
    require(connections.size() == 1 && connections.front().from.element == "decode" &&
            connections.front().to.element == "player", "saved replay lost the Video Player connection");

    LocalSchemaRunner runner;
    runner.run(document.runtimeSchema, document.dt, 5,
        {{"player/frame", "decode", 0, {}, false, {}, ArrayObserverKind::VideoPlayer}});
    QElapsedTimer timeout;
    timeout.start();
    while (runner.isRunning() && timeout.elapsed() < 5000) QThread::msleep(1);
    require(runner.state() == RunState::Completed && runner.completedTicks() == 5,
            "saved MJPEG replay did not complete five frames");

    viewers::ViewerManager manager(viewers::ViewerRegistry::instance());
    manager.presentArrays(runner.arraySources());
    auto* viewer = manager.openObserver(QStringLiteral("VideoPlayer"), QStringLiteral("player"));
    require(viewer, "saved replay did not open Video Player");
    const auto* status = viewer->widget()->findChild<QLabel*>("arrayViewer.status");
    require(status && status->text().contains("Committed domain step 5"), "Video Player did not receive the last frame");
    const auto sample = runner.arraySources().value("player/frame").mailbox->latest();
    require(sample.has_value(), "saved replay did not publish a frame");
    const auto frame = viewers::makeVideoFrame(*sample);
    require(frame.image.size() == QSize(65, 33) && frame.sourceStride == 65 &&
            frame.timing == s7::ArrayMediaTiming{s7::MediaPresentationTime{4, {25, 1}}, false},
            "Studio lost decoded geometry, stride or replay timing");
    saveViewerPdf(viewer->widget(), QStringLiteral("video-replay.pdf"));
}

class TestAudioStream final : public s7::hardware::Stream {
public:
    explicit TestAudioStream(const s7::hardware::Request& request) : Stream(describe(request)) {}

private:
    static s7::hardware::Descriptor describe(const s7::hardware::Request& request)
    {
        s7::hardware::Descriptor descriptor;
        descriptor.actual = request;
        return descriptor;
    }

    void do_start() override {}
    void do_stop() noexcept override {}

    s7::hardware::Transfer do_read(std::span<std::byte> output, std::size_t frames,
                                   s7::hardware::Deadline, std::stop_token) override
    {
        const auto channels = descriptor().actual.channels.size();
        // Exercise deinterleaving across multiple reads per committed block.
        frames = std::min<std::size_t>(frames, 311);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const auto signal = static_cast<float>(std::sin(2 * std::numbers::pi *
                    static_cast<double>((position_ + frame) % 48) * (channel == 0 ? 1 : 3) / 48));
                std::memcpy(output.data() + (frame * channels + channel) * sizeof(float), &signal, sizeof(float));
            }
        }
        position_ += frames;
        return {frames, s7::hardware::Status::Ok, 0, {}};
    }
    std::size_t position_{};
};

class TestAudioProvider final : public s7::hardware::Provider {
public:
    std::string_view id() const noexcept override { return "audio-test"; }
    std::vector<s7::hardware::DeviceInfo> devices() override { return {}; }
    std::unique_ptr<s7::hardware::Stream> prepare(const s7::hardware::Request& request) override
    {
        return std::make_unique<TestAudioStream>(request);
    }
};

// AudioBlockSource -> AudioBlockPSD through the hardware runner: mono and
// stereo blocks keep a common first sample, and each channel's PSD peaks at
// its own test tone (1 kHz left, 3 kHz right, 48 kHz, 2048-point blocks).
void checkAudioBlockPsd()
{
    auto& registry = s7::ElementRegistry::instance();
    s7::audio::register_elements(registry);
    for (const int count : {2, 1}) {
        auto schemaJson = boost::json::parse(R"({"name":"audio_psd","elements":[
            {"id":"capture","type":"AudioBlockSource","params":{"backend":"audio-test","device":"default",
             "channels":["0"],"frame_rate":[48000,1],"block_frames":2048,"queue_frames":8192}},
            {"id":"psd","type":"AudioBlockPSD","params":{"components":2048,"channels":1}}],
            "connections":[{"from_element":"capture","from_port":0,"to_element":"psd","to_port":0}]})").as_object();
        auto& elements = schemaJson.at("elements").as_array();
        elements[0].as_object().at("params").as_object()["channels"] = count == 2 ? array{"0", "1"} : array{"0"};
        elements[1].as_object().at("params").as_object()["channels"] = count;
        auto schema = s7::SchemaLibraryLoader::build_schema(schemaJson);
        s7::hardware::Run run(s7::Compiler::compile(schema), [](std::string_view) {
            return std::make_unique<TestAudioProvider>();
        });

        s7::ArrayPool::Retention reader;
        auto& runtime = run.runtime();
        std::vector<double> peaks;
        const auto stats = run.execute(2, std::chrono::milliseconds(100), {}, [&](std::uint64_t step) {
            const auto* descriptor = runtime.array_output_descriptor("psd", 0);
            require(descriptor && runtime.admit_array_output("psd", 0, reader, 1, descriptor->byte_size()),
                    "audio PSD admission failed");
            auto sample = runtime.try_retain_array_output("psd", 0, reader);
            require(sample && sample->value.sample_window && sample->value.sample_window->first_sample == (step - 1) * 2048,
                    "audio PSD lost its common sample position");
            require(sample->value.spectrum && sample->value.spectrum->units == s7::SpectrumUnits::PowerPerHz &&
                    sample->value.spectrum->sample_rate && sample->value.spectrum->sample_rate->to_hz() == 48000,
                    "audio PSD lost its spectrum metadata");
            const auto shape = descriptor->extents();
            require(shape.size() == 2 && shape[0] == count && shape[1] == 1025, "audio PSD has the wrong shape");
            const auto powers = sample->value.lease.values<s7::SignalType::F32>();
            peaks.clear();
            for (int channel = 0; channel < count; ++channel) {
                const auto row = powers.subspan(static_cast<std::size_t>(channel) * 1025, 1025);
                const auto bin = std::ranges::max_element(row) - row.begin();
                peaks.push_back(48000.0 / 2048.0 * static_cast<double>(bin));
            }
        });
        require(stats.status == s7::hardware::Status::Ok && stats.transferred_frames == 4096 &&
                stats.graph_frames == 2 && peaks.size() == static_cast<std::size_t>(count),
                "audio block run did not commit two blocks");
        for (int channel = 0; channel < count; ++channel)
            require(std::abs(peaks[static_cast<std::size_t>(channel)] - (channel == 0 ? 1000 : 3000)) < 12,
                    "audio PSD peak differs from the channel's test tone");
    }
}

} // namespace

int main(int argc, char** argv) try {
    QApplication application(argc, argv);
    check_save_open();
    check_rejected_metadata();
    check_wiring();
    check_delivery();
    check_spectrum_routing();
    checkPdfError();
    check_saved_views(QStringLiteral("video/video_patterns.s7diagram"), false);
    if (const auto plugin = qEnvironmentVariable("S7_ARRAY_TEST_SPECTRAL_PLUGIN"); !plugin.isEmpty()) {
        // The plugin comes from the s7spectral_sdk_smoke build, which skips
        // when no installed SDK is available.
        if (!QFileInfo::exists(plugin)) {
            std::cout << "spectral test plugin not built: " << plugin.toStdString() << '\n';
            return 77;
        }
        s7::PluginLoader loader;
        require(loader.load_plugin(plugin.toStdString(), s7::ElementRegistry::instance()), "cannot load spectral test plugin");
        check_saved_views(QStringLiteral("time_frequency/waterfall_psd.s7diagram"), true);
        checkAudioBlockPsd();
    }
    if (const auto plugin = qEnvironmentVariable("S7_ARRAY_TEST_VIDEO_PLUGIN"); !plugin.isEmpty()) {
        s7::PluginLoader loader;
        require(loader.load_plugin(plugin.toStdString(), s7::ElementRegistry::instance()), "cannot load video test plugin");
        checkSavedReplay(qEnvironmentVariable("S7_ARRAY_TEST_REPLAY_DIAGRAM"));
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
