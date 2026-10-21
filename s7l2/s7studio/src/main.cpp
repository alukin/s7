#include "local_runtime_v2.hpp"
#include "local_schema_runner.hpp"
#include "scope_sample_mailbox.hpp"
#include "main_window.hpp"
#include "scope_view_window.hpp"
#include "phase_portrait_view_window.hpp"
#include "phase_trajectory_3d_view_window.hpp"
#include "viewers/line_plot_view.hpp"
#include "viewers/trajectory_geometry.hpp"
#include <QtGraphs/QLineSeries>
#include "diagram_document.hpp"
#include "diagram_editor.hpp"
#include "deployment_support.hpp"
#include "scene_serializer.hpp"
#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"

#include <map>
#include <set>
#include <unordered_map>
#include "wire_type_labels.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/io.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/observe.hpp>
#include <s7/compiler.hpp>
#include <s7/transport.hpp>

#include <QApplication>
#include <QAction>
#include <QFile>
#include <QMenu>
#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>
#include <QImage>
#include <QGraphicsView>
#include <QTimer>
#include <QPainter>

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <thread>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <exception>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>
#include <cstring>
#include <cstdlib>
#include <utility>

#if defined(__linux__)
#include <sys/prctl.h>
#include <sys/resource.h>
#endif

namespace {

bool hasArg(int argc, char** argv, std::string_view needle)
{
    for (int index = 1; index < argc; ++index) {
        if (needle == argv[index]) {
            return true;
        }
    }

    return false;
}

auto executableName(char** argv) -> std::string
{
    if (!argv || !argv[0]) {
        return "s7studio";
    }

    return argv[0];
}

#if defined(__linux__)
auto readSingleLine(const char* path) -> std::string
{
    std::ifstream input(path);
    std::string line;
    std::getline(input, line);
    return line;
}

void enableCrashDumps()
{
    rlimit limits{};
    limits.rlim_cur = RLIM_INFINITY;
    limits.rlim_max = RLIM_INFINITY;
    if (setrlimit(RLIMIT_CORE, &limits) != 0) {
        std::cerr << "s7studio: warning: failed to raise RLIMIT_CORE: " << std::strerror(errno) << '\n';
    }

    if (prctl(PR_SET_DUMPABLE, 1) != 0) {
        std::cerr << "s7studio: warning: failed to mark process dumpable: " << std::strerror(errno) << '\n';
    }
}

void printCrashDumpHelp(const std::string& program)
{
    const std::string corePattern = readSingleLine("/proc/sys/kernel/core_pattern");
    std::cout << "s7studio crash-dump help\n";
    std::cout << "program=" << program << '\n';
    std::cout << "core_pattern=" << corePattern << '\n';
    std::cout << "RLIMIT_CORE will be raised when --enable-core-dumps is passed.\n";

    if (!corePattern.empty() && corePattern.front() == '|') {
        std::cout << "This system pipes crashes to systemd-coredump, so you usually will not see a local core file appear next to the executable.\n";
        std::cout << "After a crash, export a real core file with:\n";
        std::cout << "  coredumpctl list s7studio\n";
        std::cout << "  coredumpctl dump s7studio --output ./core.s7studio\n";
        std::cout << "You can also inspect it directly with:\n";
        std::cout << "  coredumpctl gdb s7studio\n";
    } else {
        std::cout << "This system should write a core file directly according to core_pattern.\n";
    }
}
#else
void enableCrashDumps()
{
}

void printCrashDumpHelp(const std::string& program)
{
    std::cout << "s7studio crash-dump help\n";
    std::cout << "program=" << program << '\n';
    std::cout << "Automatic Linux core-dump configuration is not available on this platform.\n";
}
#endif

int runSmoke()
{
    try {
        auto& registry = s7::ElementRegistry::instance();
        s7::register_builtin_elements(registry);
        s7::register_io_elements(registry);
        s7::register_net_elements(registry);
        assert(registry.find("NetSink") != nullptr);
        assert(registry.find("NetSource") != nullptr);

        static constexpr std::string_view smokeSchema = R"json({
            "name": "s7studio_smoke",
            "dt": 1.0,
            "elements": [
                { "id": "source", "type": "Constant", "params": { "value": 3.0 } },
                {
                    "id": "nested",
                    "type": "Schema",
                    "schema": {
                        "name": "nested_gain_chain",
                        "dt": 1.0,
                        "elements": [
                            { "id": "g1", "type": "Gain", "params": { "factor": 2.0 } },
                            { "id": "g2", "type": "Gain", "params": { "factor": 5.0 } }
                        ],
                        "connections": [
                            { "from_element": "g1", "from_port": 0, "to_element": "g2", "to_port": 0 }
                        ],
                        "exposed_inputs": [
                            { "name": "in", "type": "F64", "element": "g1", "port": 0 }
                        ],
                        "exposed_outputs": [
                            { "name": "out", "type": "F64", "element": "g2", "port": 0 }
                        ]
                    }
                }
            ],
            "connections": [
                { "from_element": "source", "from_port": 0, "to_element": "nested", "to_port": 0 }
            ]
        })json";
        boost::system::error_code ec;
        const auto schemaJson = boost::json::parse(smokeSchema, ec);
        if (ec) {
            throw std::runtime_error("Failed to parse smoke schema: " + ec.message());
        }
        const auto schema = s7studio::runtime::buildSchema(schemaJson);
        s7studio::runtime::validateSchema(schema);

        auto graph = s7::Compiler::compile(schema);
        s7::NodeRuntime runtime{std::move(graph)};

        runtime.step();
        runtime.step();

        {
            // The Scope mailbox stays bounded, drops oldest first, keeps order,
            // and asks for one UI notification at a time.
            s7studio::ScopeSampleMailbox mailbox{4};
            bool notified = false;
            int notifications = 0;
            for (long step = 0; step < 10; ++step) {
                notified = mailbox.push(s7studio::ScopeSample{"probe", step, 0.001 * step, static_cast<double>(step)});
                notifications += notified ? 1 : 0;
            }
            assert(notifications == 1);
            auto batch = mailbox.take();
            assert(batch.samples.size() == 4 && batch.dropped == 6);
            for (std::size_t index = 0; index < batch.samples.size(); ++index) {
                assert(batch.samples[index].step == static_cast<long>(6 + index));
            }
            [[maybe_unused]] const bool pushed = mailbox.push(s7studio::ScopeSample{"probe", 10, 0.01, 10.0});
            assert(pushed);
            batch = mailbox.take();
            assert(batch.samples.size() == 1 && batch.dropped == 0);
        }

        std::cout << "s7studio smoke\n";
        std::cout << "schema=" << schema.name() << '\n';
        assert(std::abs(runtime.real_output_of("nested") - 30.0) < 1e-12);
        std::cout << "steps=" << runtime.step_count() << '\n';
        for (const auto& line : s7studio::runtime::collectSamples(schema, runtime)) {
            std::cout << line << '\n';
        }

        auto endpoint = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
        s7::RuntimeNetContext::set_endpoint(endpoint);
        const auto tx = endpoint->open_producer(s7::net_stream_config("5b3a9f6e-2c4d-4e8a-b1f0-9d8c7b6a5f4e"));
        const double sent = 8.5;
        // Not inside assert(): the NetSource below needs the sample in release builds too.
        [[maybe_unused]] const auto published = endpoint->publish(tx, &sent, 1);
        assert(published.fault == s7::stream::FaultCode::None);

        static constexpr std::string_view netSchema = R"json({
            "name": "s7studio_net_smoke",
            "dt": 1.0,
            "elements": [
                { "id": "net_in", "type": "NetSource", "params": { "stream_id": "5b3a9f6e-2c4d-4e8a-b1f0-9d8c7b6a5f4e" } }
            ],
            "connections": []
        })json";
        const auto netSchemaJson = boost::json::parse(netSchema, ec);
        if (ec) {
            throw std::runtime_error("Failed to parse studio net smoke schema: " + ec.message());
        }
        const auto netGraphSchema = s7studio::runtime::buildSchema(netSchemaJson);
        s7studio::runtime::validateSchema(netGraphSchema);
        auto netGraph = s7::Compiler::compile(netGraphSchema);
        s7::NodeRuntime netRuntime{std::move(netGraph)};
        netRuntime.step();
        assert(std::abs(netRuntime.real_output_of("net_in") - 8.5) < 1e-12);
        s7::RuntimeNetContext::set_endpoint(nullptr);

        {
            // A run that produces samples while the UI takes none keeps at most
            // the mailbox capacity; the rest is counted as dropped.
            const auto scopeSchema = boost::json::parse(R"json({
                "name": "scope_backlog",
                "dt": 0.001,
                "elements": [
                    { "id": "source", "type": "Constant", "params": { "value": 1.0 } }
                ],
                "connections": []
            })json");
            s7studio::LocalSchemaRunner runner;
            constexpr long ticks = 300'000;
            runner.run(scopeSchema, 0.001, ticks, {s7studio::RunProbe{"probe", "source", 0}});
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (runner.isRunning() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            assert(!runner.isRunning());
            const auto batch = runner.takeScopeSamples();
            assert(batch.samples.size() == 200'000);
            assert(batch.dropped == static_cast<std::uint64_t>(ticks) - 200'000);
            assert(batch.samples.back().step == ticks);
            assert(std::abs(batch.samples.back().timeSeconds - static_cast<double>(ticks) * 0.001) < 1e-9);
            runner.reset();
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "s7studio smoke error: " << ex.what() << '\n';
        return 1;
    }
}

auto readJson(const char* path) -> boost::json::value
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(std::string("cannot read ") + path);
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return boost::json::parse(text);
}

void registerElements()
{
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_io_elements(registry);
    s7::register_net_elements(registry);
}

// Loads a diagram document, or lays a plain runtime schema out.
void loadInto(s7studio::diagram::DiagramEditor& editor, const boost::json::value& json)
{
    if (json.as_object().contains("diagram") || json.as_object().contains("document_kind")) {
        editor.loadDocument(s7studio::diagram::DiagramDocument::fromJson(json));
    } else {
        editor.loadRuntimeSchema(json);
    }
}

void renderScene(QGraphicsScene& scene, const QString& path)
{
    const auto bounds = scene.itemsBoundingRect().adjusted(-20, -20, 20, 20);
    QImage image(bounds.size().toSize() * 2, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    scene.render(&painter, QRectF(), bounds);
    painter.end();
    image.save(path);
}

// --wire-types <diagram or schema JSON>: lays the diagram out in an (offscreen)
// editor, resolves its types like the wire labels do, and prints one
// "element:port=TYPE" line per connection, plus "error: ..." on a type error.
int runWireTypes(int argc, char** argv)
{
    QApplication app(argc, argv);
    registerElements();
    s7studio::diagram::DiagramEditor editor;
    loadInto(editor, readJson(argv[2]));
    QString error;
    for (const auto& label : s7studio::diagram::updateWireTypeLabels(*editor.scene(), &error)) {
        std::cout << label.element.toStdString() << ':' << label.port << '=' << label.type.toStdString() << '\n';
    }
    if (!error.isEmpty()) {
        std::cout << "error: " << error.toStdString() << '\n';
    }
    // Optional picture of the labelled diagram, for checking the rendering.
    if (const char* png = std::getenv("S7_WIRE_TYPES_PNG")) {
        renderScene(*editor.scene(), QString::fromLocal8Bit(png));
    }
    return 0;
}

// --normalize <in> <out>: loads a diagram (or lays a schema out) and writes
// it as Studio saves it (format version 2).
int runNormalize(int argc, char** argv)
{
    QApplication app(argc, argv);
    registerElements();
    const auto json = readJson(argv[2]);
    s7studio::diagram::DiagramEditor editor;
    loadInto(editor, json);
    const auto loaded = s7studio::diagram::DiagramDocument::fromJson(json);
    s7studio::diagram::SerializationOptions options;
    options.schemaName = loaded.runtimeSchema.is_object() && loaded.runtimeSchema.as_object().contains("name")
        ? QString::fromStdString(std::string(loaded.runtimeSchema.as_object().at("name").as_string()))
        : loaded.id;
    options.dt = loaded.dt;
    options.sheetOrientation = loaded.pageLayout.orientation;
    options.sheetWidthMm = loaded.pageLayout.widthMm;
    options.sheetHeightMm = loaded.pageLayout.heightMm;
    auto document = s7studio::diagram::SceneSerializer{}.serialize(*editor.scene(), options);
    document.id = loaded.id;
    document.runtimeNodes = loaded.runtimeNodes;
    document.streams = loaded.streams;
    // Root keys of the schema other than its elements and connections stay.
    if (loaded.runtimeSchema.is_object()) {
        auto schema = loaded.runtimeSchema.as_object();
        schema["elements"] = document.runtimeSchema.as_object().at("elements");
        schema["connections"] = document.runtimeSchema.as_object().at("connections");
        document.runtimeSchema = std::move(schema);
    }
    std::ofstream(argv[3]) << boost::json::serialize(document.toJson()) << '\n';
    if (const char* png = std::getenv("S7_NORMALIZE_PNG")) {
        renderScene(*editor.scene(), QString::fromLocal8Bit(png));
    }
    return 0;
}

// --deployment-diagram <diagram> <out>: loads the diagram into an (offscreen)
// editor, serializes it back as Studio saves it, assigns its streams as
// "Deploy to Nodes" does, and writes the result (placement and streams
// included). Prints "error: ..." lines for placement problems.
int runDeploymentDiagram(int argc, char** argv)
{
    QApplication app(argc, argv);
    registerElements();
    const auto loaded = s7studio::diagram::DiagramDocument::fromJson(readJson(argv[2]));

    s7studio::diagram::DiagramEditor editor;
    editor.loadDocument(loaded);
    s7studio::diagram::SerializationOptions options;
    options.schemaName = loaded.id;
    auto document = s7studio::diagram::SceneSerializer{}.serialize(*editor.scene(), options);
    document.runtimeNodes = loaded.runtimeNodes;
    document.streams = loaded.streams;

    auto diagram = s7studio::diagram::deploymentDiagram(document, {});
    // Optional picture with the placement labels, for checking the rendering.
    if (const char* png = std::getenv("S7_DEPLOYMENT_PNG")) {
        QMap<QString, QString> names;
        for (const auto& node : s7::deployment::parse_runtime_nodes(diagram)) {
            names[QString::fromStdString(node.node_uuid)] = QString::fromStdString(node.name);
        }
        s7studio::diagram::updatePlacementLabels(*editor.scene(), names);
        renderScene(*editor.scene(), QString::fromLocal8Bit(png));
    }
    const auto errors = s7::deployment::assign_streams(diagram, s7studio::diagram::studioOutputTypes(diagram));
    for (const auto& error : errors) {
        std::cout << "error: " << error << '\n';
    }
    std::ofstream(argv[3]) << boost::json::serialize(diagram) << '\n';
    return errors.empty() ? 0 : 1;
}

auto connectionSet(const s7studio::diagram::DiagramScene& scene) -> std::set<std::string>
{
    std::set<std::string> result;
    for (const auto& connection : s7studio::diagram::NetModel(scene).connections()) {
        result.insert(connection.from.element + ":" + std::to_string(connection.from.port) + "->" + connection.to.element + ":" +
                      std::to_string(connection.to.port));
    }
    return result;
}

auto routesOf(const s7studio::diagram::DiagramScene& scene) -> std::map<std::string, std::string>
{
    std::map<std::string, std::string> routes;
    for (const auto& connection : s7studio::diagram::NetModel(scene).connections()) {
        std::string text;
        for (const auto& point : connection.wire->pointsAbsolute()) {
            text += "(" + std::to_string(std::lround(point.x())) + "," + std::to_string(std::lround(point.y())) + ")";
        }
        routes[connection.to.element + ":" + std::to_string(connection.to.port)] = text;
    }
    return routes;
}

// --edit-check <diagram>: adds an element through the Element Library slot of
// a main window and checks that every connection and route stays, then that
// a save/reload round trip keeps connections and routes.
int runEditCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    const auto document = s7studio::diagram::DiagramDocument::fromJson(readJson(argv[2]));

    s7studio::MainWindow window;
    auto* editor = window.findChild<s7studio::diagram::DiagramEditor*>();
    if (!editor) {
        std::cerr << "no diagram editor\n";
        return 2;
    }
    editor->loadDocument(document);
    auto& scene = *editor->scene();
    const auto expected = connectionSet(scene);
    const auto routes = routesOf(scene);
    const auto elementCount = scene.elementNodes().size();

    QMetaObject::invokeMethod(&window, "onElementRequested", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("Gain")),
                              Q_ARG(QString, QStringLiteral("Gain")));
    bool ok = true;
    if (connectionSet(scene) != expected) {
        std::cout << "error: adding an element changed the connections\n";
        ok = false;
    }
    if (scene.elementNodes().size() != elementCount + 1) {
        std::cout << "error: expected " << elementCount + 1 << " elements, got " << scene.elementNodes().size() << '\n';
        ok = false;
    }
    if (routesOf(scene) != routes) {
        std::cout << "error: adding an element away from the wires moved them\n";
        ok = false;
    }
    scene.undoStack()->undo();
    if (scene.elementNodes().size() != elementCount) {
        std::cout << "error: undo did not remove the added element\n";
        ok = false;
    }

    // Save and reload.
    const auto saved = s7studio::diagram::SceneSerializer{}.serialize(scene, {});
    s7studio::diagram::DiagramEditor reloaded;
    reloaded.loadDocument(s7studio::diagram::DiagramDocument::fromJson(saved.toJson()));
    if (connectionSet(*reloaded.scene()) != expected) {
        std::cout << "error: connections changed across save and reload\n";
        ok = false;
    }
    if (routesOf(*reloaded.scene()) != routes) {
        std::cout << "error: routes changed across save and reload\n";
        ok = false;
    }
    std::cout << (ok ? "edit check: OK (" : "edit check: FAILED (") << expected.size() << " connections)\n";
    return ok ? 0 : 1;
}

// Problems of the scene's routes: wires not attached at both connectors,
// non-orthogonal segments, segments through elements, and wires of different
// nets that overlap or touch.
auto routeProblems(const s7studio::diagram::DiagramScene& scene) -> std::vector<std::string>
{
    using s7studio::diagram::NetModel;
    std::vector<std::string> problems;
    const auto manager = scene.wire_manager();
    std::vector<std::pair<const s7studio::diagram::SignalWire*, std::string>> wires;
    for (const auto& connection : NetModel(scene).connections()) {
        const auto name = connection.from.element + ":" + std::to_string(connection.from.port) + " -> " + connection.to.element + ":" +
                          std::to_string(connection.to.port);
        const auto* wire = connection.wire;
        wires.emplace_back(wire, name);
        const auto points = wire->pointsAbsolute();
        const auto input = wire->downstream.port();
        if (!input || !manager->is_wire_attached_to(wire, input.get())) {
            problems.push_back("the wire of " + name + " is not attached to its input");
        }
        if (const auto output = wire->upstream.port(); output && !manager->is_wire_attached_to(wire, output.get())) {
            problems.push_back("the wire of " + name + " is not attached to its output");
        }
        for (int index = 0; index + 1 < points.size(); ++index) {
            const QPointF a = points[index];
            const QPointF b = points[index + 1];
            if (std::abs(a.x() - b.x()) > 0.5 && std::abs(a.y() - b.y()) > 0.5) {
                problems.push_back("the wire of " + name + " has a diagonal segment");
            }
            const QLineF segment(a, b);
            for (const auto& node : scene.elementNodes()) {
                const QRectF box = QRectF(node->pos(), node->size()).adjusted(2, 2, -2, -2);
                for (int step = 0; step <= 50; ++step) {
                    if (box.contains(segment.pointAt(step / 50.0))) {
                        problems.push_back("the wire of " + name + " crosses " + node->id());
                        break;
                    }
                }
            }
        }
    }
    // Different nets: no shared stretch, and no end or bend of one on the other.
    auto onSegment = [](const QPointF p, const QPointF a, const QPointF b) {
        return std::abs(QLineF(a, p).length() + QLineF(p, b).length() - QLineF(a, b).length()) < 0.01;
    };
    for (std::size_t i = 0; i < wires.size(); ++i) {
        for (std::size_t j = i + 1; j < wires.size(); ++j) {
            if (NetModel::rootOf(*wires[i].first) == NetModel::rootOf(*wires[j].first)) {
                continue;
            }
            const auto a = wires[i].first->pointsAbsolute();
            const auto b = wires[j].first->pointsAbsolute();
            bool touch = false;
            for (const auto& point : a) {
                for (int k = 0; k + 1 < b.size() && !touch; ++k) {
                    touch = onSegment(point, b[k], b[k + 1]);
                }
            }
            for (const auto& point : b) {
                for (int k = 0; k + 1 < a.size() && !touch; ++k) {
                    touch = onSegment(point, a[k], a[k + 1]);
                }
            }
            if (touch) {
                problems.push_back("the wires of " + wires[i].second + " and " + wires[j].second + " touch");
            }
        }
    }
    return problems;
}

// --layout-check <schema>: lays a schema without a diagram out and checks the
// automatic routing (routeProblems) and that every connection is drawn.
int runLayoutCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    registerElements();
    const auto schema = readJson(argv[2]);
    s7studio::diagram::DiagramEditor editor;
    editor.loadRuntimeSchema(schema);
    auto& scene = *editor.scene();

    bool ok = true;
    const auto expected = schema.as_object().at("connections").as_array().size();
    if (const auto drawn = connectionSet(scene).size(); drawn != expected) {
        std::cout << "error: " << drawn << " of " << expected << " connections drawn\n";
        ok = false;
    }
    for (const auto& problem : routeProblems(scene)) {
        std::cout << "error: " << problem << '\n';
        ok = false;
    }
    if (const char* png = std::getenv("S7_LAYOUT_PNG")) {
        renderScene(scene, QString::fromLocal8Bit(png));
    }
    std::cout << (ok ? "layout check: OK (" : "layout check: FAILED (") << expected << " connections)\n";
    return ok ? 0 : 1;
}
int runRecentDiagramsCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) throw std::runtime_error("Cannot create recent-diagrams fixture");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QApplication::setOrganizationName(QStringLiteral("s7-test"));
    QApplication::setApplicationName(QStringLiteral("recent-diagrams"));
    registerElements();
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    const auto recent = [] { return QSettings{}.value(QStringLiteral("files/recentDiagrams")).toStringList(); };
    auto document = readJson(S7STUDIO_SOURCE_DIR "/../examples/2int_osc.s7diagram");
    QStringList paths;
    for (int i = 0; i < 11; ++i) {
        document.as_object()["id"] = "recent_" + std::to_string(i);
        document.as_object().at("schema").as_object()["name"] = "recent_" + std::to_string(i);
        const auto path = directory.path() + QStringLiteral("/recent_%1.s7diagram").arg(i);
        QFile file(path);
        require(file.open(QIODevice::WriteOnly), "Cannot write recent diagram");
        const auto bytes = QByteArray::fromStdString(boost::json::serialize(document));
        require(file.write(bytes) == bytes.size(), "Cannot write recent diagram fixture");
        paths.append(path);
    }
    {
        s7studio::MainWindow window;
        require(recent().isEmpty(), "Demo was added to recent diagrams");
        auto* menu = window.findChild<QMenu*>(QStringLiteral("menu.recentDiagrams"));
        require(menu && !menu->actions().front()->isEnabled(), "Empty recent menu is missing");
        for (const auto& path : paths) window.openPath(path);
        require(recent().size() == 10 && recent().front() == paths[10] && !recent().contains(paths[0]),
            "Recent list ordering or size bound failed");
        window.openPath(directory.path() + QStringLiteral("/./recent_2.s7diagram"));
        require(recent().front() == paths[2] && recent().count(paths[2]) == 1, "Recent paths were not deduplicated");
        QAction* older = nullptr;
        for (auto* action : menu->actions()) if (action->data().toString() == paths[5]) older = action;
        require(older, "Recent diagram menu entry missing");
        older->trigger();
        require(recent().front() == paths[5] && window.windowTitle().remove(QStringLiteral("[*]")).endsWith(QStringLiteral("recent_5")),
            "Recent menu did not open and promote the chosen file");
        QMetaObject::invokeMethod(&window, "saveDocument", Qt::DirectConnection);
        require(recent().front() == paths[5], "Saving did not retain current diagram history");
        window.openPath(QStringLiteral(S7STUDIO_SOURCE_DIR "/../s7runtime/examples/schema_minimal.json"));
        require(recent().front() == paths[5] && recent().size() == 10, "Runtime schema polluted recent diagrams");
    }
    {
        s7studio::MainWindow restored;
        require(restored.windowTitle().remove(QStringLiteral("[*]")).endsWith(QStringLiteral("recent_5")), "Startup did not restore latest diagram");
        // Loading can run the event loop while hidden; the fit must wait for
        // the shown window's real viewport, and fit the sheet, not its nodes.
        app.processEvents();
        restored.resize(900, 600);
        restored.show();
        for (int i = 0; i < 5; ++i) app.processEvents();
        auto* editor = restored.findChild<s7studio::diagram::DiagramEditor*>();
        auto* view = editor->findChild<QGraphicsView*>();
        const auto page = editor->pageLayout();
        const auto width = page.orientation == s7studio::diagram::PageOrientation::Portrait
            ? std::min(page.widthMm, page.heightMm) : std::max(page.widthMm, page.heightMm);
        const auto height = page.orientation == s7studio::diagram::PageOrientation::Portrait
            ? std::max(page.widthMm, page.heightMm) : std::min(page.widthMm, page.heightMm);
        const QRectF sheet(0, 0, width * 96.0 / 25.4, height * 96.0 / 25.4);
        const auto sheetFits = [&] {
            return view->viewport()->rect().adjusted(-2, -2, 2, 2).contains(view->mapFromScene(sheet).boundingRect());
        };
        require(sheetFits(), "Startup sheet extends beyond the editor viewport");
        editor->zoomIn();
        const auto userZoom = view->transform().m11();
        restored.resize(950, 650);
        for (int i = 0; i < 5; ++i) app.processEvents();
        require(view->transform().m11() == userZoom, "Later layout changes reset user zoom");
        // Explicit page fit also supports viewports below the vendor's default
        // 25 percent zoom floor, without modifying vendored code.
        restored.resize(900, 360);
        for (int i = 0; i < 5; ++i) app.processEvents();
        editor->fitPageInView();
        for (int i = 0; i < 5; ++i) app.processEvents();
        require(sheetFits(), "Sheet does not fit a short viewport");
    }
    // Missing/corrupt latest files must not block startup with a modal dialog.
    QFile broken(paths[5]);
    require(broken.open(QIODevice::WriteOnly | QIODevice::Truncate), "Cannot corrupt fixture");
    broken.write("invalid JSON"); broken.close();
    {
        s7studio::MainWindow restored;
        require(restored.windowTitle().remove(QStringLiteral("[*]")).endsWith(QStringLiteral("recent_2")), "Corrupt recent file did not fall back");
    }
    require(QFile::remove(paths[2]), "Cannot remove missing-file fixture");
    {
        s7studio::MainWindow restored;
        require(restored.windowTitle().remove(QStringLiteral("[*]")).endsWith(QStringLiteral("recent_10")), "Missing recent file did not fall back");
        auto* menu = restored.findChild<QMenu*>(QStringLiteral("menu.recentDiagrams"));
        for (auto* action : menu->actions()) if (action->data().toString() == paths[2])
            require(!action->isEnabled(), "Missing recent file was not disabled");
    }
    {
        s7studio::MainWindow explicitFile(nullptr, false);
        require(!explicitFile.windowTitle().remove(QStringLiteral("[*]")).endsWith(QStringLiteral("recent_10")), "Explicit-file startup restored history");
        explicitFile.openPath(paths[1]);
        require(explicitFile.windowTitle().remove(QStringLiteral("[*]")).endsWith(QStringLiteral("recent_1")), "Explicit startup file did not win");
        explicitFile.findChild<QAction*>(QStringLiteral("action.clearRecentDiagrams"))->trigger();
        require(recent().isEmpty(), "Clearing recent diagrams did not persist");
    }
    {
        s7studio::MainWindow fallback;
        require(!fallback.windowTitle().contains(QStringLiteral("recent_")), "Cleared history was restored");
    }
    std::cout << "Recent diagrams: ordering, deduplication, menu actions, startup and fallback passed\n";
    return 0;
}

int runPhasePortraitCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) throw std::runtime_error("Cannot create Phase Portrait fixture");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QApplication::setOrganizationName(QStringLiteral("s7-test"));
    QApplication::setApplicationName(QStringLiteral("phase-portrait"));
    QSettings{}.setValue(QStringLiteral("scope/temporaryDirectory"), directory.path());
    registerElements();
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    const QString path = QStringLiteral(S7STUDIO_SOURCE_DIR "/../examples/2int_osc_phase_portrait.s7diagram");
    for (const int frequency : {1000, 10000, 100000}) {
        s7studio::MainWindow window(nullptr, false);
        window.openPath(path);
        auto* editor = window.findChild<s7studio::diagram::DiagramEditor*>();
        const s7studio::diagram::NetModel net(*editor->scene());
        require(net.problems().empty(), "Phase Portrait example wiring is invalid");
        const auto document = s7studio::diagram::DiagramDocument::fromJson(readJson(path.toLocal8Bit().constData()));
        auto schemaJson = document.runtimeSchema;
        const double dt = 1.0 / frequency;
        for (auto& element : schemaJson.as_object().at("elements").as_array()) {
            if (element.as_object().at("type").as_string() == "EulerIntegrator")
                element.as_object().at("params").as_object()["dt"] = dt;
        }
        std::vector<s7studio::RunProbe> probes;
        int axes = 0;
        for (const auto& connection : net.observationConnections()) {
            std::string name;
            if (connection.to.element == "phase_portrait_1") {
                require(connection.to.port == 0 || connection.to.port == 1, "Unexpected phase input port");
                const auto expected = connection.to.port == 0 ? "eulerintegrator_1" : "eulerintegrator_2";
                require(connection.from.element == expected && connection.from.port == 0, "Integrator axis mapping is reversed");
                name = "phase_portrait_1/" + std::string(connection.to.port == 0 ? "x" : "y");
                ++axes;
            } else {
                require(connection.to.element == "scope_1", "Unexpected observer in oscillator example");
                name = "scope_1/v";
            }
            probes.push_back({name, connection.from.element, connection.from.port});
        }
        require(axes == 2 && probes.size() == 3, "Missing diagram observer connections");
        auto referenceSchema = s7studio::runtime::buildSchema(schemaJson);
        referenceSchema.set_rate(s7::Rate::from_seconds(dt));
        s7studio::runtime::validateSchema(referenceSchema);
        s7::NodeRuntime reference(s7::Compiler::compile(referenceSchema));
        s7::ObservationService observation(reference);
        const long steps = 10 * frequency;
        std::vector<std::pair<double, double>> expected;
        expected.reserve(steps);
        for (long step = 0; step < steps; ++step) {
            reference.step();
            expected.emplace_back(observation.sample_output("eulerintegrator_1", 0).to_display_double(),
                                  observation.sample_output("eulerintegrator_2", 0).to_display_double());
        }
        auto* runner = window.findChild<s7studio::LocalSchemaRunner*>();
        require(runner, "Missing bundled runner");
        bool completed = false, success = false;
        QObject::connect(runner, &s7studio::LocalSchemaRunner::runCompleted, &window,
            [&](bool ok, const QString&) { completed = true; success = ok; });
        const auto open = [&] {
            require(QMetaObject::invokeMethod(&window, "onElementNodeDoubleClicked", Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("phase_portrait_1")), Q_ARG(QString, QStringLiteral("PhasePortrait")),
                Q_ARG(int, 2)), "Cannot open Phase Portrait observer");
        };
        // Exercise an already-open viewer at 1 kHz, and first opening after
        // completion at higher rates. Both must show the complete trajectory.
        if (frequency == 1000) open();
        runner->run(schemaJson, dt, steps, probes);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!completed && std::chrono::steady_clock::now() < deadline) {
            app.processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(completed && success, "Phase Portrait oscillator run failed or timed out");
        app.processEvents();
        QMetaObject::invokeMethod(&window, "onScopeSamplesAvailable", Qt::DirectConnection);
        open();
        s7studio::ui::PhasePortraitViewWindow* phase = nullptr;
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* candidate = qobject_cast<s7studio::ui::PhasePortraitViewWindow*>(widget))
                if (candidate->xProbeName() == QStringLiteral("phase_portrait_1/x")) phase = candidate;
        }
        require(phase, "Phase Portrait window was not created");
        for (auto* label : phase->findChildren<QLabel*>())
            require(!label->text().contains(QStringLiteral("display dropped")), "Phase Portrait run lost mailbox samples");
        const auto check = [&] {
            while (phase->refreshSource()) {} // test harness explicitly drains bounded history pages
            QMetaObject::invokeMethod(phase, "refreshPlot", Qt::DirectConnection);
            auto* plot = phase->plotView();
            require(plot->series().size() == 1 && plot->series().front() == phase->trajectorySeries(),
                "Phase Portrait must draw exactly one trajectory series");
            // Every pair is recorded; up to kMaxDrawnPoints are drawn, beyond
            // that a uniform subsample that keeps both ends.
            const auto& points = phase->points();
            if (points.size() != steps) {
                throw std::runtime_error("Phase Portrait state pairs: received "
                    + std::to_string(points.size()) + ", expected " + std::to_string(steps));
            }
            const auto drawnPoints = phase->trajectorySeries()->points();
            const qsizetype stride = (steps + s7studio::ui::PhasePortraitViewWindow::kMaxDrawnPoints - 1)
                / s7studio::ui::PhasePortraitViewWindow::kMaxDrawnPoints;
            require(drawnPoints.size() <= s7studio::ui::PhasePortraitViewWindow::kMaxDrawnPoints + 1
                && drawnPoints.front() == points.front() && drawnPoints.back() == points.back(),
                "Drawn phase trajectory is unbounded or lost its ends");
            for (qsizetype i = 0; i + 1 < drawnPoints.size(); ++i)
                require(drawnPoints[i] == points[i * stride], "Drawn phase trajectory is not a uniform subsample");
            if (stride == 1) require(drawnPoints == points, "Phase Portrait thinned a trajectory below its display limit");
            require(plot->xTitle() == QStringLiteral("phase_portrait_1/x")
                && plot->yTitle() == QStringLiteral("phase_portrait_1/y"), "Phase axes are mislabeled");
            long index = 0;
            for (const auto& point : points) {
                require(point.x() == expected[index].first && point.y() == expected[index].second,
                    "Plotted X/Y values differ from same-step headless integrator states");
                require(point.x() >= plot->xMin() && point.x() <= plot->xMax()
                    && point.y() >= plot->yMin() && point.y() <= plot->yMax(),
                    "Plotted phase point lies outside the visible axis ranges");
                const double amplitude = std::exp(.5 * index * std::log1p(dt * dt));
                const double angle = index * std::atan(dt);
                require(std::abs(point.x() - amplitude * std::cos(angle)) < 1e-10
                    && std::abs(point.y() - amplitude * std::sin(angle)) < 1e-10,
                    "Phase trajectory disagrees with forward-Euler discrete reference");
                ++index;
            }
            require(plot->xMin() <= -1 && plot->xMax() >= 1 && plot->yMin() <= -1 && plot->yMax() >= 1,
                "Phase trajectory is clipped by plot axes");
        };
        check();
        phase->close(); app.sendPostedEvents(nullptr, QEvent::DeferredDelete); open(); check();
        if (frequency == 1000) {
            if (const auto png = qEnvironmentVariable("S7_PHASE_PORTRAIT_PNG"); !png.isEmpty()) {
                for (int i = 0; i < 20; ++i) { app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
                require(phase->grab().save(png), "Cannot save Phase Portrait screenshot");
            }
        }
        const auto drawn = [&] {
            QMetaObject::invokeMethod(phase, "refreshPlot", Qt::DirectConnection);
            return phase->trajectorySeries()->points();
        };
        phase->clearSamples();
        require(drawn().isEmpty() && phase->points().isEmpty(), "Phase clear retained old points");
        // Mismatched steps must not manufacture a point, including after clear.
        phase->appendXSample(1, 5); phase->appendYSample(2, 6);
        require(drawn().isEmpty(), "Phase paired different runtime steps");
        phase->appendXSample(2, 7);
        const auto paired = drawn();
        require(paired.size() == 1 && paired.front() == QPointF(7, 6), "Reverse-order phase pairing failed");
        std::cout << "Phase Portrait " << frequency << " Hz: " << steps
                  << " exact headless/discrete-reference pairs; late open/reopen and pairing passed\n";
    }
    return 0;
}

int runPhaseTrajectory3DCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    if (!directory.isValid()) throw std::runtime_error("Cannot create 3D Phase Trajectory fixture");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    QApplication::setOrganizationName(QStringLiteral("s7-test"));
    QApplication::setApplicationName(QStringLiteral("phase-trajectory-3d"));
    QSettings{}.setValue(QStringLiteral("scope/temporaryDirectory"), directory.path());
    registerElements();
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    const QString path = QStringLiteral(S7STUDIO_SOURCE_DIR "/../examples/3int_helix_phase_trajectory.s7diagram");
    for (const int frequency : {1000, 10000}) {
        s7studio::MainWindow window(nullptr, false);
        window.openPath(path);
        auto* editor = window.findChild<s7studio::diagram::DiagramEditor*>();
        const s7studio::diagram::NetModel net(*editor->scene());
        require(net.problems().empty(), "3D Phase Trajectory example wiring is invalid");
        const auto document = s7studio::diagram::DiagramDocument::fromJson(readJson(path.toLocal8Bit().constData()));
        auto schemaJson = document.runtimeSchema;
        const double dt = 1.0 / frequency;
        for (auto& element : schemaJson.as_object().at("elements").as_array()) {
            if (element.as_object().at("type").as_string() == "EulerIntegrator")
                element.as_object().at("params").as_object()["dt"] = dt;
        }
        std::vector<s7studio::RunProbe> probes;
        int axes = 0;
        for (const auto& connection : net.observationConnections()) {
            std::string name;
            if (connection.to.element == "phase_trajectory_1") {
                require(connection.to.port >= 0 && connection.to.port <= 2, "Unexpected phase input port");
                const auto expected = connection.to.port == 0 ? "eulerintegrator_1" : connection.to.port == 1 ? "eulerintegrator_2" : "eulerintegrator_3";
                require(connection.from.element == expected && connection.from.port == 0, "Integrator axis mapping is reversed");
                name = "phase_trajectory_1/" + std::string(connection.to.port == 0 ? "x" : connection.to.port == 1 ? "y" : "z");
                ++axes;
            } else {
                require(connection.to.element == "scope_1", "Unexpected observer in oscillator example");
                name = "scope_1/v";
            }
            probes.push_back({name, connection.from.element, connection.from.port});
        }
        require(axes == 3 && probes.size() == 4, "Missing diagram observer connections");
        auto referenceSchema = s7studio::runtime::buildSchema(schemaJson);
        referenceSchema.set_rate(s7::Rate::from_seconds(dt));
        s7studio::runtime::validateSchema(referenceSchema);
        s7::NodeRuntime reference(s7::Compiler::compile(referenceSchema));
        s7::ObservationService observation(reference);
        const long steps = 10 * frequency;
        std::vector<std::array<double, 3>> expected;
        expected.reserve(steps);
        for (long step = 0; step < steps; ++step) {
            reference.step();
            expected.push_back({observation.sample_output("eulerintegrator_1", 0).to_display_double(),
                                observation.sample_output("eulerintegrator_2", 0).to_display_double(),
                                observation.sample_output("eulerintegrator_3", 0).to_display_double()});
        }
        auto* runner = window.findChild<s7studio::LocalSchemaRunner*>();
        require(runner, "Missing bundled runner");
        bool completed = false, success = false;
        QObject::connect(runner, &s7studio::LocalSchemaRunner::runCompleted, &window,
            [&](bool ok, const QString&) { completed = true; success = ok; });
        const auto open = [&] {
            require(QMetaObject::invokeMethod(&window, "onElementNodeDoubleClicked", Qt::DirectConnection,
                Q_ARG(QString, QStringLiteral("phase_trajectory_1")), Q_ARG(QString, QStringLiteral("PhaseTrajectory3D")),
                Q_ARG(int, 3)), "Cannot open 3D Phase Trajectory observer");
        };
        // Exercise an already-open viewer at 1 kHz, and first opening after
        // completion at higher rates. Both must show the complete trajectory.
        if (frequency == 1000) open();
        runner->run(schemaJson, dt, steps, probes);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (!completed && std::chrono::steady_clock::now() < deadline) {
            app.processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(completed && success, "3D Phase Trajectory oscillator run failed or timed out");
        app.processEvents();
        QMetaObject::invokeMethod(&window, "onScopeSamplesAvailable", Qt::DirectConnection);
        open();
        s7studio::ui::PhaseTrajectory3DViewWindow* phase = nullptr;
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* candidate = qobject_cast<s7studio::ui::PhaseTrajectory3DViewWindow*>(widget))
                if (candidate->xProbeName() == QStringLiteral("phase_trajectory_1/x")) phase = candidate;
        }
        require(phase, "3D Phase Trajectory window was not created");
        for (auto* label : phase->findChildren<QLabel*>())
            require(!label->text().contains(QStringLiteral("display dropped")), "3D Phase Trajectory run lost mailbox samples");
        const auto check = [&] {
            while (phase->refreshSource()) {} // test harness explicitly drains bounded history pages
            QMetaObject::invokeMethod(phase, "refreshPlots", Qt::DirectConnection);
            const auto& points = phase->points();
            const auto& geometry = phase->bridge()->trajectoryGeometry();
            require(points.size() == steps, "3D trajectory lost or duplicated triples");
            // One continuous line strip through every triple, in step order.
            require(geometry.primitiveType() == QQuick3DGeometry::PrimitiveType::LineStrip
                && geometry.vertexCount() == steps, "3D trajectory is not one continuous line through every triple");
            QStringList labels;
            for (const auto& label : phase->bridge()->labels()) labels << label.toMap().value(QStringLiteral("text")).toString();
            for (const auto* axis : {"X: phase_trajectory_1/x", "Y: phase_trajectory_1/y", "Z: phase_trajectory_1/z"})
                require(labels.contains(QString::fromLatin1(axis)), "3D axes are mislabeled");
            constexpr float extent = s7studio::viewers::kTrajectoryHalfExtent;
            long index = 0;
            for (const auto& point : points) {
                require(point[0] == expected[index][0] && point[1] == expected[index][1] && point[2] == expected[index][2],
                    "3D trajectory differs from same-step headless states");
                const double amplitude = std::exp(.5 * index * std::log1p(dt * dt));
                const double angle = index * std::atan(dt);
                const std::array<double, 3> analytical{amplitude * std::cos(angle),
                    amplitude * std::sin(angle), .2 * index * dt};
                for (int axis = 0; axis < 3; ++axis)
                    require(std::abs(point[axis] - analytical[axis]) < 1e-10, "3D helix disagrees with discrete reference");
                const auto vertex = geometry.vertexAt(index);
                require((vertex - s7studio::viewers::toScene(point, phase->bounds())).length() < 1e-3f,
                    "3D vertex does not show its triple");
                require(std::abs(vertex.x()) <= extent * 1.0001f && std::abs(vertex.y()) <= extent * 1.0001f
                    && std::abs(vertex.z()) <= extent * 1.0001f, "3D frame clips a trajectory point");
                ++index;
            }
        };
        check();
        phase->close(); app.sendPostedEvents(nullptr, QEvent::DeferredDelete); open(); check();
        if (frequency == 1000) {
            if (const auto png = qEnvironmentVariable("S7_PHASE_TRAJECTORY_3D_PNG"); !png.isEmpty()) {
                for (int i = 0; i < 20; ++i) { app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
                require(phase->grab().save(png), "Cannot save 3D viewer screenshot");
            }
        }
        const auto drawn = [&] {
            QMetaObject::invokeMethod(phase, "refreshPlots", Qt::DirectConnection);
            return phase->points();
        };
        phase->clearSamples();
        require(drawn().isEmpty() && phase->bridge()->trajectoryGeometry().vertexCount() == 0,
            "3D clear retained old points");
        phase->appendZSample(2, 9); phase->appendXSample(1, 5); phase->appendYSample(2, 8);
        require(drawn().isEmpty(), "3D mixed different runtime steps");
        phase->appendXSample(2, 7);
        const auto paired = drawn();
        require(paired.size() == 1 && paired.front() == s7studio::viewers::TrajectoryPoint{7, 8, 9},
            "3D reverse-order triple pairing failed");
        std::cout << "3D Phase Trajectory " << frequency << " Hz: " << steps
                  << " exact headless/discrete-reference triples; late open/reopen and pairing passed\n";
    }
    return 0;
}

// Exercise delivery, late opening and reopening with the actual oscillator.
int runScopeRunCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settingsDir;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settingsDir.path());
    QApplication::setOrganizationName(QStringLiteral("s7-test"));
    QApplication::setApplicationName(QStringLiteral("scope-run"));
    registerElements();
    for (const int frequency : {1000, 10000, 100000}) {
        s7studio::MainWindow window;
        const QString path = QStringLiteral(S7STUDIO_SOURCE_DIR "/../examples/2int_osc.s7diagram");
        window.openPath(path);
        auto schema = readJson(path.toLocal8Bit().constData()).as_object().at("schema");
        const double dt = 1.0 / frequency;
        for (auto& element : schema.as_object().at("elements").as_array()) {
            if (element.as_object().at("type").as_string() == "EulerIntegrator") {
                element.as_object().at("params").as_object()["dt"] = dt;
            }
        }
        auto* runner = window.findChild<s7studio::LocalSchemaRunner*>();
        if (!runner) throw std::runtime_error("Missing runner");
        runner->run(schema, dt, 10 * frequency, {{"scope_1/v", "eulerintegrator_2", 0}});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (runner->isRunning() && std::chrono::steady_clock::now() < deadline) {
            app.processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (runner->isRunning()) throw std::runtime_error("Oscillator run timed out");
        app.processEvents();
        QMetaObject::invokeMethod(&window, "onScopeSamplesAvailable", Qt::DirectConnection);
        const auto open = [&] {
            QMetaObject::invokeMethod(&window, "onProbeOpenScopeRequested", Qt::DirectConnection,
                                      Q_ARG(QString, QStringLiteral("scope_1")));
        };
        open();
        s7studio::ui::ScopeViewWindow* scope = nullptr;
        for (auto* widget : QApplication::topLevelWidgets()) {
            if (auto* candidate = qobject_cast<s7studio::ui::ScopeViewWindow*>(widget)) {
                if (candidate->probeName() == QStringLiteral("scope_1")) scope = candidate;
            }
        }
        if (!scope) throw std::runtime_error("Missing Scope window");
        const auto check = [&] {
            while (scope->refreshSource()) {} // late-open projection is intentionally incremental
            QMetaObject::invokeMethod(scope, "refreshPlot", Qt::DirectConnection);
            auto* plot = scope->plotView();
            auto* series = scope->plottedSeries(QStringLiteral("live:v"));
            if (!series || plot->series().size() != 1) throw std::runtime_error("Scope channel was not plotted");
            const auto data = series->points();
            if (data.isEmpty() || data.front().x() > 2 * dt
                || std::abs(data.back().x() - 10.0) > 1e-8
                || plot->xMin() > 2 * dt
                || std::abs(plot->xMax() - 10.0) > 1e-8) {
                throw std::runtime_error("Scope lost full run time range");
            }
            double previous = 0.0;
            for (const auto& point : data) {
                if (point.x() - previous > 0.01 || std::abs(point.y() - std::sin(point.x())) > 0.02) {
                    throw std::runtime_error("Scope lost oscillator curve");
                }
                previous = point.x();
            }
        };
        check();
        scope->close();
        app.sendPostedEvents(nullptr, QEvent::DeferredDelete);
        open();
        check();
        if (frequency == 1000) {
            if (const auto png = qEnvironmentVariable("S7_SCOPE_PNG"); !png.isEmpty()) {
                for (int i = 0; i < 20; ++i) { app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
                if (!scope->grab().save(png)) throw std::runtime_error("Cannot save Scope screenshot");
            }
        }
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) try
{
    const auto program = executableName(argv);

    if (hasArg(argc, argv, "--crash-dump-help")) {
        printCrashDumpHelp(program);
        return 0;
    }

    if (hasArg(argc, argv, "--enable-core-dumps")) {
        enableCrashDumps();
    }

    if (hasArg(argc, argv, "--phase-trajectory-3d-check")) {
        return runPhaseTrajectory3DCheck(argc, argv);
    }

    if (hasArg(argc, argv, "--phase-portrait-check")) {
        return runPhasePortraitCheck(argc, argv);
    }

    if (hasArg(argc, argv, "--recent-diagrams-check")) {
        return runRecentDiagramsCheck(argc, argv);
    }

    if (hasArg(argc, argv, "--scope-run-check")) {
        return runScopeRunCheck(argc, argv);
    }

    if (hasArg(argc, argv, "--smoke")) {
        return runSmoke();
    }

    if (argc == 3 && std::string_view(argv[1]) == "--wire-types") {
        return runWireTypes(argc, argv);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--layout-check") {
        return runLayoutCheck(argc, argv);
    }
    if (argc == 3 && std::string_view(argv[1]) == "--edit-check") {
        return runEditCheck(argc, argv);
    }
    if (argc == 4 && std::string_view(argv[1]) == "--deployment-diagram") {
        return runDeploymentDiagram(argc, argv);
    }
    if (argc == 4 && std::string_view(argv[1]) == "--normalize") {
        return runNormalize(argc, argv);
    }

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("s7"));
    QApplication::setApplicationName(QStringLiteral("s7studio"));
    const bool explicitFile = argc == 2 && argv[1][0] != '-';
    s7studio::MainWindow window(nullptr, !explicitFile);
    window.show();
    // s7studio <file>: opens a diagram, schema or element library.
    if (explicitFile) {
        window.openPath(QString::fromLocal8Bit(argv[1]));
    }
    // S7_STUDIO_SCREENSHOT=<png>: saves a picture of the main window and quits (for checking the layout).
    if (const char* png = std::getenv("S7_STUDIO_SCREENSHOT")) {
        QTimer::singleShot(800, &window, [&window, path = QString::fromLocal8Bit(png)] {
            window.grab().save(path);
            QApplication::quit();
        });
    }
    return QApplication::exec();
}
catch (const std::exception& ex) {
    std::cerr << "s7studio: fatal error: " << ex.what() << '\n';
    return 1;
}
catch (...) {
    std::cerr << "s7studio: unknown fatal error\n";
    return 1;
}
