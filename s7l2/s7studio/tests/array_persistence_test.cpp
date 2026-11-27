// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-S7-plugin-permission

#include "diagram/diagram_scene.hpp"
#include "diagram_document.hpp"
#include "scene_serializer.hpp"
#include "wire_type_labels.hpp"

#include <s7/builtin_element_registry.hpp>
#include <s7/schema_library.hpp>

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
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
}
} // namespace

int main(int argc, char** argv) try {
    QApplication application(argc, argv);
    check_save_open();
    check_rejected_metadata();
    check_wiring();
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
