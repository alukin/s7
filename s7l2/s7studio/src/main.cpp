#include "local_runtime_v2.hpp"
#include "local_schema_runner.hpp"
#include "scope_sample_mailbox.hpp"
#include "main_window.hpp"
#include "diagram_document.hpp"
#include "diagram_editor.hpp"
#include "deployment_support.hpp"
#include "scene_serializer.hpp"
#include "diagram_node_roles.hpp"
#include "runtime_schema_extractor.hpp"

#include <qschematic/items/node.hpp>
#include <qschematic/items/wire.hpp>
#include <qschematic/wire_system/manager.hpp>
#include <qschematic/wire_system/net.hpp>
#include <qschematic/scene.hpp>

#include <set>
#include <unordered_map>
#include "wire_type_labels.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/transport.hpp>

#include <QApplication>
#include <qschematic/scene.hpp>
#include <QImage>
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
            assert(mailbox.push(s7studio::ScopeSample{"scope", 0, 10, 0.01, 10.0}));
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
        assert(endpoint->publish(tx, &sent, 1).fault == s7::stream::FaultCode::None);

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

// --wire-types <diagram or schema JSON>: lays the diagram out in an (offscreen)
// editor, resolves its types like the wire labels do, and prints one
// "element:port=TYPE" line per wire, plus "error: ..." on a type error.
int runWireTypes(int argc, char** argv)
{
    QApplication app(argc, argv);
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);

    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot read " << argv[2] << '\n';
        return 2;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto json = boost::json::parse(text);

    s7studio::diagram::DiagramEditor editor;
    if (json.as_object().contains("diagram")) {
        editor.loadDocument(s7studio::diagram::DiagramDocument::fromJson(json));
    } else {
        editor.loadRuntimeSchema(json);
    }
    QString error;
    for (const auto& label : s7studio::diagram::updateWireTypeLabels(*editor.scene(), &error)) {
        std::cout << label.element.toStdString() << ':' << label.port << '=' << label.type.toStdString() << '\n';
    }
    if (!error.isEmpty()) {
        std::cout << "error: " << error.toStdString() << '\n';
    }
    // Optional picture of the labelled diagram, for checking the rendering.
    if (const char* png = std::getenv("S7_WIRE_TYPES_PNG")) {
        auto* scene = editor.scene();
        const auto bounds = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(bounds.size().toSize() * 2, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene->render(&painter, QRectF(), bounds);
        painter.end();
        image.save(QString::fromLocal8Bit(png));
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
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);

    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot read " << argv[2] << '\n';
        return 2;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto loaded = s7studio::diagram::DiagramDocument::fromJson(boost::json::parse(text));

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
        auto* scene = editor.scene();
        const auto bounds = scene->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(bounds.size().toSize() * 2, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene->render(&painter, QRectF(), bounds);
        painter.end();
        image.save(QString::fromLocal8Bit(png));
    }
    const auto errors = s7::deployment::assign_streams(diagram, s7studio::diagram::studioOutputTypes(diagram));
    for (const auto& error : errors) {
        std::cout << "error: " << error << '\n';
    }
    std::ofstream(argv[3]) << boost::json::serialize(diagram) << '\n';
    return errors.empty() ? 0 : 1;
}

// --edit-check <diagram>: the scene is the source of truth. Loads the diagram
// into the editor of a main window whose stored schema does not know it (as
// after drawing wires), adds a wire that connects nothing, adds an element
// through the Element Library slot, and checks that every connection
// survives, then that a save/reload round trip keeps connections and routes.
int runEditCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot read " << argv[2] << '\n';
        return 2;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto document = s7studio::diagram::DiagramDocument::fromJson(boost::json::parse(text));

    s7studio::MainWindow window;
    auto* editor = window.findChild<s7studio::diagram::DiagramEditor*>();
    if (!editor) {
        std::cerr << "no diagram editor\n";
        return 2;
    }
    editor->loadDocument(document);
    auto connections = [](QSchematic::Scene& scene) {
        const auto schema = s7studio::diagram::SceneSerializer{}.serialize(scene, {}).runtimeSchema.as_object();
        std::set<std::string> result;
        for (const auto& c : schema.at("connections").as_array()) {
            result.insert(boost::json::serialize(c));
        }
        return std::make_pair(result, schema.at("elements").as_array().size());
    };
    const auto [expected, element_count] = connections(*editor->scene());

    auto dangling = std::make_shared<QSchematic::Items::Wire>();
    dangling->append_point(QPointF(-500, -500));
    dangling->append_point(QPointF(-400, -500));
    editor->scene()->addWire(dangling);

    QMetaObject::invokeMethod(&window, "onElementRequested", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("Gain")),
                              Q_ARG(QString, QStringLiteral("Gain")));
    const auto [after, after_count] = connections(*editor->scene());
    bool ok = true;
    if (after != expected) {
        std::cout << "error: adding an element changed the connections (" << expected.size() << " -> " << after.size() << ")\n";
        ok = false;
    }
    if (after_count != element_count + 1) {
        std::cout << "error: expected " << element_count + 1 << " elements, got " << after_count << '\n';
        ok = false;
    }

    // Save and reload: routes belong to their connections.
    const auto saved = s7studio::diagram::SceneSerializer{}.serialize(*editor->scene(), {});
    const auto& wires = saved.diagram.at("wires").as_array();
    if (wires.size() != expected.size()) {
        std::cout << "error: " << wires.size() << " routes saved for " << expected.size() << " connections\n";
        ok = false;
    }
    for (const auto& wire : wires) {
        if (!wire.as_object().contains("from_element") || !wire.as_object().contains("to_element")) {
            std::cout << "error: a route does not name its connection\n";
            ok = false;
        }
    }
    s7studio::diagram::DiagramEditor reloaded;
    reloaded.loadDocument(s7studio::diagram::DiagramDocument::fromJson(saved.toJson()));
    if (connections(*reloaded.scene()).first != expected) {
        std::cout << "error: connections changed across save and reload\n";
        ok = false;
    }
    std::cout << (ok ? "edit check: OK (" : "edit check: FAILED (") << expected.size() << " connections)\n";
    return ok ? 0 : 1;
}

// --layout-check <schema>: lays a schema without a diagram out and checks the
// automatic routing: every connection's wire is attached at both connectors,
// no wire segment passes through a node, and every QSchematic net carries
// one output (wires of different outputs never touch).
int runLayoutCheck(int argc, char** argv)
{
    QApplication app(argc, argv);
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);
    std::ifstream in(argv[2]);
    if (!in) {
        std::cerr << "cannot read " << argv[2] << '\n';
        return 2;
    }
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto schema = boost::json::parse(text);
    s7studio::diagram::DiagramEditor editor;
    editor.loadRuntimeSchema(schema);
    auto& scene = *editor.scene();

    bool ok = true;
    std::vector<s7studio::diagram::WireSource> sources;
    const auto extracted = s7studio::diagram::RuntimeSchemaExtractor{}.extract(scene, {}, &sources).as_object();
    const auto expected = schema.as_object().at("connections").as_array().size();
    if (extracted.at("connections").as_array().size() != expected) {
        std::cout << "error: " << extracted.at("connections").as_array().size() << " of " << expected
                  << " connections attached at both connectors\n";
        std::set<std::string> attached;
        for (const auto& c : extracted.at("connections").as_array()) {
            attached.insert(boost::json::serialize(c));
        }
        for (const auto& c : schema.as_object().at("connections").as_array()) {
            if (!attached.contains(boost::json::serialize(c))) {
                std::cout << "error: not attached: " << boost::json::serialize(c) << "\n";
            }
        }
        ok = false;
    }
    std::unordered_map<const void*, std::string> outputOf;
    for (const auto& source : sources) {
        outputOf[static_cast<const wire_system::wire*>(source.wire)] = source.element + ":" + std::to_string(source.port);
        const auto points = source.wire->pointsAbsolute();
        for (int index = 0; index + 1 < points.size(); ++index) {
            const QLineF segment(points[index], points[index + 1]);
            for (const auto& node : scene.items<QSchematic::Items::Node>()) {
                const auto box = node->sceneBoundingRect().adjusted(2, 2, -2, -2);
                // Orthogonal segments: sample the segment densely.
                for (int step = 0; step <= 50; ++step) {
                    if (box.contains(segment.pointAt(step / 50.0))) {
                        std::cout << "error: the wire of " << source.element << ":" << source.port << " -> " << source.toElement << ":"
                                  << source.toPort << " crosses " << node->data(s7studio::diagram::kNodeIdRole).toString().toStdString() << '\n';
                        ok = false;
                        step = 51;
                    }
                }
            }
        }
    }
    for (const auto& net : scene.wire_manager()->nets()) {
        std::set<std::string> outputs;
        for (const auto& wire : net->wires()) {
            if (const auto it = outputOf.find(wire.get()); it != outputOf.end()) {
                outputs.insert(it->second);
            }
        }
        if (outputs.size() > 1) {
            std::string names;
            for (const auto& output : outputs) {
                names += " " + output;
            }
            std::cout << "error: one net joins the wires of " << outputs.size() << " outputs:" << names << "\n";
            ok = false;
        }
    }
    if (const char* png = std::getenv("S7_LAYOUT_PNG")) {
        const auto bounds = scene.itemsBoundingRect().adjusted(-20, -20, 20, 20);
        QImage image(bounds.size().toSize() * 2, QImage::Format_ARGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        scene.render(&painter, QRectF(), bounds);
        painter.end();
        image.save(QString::fromLocal8Bit(png));
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

    QApplication app(argc, argv);
    s7studio::MainWindow window;
    window.show();
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
