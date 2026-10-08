#include "local_runtime_v2.hpp"
#include "local_schema_runner.hpp"
#include "scope_sample_mailbox.hpp"
#include "main_window.hpp"
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
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/transport.hpp>

#include <QApplication>
#include <QImage>
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
        s7::register_net_elements(registry);
        assert(registry.find("NetSink") != nullptr);
        assert(registry.find("NetSource") != nullptr);

        static constexpr std::string_view smokeSchema = R"json({
            "name": "s7studio_smoke",
            "elements": [
                { "id": "source", "type": "Constant", "params": { "value": 3.0 } },
                {
                    "id": "nested",
                    "type": "Schema",
                    "schema": {
                        "name": "nested_gain_chain",
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
                },
                { "id": "scope_main", "type": "Scope", "params": { "channel_count": 1 } }
            ],
            "connections": [
                { "from_element": "source", "from_port": 0, "to_element": "nested", "to_port": 0 },
                { "from_element": "nested", "from_port": 0, "to_element": "scope_main", "to_port": 0 }
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
                notified = mailbox.push(s7studio::ScopeSample{"scope", 0, step, 0.001 * step, static_cast<double>(step)});
                notifications += notified ? 1 : 0;
            }
            assert(notifications == 1);
            auto batch = mailbox.take();
            assert(batch.samples.size() == 4 && batch.dropped == 6);
            for (std::size_t index = 0; index < batch.samples.size(); ++index) {
                assert(batch.samples[index].step == static_cast<long>(6 + index));
            }
            [[maybe_unused]] const bool pushed = mailbox.push(s7studio::ScopeSample{"scope", 0, 10, 0.01, 10.0});
            assert(pushed);
            batch = mailbox.take();
            assert(batch.samples.size() == 1 && batch.dropped == 0);
        }

        const s7studio::runtime::detail::ScopeElement scope3{3};
        assert(scope3.input_ports().size() == 3 && scope3.output_ports().size() == 3);
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const std::string expected = "ch" + std::to_string(channel);
            assert(scope3.input_ports()[channel].name == expected);
            assert(scope3.output_ports()[channel].name == expected);
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
                "elements": [
                    { "id": "source", "type": "Constant", "params": { "value": 1.0 } },
                    { "id": "scope", "type": "Scope", "params": { "channel_count": 1 } }
                ],
                "connections": [ { "from_element": "source", "from_port": 0, "to_element": "scope", "to_port": 0 } ]
            })json");
            s7studio::LocalSchemaRunner runner;
            constexpr long ticks = 300'000;
            runner.run(scopeSchema, 0.001, ticks);
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
    s7studio::MainWindow window;
    window.show();
    // s7studio <file>: opens a diagram, schema or element library.
    if (argc == 2 && argv[1][0] != '-') {
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
