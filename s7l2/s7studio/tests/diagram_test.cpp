// Diagram editing (offscreen): connection rules, wire trees, deletion,
// undo, routing around elements, user bends and the file round trip.

#include "diagram/diagram_scene.hpp"
#include "diagram/net_model.hpp"
#include "element_requirements.hpp"
#include "scene_serializer.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/element_registry.hpp>
#include <s7/net.hpp>
#include <s7/plugin_loader.hpp>

#include <qschematic/commands/item_move.hpp>

#include <QApplication>
#include <QGraphicsSceneMouseEvent>
#include <QUndoStack>
#include <QVector2D>

#include <cmath>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

using namespace s7studio::diagram;

namespace {

int failures = 0;

void check(const bool condition, const std::string& what)
{
    if (!condition) {
        std::cout << "FAILED: " << what << '\n';
        ++failures;
    }
}

auto element(const std::string& id, const std::string& type, boost::json::object params = {}) -> ElementNode::Element
{
    ElementNode::Element result;
    result.id = id;
    result.type = type;
    result.params = std::move(params);
    return result;
}

auto out(const std::shared_ptr<ElementNode>& node, const int index = 0) -> WireEnd
{
    return WireEnd::onPort(node, PortDirection::Output, index);
}

auto in(const std::shared_ptr<ElementNode>& node, const int index = 0) -> WireEnd
{
    return WireEnd::onPort(node, PortDirection::Input, index);
}

auto connections(const DiagramScene& scene) -> std::set<std::string>
{
    std::set<std::string> result;
    for (const auto& connection : NetModel(scene).connections()) {
        result.insert(connection.from.element + ":" + std::to_string(connection.from.port) + "->" + connection.to.element + ":" +
                      std::to_string(connection.to.port));
    }
    return result;
}

auto wireInto(const DiagramScene& scene, const std::shared_ptr<ElementNode>& node, const int index) -> std::shared_ptr<SignalWire>
{
    for (const auto& wire : scene.signalWires()) {
        if (wire->downstream.sameAs(in(node, index))) {
            return wire;
        }
    }
    return nullptr;
}

// Every route is orthogonal, starts and ends at its anchors, stays out of
// elements, passes its user bends, and branches start on their trunk.
void checkRoutes(const DiagramScene& scene, const std::string& when)
{
    for (const auto& wire : scene.signalWires()) {
        const auto points = wire->pointsAbsolute();
        const std::string name = when + ": wire into " + (wire->downstream.node.lock() ? wire->downstream.node.lock()->id() : "?");
        check(points.size() >= 2, name + " has a route");
        if (points.size() < 2) {
            continue;
        }
        if (const auto port = wire->downstream.port()) {
            check(QLineF(points.back(), port->scenePos()).length() < 0.5, name + " ends at its input");
        }
        if (const auto port = wire->upstream.port()) {
            check(QLineF(points.front(), port->scenePos()).length() < 0.5, name + " starts at its output");
        }
        if (wire->upstream.isJunction()) {
            const auto trunk = wire->upstream.wire.lock()->pointsAbsolute();
            bool on = false;
            for (int index = 0; index + 1 < trunk.size(); ++index) {
                const QLineF segment(trunk[index], trunk[index + 1]);
                on = on || std::abs(QLineF(trunk[index], points.front()).length() + QLineF(points.front(), trunk[index + 1]).length() -
                                    segment.length()) < 0.01;
            }
            check(on, name + " starts on its trunk");
        }
        for (int index = 0; index + 1 < points.size(); ++index) {
            const QPointF a = points[index];
            const QPointF b = points[index + 1];
            check(std::abs(a.x() - b.x()) < 0.5 || std::abs(a.y() - b.y()) < 0.5, name + " is orthogonal");
            for (const auto& node : scene.elementNodes()) {
                const QRectF box = QRectF(node->pos(), node->size()).adjusted(2, 2, -2, -2);
                bool crosses = false;
                for (int step = 0; step <= 40; ++step) {
                    crosses = crosses || box.contains(QLineF(a, b).pointAt(step / 40.0));
                }
                check(!crosses, name + " does not cross " + node->id());
            }
        }
        for (const auto& bend : wire->userBends) {
            bool on = false;
            for (int index = 0; index + 1 < points.size(); ++index) {
                on = on || std::abs(QLineF(points[index], bend).length() + QLineF(bend, points[index + 1]).length() -
                                    QLineF(points[index], points[index + 1]).length()) < 0.01;
            }
            check(on, name + " passes its bend");
        }
    }
}

void mouse(DiagramScene& scene, const QEvent::Type type, const QPointF pos, const Qt::MouseButton button = Qt::LeftButton,
           const Qt::KeyboardModifiers modifiers = Qt::NoModifier)
{
    QGraphicsSceneMouseEvent event(type);
    event.setScenePos(pos);
    event.setLastScenePos(pos);
    event.setButtonDownScenePos(button, pos);
    event.setButton(type == QEvent::GraphicsSceneMouseMove ? Qt::NoButton : button);
    event.setButtons(type == QEvent::GraphicsSceneMouseRelease ? Qt::NoButton : Qt::MouseButtons(button));
    event.setModifiers(modifiers);
    QCoreApplication::sendEvent(&scene, &event);
}

void click(DiagramScene& scene, const QPointF pos)
{
    mouse(scene, QEvent::GraphicsSceneMousePress, pos);
    mouse(scene, QEvent::GraphicsSceneMouseRelease, pos);
}

void drag(DiagramScene& scene, const QPointF from, const QPointF to)
{
    mouse(scene, QEvent::GraphicsSceneMousePress, from);
    mouse(scene, QEvent::GraphicsSceneMouseMove, (from + to) / 2.0);
    mouse(scene, QEvent::GraphicsSceneMouseMove, to);
    mouse(scene, QEvent::GraphicsSceneMouseRelease, to);
}

auto reload(const DiagramScene& scene, DiagramScene& into) -> DiagramDocument
{
    SerializationOptions options;
    options.schemaName = QStringLiteral("test");
    const auto saved = SceneSerializer{}.serialize(scene, options);
    SceneSerializer{}.deserialize(DiagramDocument::fromJson(saved.toJson()), into);
    return saved;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);

    DiagramScene scene;
    auto& undo = *scene.undoStack();
    const auto c1 = scene.addElement(element("c1", "Constant", {{"value", 1.0}}), {40, 40});
    const auto c2 = scene.addElement(element("c2", "Constant", {{"value", 2.0}}), {40, 240});
    const auto gain = scene.addElement(element("g", "Gain", {{"factor", 2.0}}), {400, 40});
    const auto adder = scene.addElement(element("sum", "Adder"), {400, 240});
    const auto i16 = scene.addElement(element("i16", "Constant", {{"type", "I16"}}), {40, 440});
    const auto f32 = scene.addElement(element("f32", "Constant", {{"type", "F32"}}), {40, 640});
    const auto mix = scene.addElement(element("mix", "Adder"), {400, 540});
    check(scene.elementNodes().size() == 7, "elements added");

    // Rules.
    check(scene.connectEnds(out(c1), in(gain)).isEmpty(), "output to input connects");
    check(!scene.connectEnds(out(c1), out(c2)).isEmpty(), "two outputs are refused");
    check(!scene.connectEnds(in(gain), in(adder)).isEmpty(), "two inputs are refused");
    check(!scene.connectEnds(out(c2), in(gain)).isEmpty(), "an input fed twice is refused");
    const auto trunk = wireInto(scene, gain, 0);
    check(trunk && trunk->upstream.isOutput(), "the first wire of an output starts at the output");
    check(!scene.connectEnds(out(c2), WireEnd::onWire(trunk, trunk->pointsAbsolute()[1])).isEmpty(),
          "an output onto a wire carrying another signal is refused");
    check(scene.connectEnds(in(mix, 0), out(i16)).isEmpty(), "input to output connects (drawn backwards)");
    check(!scene.connectEnds(out(f32), in(mix, 1)).isEmpty(), "incompatible types are refused (I16 and F32 into one Adder)");

    // Fan-out from an output: the second wire branches off the first.
    check(scene.connectEnds(out(c1), in(adder, 0)).isEmpty(), "a second wire from an output connects");
    const auto branch = wireInto(scene, adder, 0);
    check(branch && branch->upstream.isJunction() && branch->upstream.wire.lock() == trunk, "the second wire branches off the first");
    // A wire from a junction to an input.
    check(scene.connectEnds(WireEnd::onWire(trunk, trunk->pointsAbsolute()[1]), in(adder, 1)).isEmpty(), "a wire from a wire to an input connects");
    const std::set<std::string> expected{"c1:0->g:0", "c1:0->sum:0", "c1:0->sum:1", "i16:0->mix:0"};
    check(connections(scene) == expected, "connections follow the wire trees");
    checkRoutes(scene, "after connecting");

    // Deleting the first wire of an output keeps its other connections.
    scene.deleteWire(trunk);
    check(connections(scene) == std::set<std::string>{"c1:0->sum:0", "c1:0->sum:1", "i16:0->mix:0"}, "deleting a trunk keeps its branches");
    check(wireInto(scene, adder, 0)->upstream.isOutput(), "a branch becomes the first wire of the output");
    checkRoutes(scene, "after deleting a trunk");
    undo.undo();
    check(connections(scene) == expected, "undo restores the deleted trunk");
    check(wireInto(scene, adder, 0)->upstream.isJunction(), "undo hangs the branch on the trunk again");
    checkRoutes(scene, "after undo");

    // Deleting an element removes its wires; the nets it drove go with it.
    scene.clearSelection();
    c1->setSelected(true);
    scene.deleteSelection();
    check(connections(scene) == std::set<std::string>{"i16:0->mix:0"}, "deleting the driver removes its nets");
    undo.undo();
    check(connections(scene) == expected, "undo restores the element and its nets");

    // Port changes: three Adder inputs, then two again (the wire on the third goes).
    auto three = adder->element();
    three.params["weights"] = boost::json::array{1, 1, 1};
    scene.changeElement(adder, three);
    check(adder->inputs().size() == 3, "the Adder shows three inputs");
    check(scene.connectEnds(out(c2), in(adder, 2)).isEmpty(), "the new input connects");
    checkRoutes(scene, "after adding an input");
    auto two = adder->element();
    two.params["weights"] = boost::json::array{1, 1};
    scene.changeElement(adder, two);
    check(adder->inputs().size() == 2 && !connections(scene).contains("c2:0->sum:2"), "removing an input removes its wire");
    checkRoutes(scene, "after removing an input");

    // Moving an element reroutes its wires.
    undo.push(new QSchematic::Commands::ItemMove({gain}, QVector2D(0, 300)));
    checkRoutes(scene, "after moving an element");
    undo.undo();
    checkRoutes(scene, "after undoing the move");

    // User bends are kept by the router and by the file.
    const auto bent = wireInto(scene, adder, 1);
    scene.setUserBends(bent, {QPointF(320, 160)});
    check(bent->userBends.size() == 1, "the bend is set");
    checkRoutes(scene, "with a user bend");
    const auto beforeSave = connections(scene);
    DiagramScene reloaded;
    const auto saved = reload(scene, reloaded);
    check(connections(reloaded) == beforeSave, "connections survive save and reload");
    check(wireInto(reloaded, reloaded.elementNode("sum"), 1)->userBends == QVector<QPointF>{QPointF(320, 160)}, "bends survive save and reload");
    checkRoutes(reloaded, "after reload");
    for (const auto& wire : scene.signalWires()) {
        const auto twin = wireInto(reloaded, reloaded.elementNode(wire->downstream.node.lock()->id()), wire->downstream.index);
        check(twin && twin->pointsAbsolute() == wire->pointsAbsolute(), "routes survive save and reload");
    }

    // Elements of the reloaded diagram keep their identity.
    check(reloaded.elementNode("sum") && reloaded.elementNode("sum")->element().type == "Adder", "element identity survives");

    // A layout that does not match its schema is refused.
    {
        auto document = DiagramDocument::fromJson(saved.toJson());
        auto& schemaConnections = document.runtimeSchema.as_object()["connections"].as_array();
        schemaConnections.pop_back();
        DiagramScene refused;
        bool threw = false;
        try {
            SceneSerializer{}.deserialize(document, refused);
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "a wire the schema does not have is a load error");
    }

    // A plain schema is laid out and wired automatically, fan-out as branches.
    {
        DiagramScene laidOut;
        DiagramDocument document;
        document.runtimeSchema = boost::json::parse(R"({
            "name": "fanout",
            "elements": [
                { "id": "src", "type": "Constant" },
                { "id": "a", "type": "Gain" }, { "id": "b", "type": "Gain" }, { "id": "c", "type": "Gain" }
            ],
            "connections": [
                { "from_element": "src", "from_port": 0, "to_element": "a", "to_port": 0 },
                { "from_element": "src", "from_port": 0, "to_element": "b", "to_port": 0 },
                { "from_element": "src", "from_port": 0, "to_element": "c", "to_port": 0 }
            ]
        })");
        SceneSerializer{}.deserialize(document, laidOut);
        check(connections(laidOut).size() == 3, "a plain schema is wired");
        int branches = 0;
        for (const auto& wire : laidOut.signalWires()) {
            branches += wire->upstream.isJunction() ? 1 : 0;
        }
        check(branches == 2, "fan-out of a plain schema is one trunk and two branches");
        checkRoutes(laidOut, "plain schema");
    }

    // Gestures: drag from a port, click by click, bend dragging.
    {
        DiagramScene gestures;
        const auto a = gestures.addElement(element("a", "Constant"), {40, 40});
        const auto b = gestures.addElement(element("b", "Gain"), {400, 40});
        const auto c = gestures.addElement(element("c", "Gain"), {400, 300});
        const auto port = [](const std::shared_ptr<ElementNode>& node, const PortDirection direction) {
            return node->port(direction, 0)->scenePos();
        };

        drag(gestures, port(a, PortDirection::Output), port(b, PortDirection::Input));
        check(connections(gestures) == std::set<std::string>{"a:0->b:0"}, "dragging from an output to an input connects");
        drag(gestures, port(b, PortDirection::Output), port(a, PortDirection::Output));
        check(connections(gestures).size() == 1, "dragging from an output to an output connects nothing");

        // A click on a port starts drawing; a click on empty canvas is a bend;
        // a click on a wire ends at a junction.
        click(gestures, port(c, PortDirection::Input));
        check(gestures.mode() == DiagramScene::DrawMode, "a click on a port starts drawing");
        click(gestures, QPointF(300, 400));
        const auto trunkRoute = wireInto(gestures, b, 0)->pointsAbsolute();
        const QPointF onTrunk = trunkRoute[0] + QPointF(2 * kGridStep, 0);
        click(gestures, onTrunk);
        check(connections(gestures) == std::set<std::string>{"a:0->b:0", "a:0->c:0"}, "drawing from an input to a wire connects");
        const auto drawn = wireInto(gestures, c, 0);
        check(drawn && drawn->upstream.isJunction() && drawn->userBends == QVector<QPointF>{QPointF(300, 400)},
              "the drawn wire starts at the junction and keeps the clicked bend");
        gestures.handleKey(Qt::Key_Escape, Qt::NoModifier);
        check(gestures.mode() == QSchematic::Scene::NormalMode, "Escape leaves drawing");
        checkRoutes(gestures, "after drawing");

        // Dragging a segment of a selected wire places bends; undo removes them.
        const auto wire = wireInto(gestures, b, 0);
        gestures.clearSelection();
        wire->setSelected(true);
        const auto route = wire->pointsAbsolute();
        const QPointF middle = (route[0] + route[1]) / 2.0;
        drag(gestures, middle, middle + QPointF(0, -2 * kGridStep));
        check(!wire->userBends.isEmpty(), "dragging a segment places bends");
        checkRoutes(gestures, "after dragging a segment");
        gestures.undoStack()->undo();
        check(wire->userBends.isEmpty(), "undo removes the dragged bends");
    }

#ifdef S7_TEST_NOISE_PLUGIN_DIR
    // Element types and plugins: a required plugin is searched for on load,
    // and what cannot be found is reported.
    {
        std::ostringstream quiet;
        s7::PluginLoader loader({S7_TEST_NOISE_PLUGIN_DIR}, quiet);
        auto parse = [](const char* text) { return boost::json::parse(text).as_object(); };
        const auto noise = parse(R"({"name": "n", "elements": [ { "id": "w", "type": "WhiteNoiseGenerator" }, { "id": "g", "type": "Gain" } ],
                                     "requires": [ { "plugin": "s7noise", "version": "1.0.0", "elements": ["WhiteNoiseGenerator"] } ]})");
        auto found = s7studio::ensureElementTypes(noise, loader, registry);
        check(found.errors.empty() && registry.find("WhiteNoiseGenerator"), "a required plugin is found in the search paths and loaded");
        const auto requirements = s7studio::pluginRequirements(noise, loader);
        check(requirements.size() == 1 && requirements[0].as_object().at("plugin") == "s7noise" &&
                  requirements[0].as_object().at("elements") == boost::json::array{"WhiteNoiseGenerator"},
              "requirements list the plugin and the element types taken from it (base elements are not listed)");
        const auto nested = parse(R"({"name": "n", "elements": [ { "id": "lib", "type": "MyLib",
                                      "schema": { "elements": [ { "id": "w", "type": "WhiteNoiseGenerator" } ] } } ]})");
        check(s7studio::pluginRequirements(nested, loader).size() == 1, "requirements include nested schemas");

        auto missingPlugin = s7studio::ensureElementTypes(
            parse(R"({"elements": [ { "id": "f", "type": "Fancy" } ], "requires": [ { "plugin": "s7missing", "elements": ["Fancy"] } ]})"),
            loader, registry);
        check(missingPlugin.errors.size() == 1 && missingPlugin.errors[0].contains("s7missing") && missingPlugin.elementProblems.contains("f"),
              "a plugin that cannot be found is reported for its elements");
        auto missingElement = s7studio::ensureElementTypes(
            parse(R"({"elements": [ { "id": "f", "type": "Fancy" } ], "requires": [ { "plugin": "s7noise", "elements": ["Fancy"] } ]})"),
            loader, registry);
        check(missingElement.errors.size() == 1 && missingElement.errors[0].contains("does not provide"),
              "an element type the plugin does not provide is reported");
        auto unknown = s7studio::ensureElementTypes(parse(R"({"elements": [ { "id": "x", "type": "Nope" } ]})"),
                                                    loader, registry);
        check(unknown.errors.size() == 1 && unknown.elementProblems.contains("x"),
              "an unknown type without a plugin is reported");
        auto version = s7studio::ensureElementTypes(
            parse(R"({"elements": [ { "id": "w", "type": "WhiteNoiseGenerator" } ],
                      "requires": [ { "plugin": "s7noise", "version": "0.0.1", "elements": ["WhiteNoiseGenerator"] } ]})"),
            loader, registry);
        check(version.errors.empty() && version.warnings.size() == 1, "another plugin version is a warning");
    }
#endif

    std::cout << (failures == 0 ? "diagram test: OK\n" : "diagram test: FAILED\n");
    return failures == 0 ? 0 : 1;
}
