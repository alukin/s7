#include "s7ui_json_dto.hpp"

#include <stdexcept>

namespace s7ui::dto {

auto ConnectorEndpoint::fromJson(const boost::json::value& value) -> ConnectorEndpoint
{
	const auto& obj = value.as_object();
	return ConnectorEndpoint{
		.nodeId = QString::fromStdString(boost::json::value_to<std::string>(obj.at("node"))),
		.connectorName = QString::fromStdString(boost::json::value_to<std::string>(obj.at("connector")))};
}

auto ConnectorEndpoint::toJson() const -> boost::json::value
{
	return boost::json::object{{"node", nodeId.toStdString()}, {"connector", connectorName.toStdString()}};
}

auto LoadedNodeSpec::fromJson(const boost::json::value& nodeJson) -> LoadedNodeSpec
{
	const auto& nodeObj = nodeJson.as_object();
	LoadedNodeSpec spec;
	spec.id = QString::fromStdString(jsonValueOr<std::string>(nodeObj, "id", std::string{}));
	spec.typeName = QString::fromStdString(jsonValueOr<std::string>(nodeObj, "type", std::string{"Constant"}));
	spec.name = QString::fromStdString(jsonValueOr<std::string>(nodeObj, "name", spec.typeName.toStdString()));
	if (const auto* inputPorts = nodeObj.if_contains("input_ports")) {
		spec.inputPorts = boost::json::value_to<int>(*inputPorts);
	} else if (const auto* inputs = nodeObj.if_contains("inputs")) {
		spec.inputPorts = boost::json::value_to<int>(*inputs);
	}
	if (const auto* outputPorts = nodeObj.if_contains("output_ports")) {
		spec.outputPorts = boost::json::value_to<int>(*outputPorts);
	} else if (const auto* outputs = nodeObj.if_contains("outputs")) {
		spec.outputPorts = boost::json::value_to<int>(*outputs);
	}
	const bool isVisualType = (spec.typeName == "ScopeSink" || spec.typeName == "ComplexScopeSink");
	spec.visualSink = jsonValueOr<bool>(nodeObj, "visual", isVisualType);
	if (const auto* position = nodeObj.if_contains("position")) {
		spec.position = JsonPoint::fromJson(*position);
	} else {
		spec.position = {jsonValueOr<double>(nodeObj, "x", 0.0), jsonValueOr<double>(nodeObj, "y", 0.0)};
	}
	if (const auto* params = nodeObj.if_contains("params")) {
		spec.params = *params;
	}
	return spec;
}

auto LoadedNodeSpec::toJson() const -> boost::json::value
{
	boost::json::object obj;
	obj["id"] = id.toStdString();
	obj["type"] = typeName.toStdString();
	obj["name"] = name.toStdString();
	obj["visual"] = visualSink;
	obj["position"] = JsonPoint::toJson(position);
	if (inputPorts >= 0) {
		obj["input_ports"] = inputPorts;
	}
	if (outputPorts >= 0) {
		obj["output_ports"] = outputPorts;
	}
	obj["params"] = params;
	return obj;
}

auto LoadedWireSpec::fromJson(const boost::json::value& wireJson) -> LoadedWireSpec
{
	const auto& wireObj = wireJson.as_object();
	LoadedWireSpec spec;
	spec.name = QString::fromStdString(jsonValueOr<std::string>(wireObj, "name", std::string{}));
	spec.from = ConnectorEndpoint::fromJson(wireObj.at("from"));
	spec.to = ConnectorEndpoint::fromJson(wireObj.at("to"));
	if (const auto* points = wireObj.if_contains("points")) {
		if (!points->is_array()) {
			throw std::runtime_error("Schema wire field 'points' must be an array");
		}
		for (const auto& pointJson : points->as_array()) {
			spec.points.push_back(JsonPoint::fromJson(pointJson));
		}
	}
	return spec;
}

auto LoadedWireSpec::toJson() const -> boost::json::value
{
	boost::json::object obj;
	obj["name"] = name.toStdString();
	obj["from"] = from.toJson();
	obj["to"] = to.toJson();
	boost::json::array pts;
	for (const auto& p : points) {
		pts.push_back(JsonPoint::toJson(p));
	}
	obj["points"] = std::move(pts);
	return obj;
}

auto VisualSpecDto::fromJson(const boost::json::value& root) -> visual::VisualSpec
{
	if (!root.is_object()) {
		throw std::runtime_error("visual spec root must be object");
	}

	const auto& obj = root.as_object();
	const auto* typeVal = obj.if_contains("type");
	if (!typeVal || !typeVal->is_string()) {
		throw std::runtime_error("missing string field 'type'");
	}

	visual::VisualSpec spec;
	spec.type = QString::fromStdString(boost::json::value_to<std::string>(*typeVal));
	spec.displayName = spec.type;
	if (const auto* v = obj.if_contains("display_name"); v && v->is_string()) {
		spec.displayName = QString::fromStdString(boost::json::value_to<std::string>(*v));
	}
	if (const auto* v = obj.if_contains("category"); v && v->is_string()) {
		spec.category = QString::fromStdString(boost::json::value_to<std::string>(*v));
	}
	if (const auto* v = obj.if_contains("shape"); v && v->is_string()) {
		spec.shape = QString::fromStdString(boost::json::value_to<std::string>(*v));
	}
	if (const auto* v = obj.if_contains("description"); v && v->is_string()) {
		spec.description = QString::fromStdString(boost::json::value_to<std::string>(*v));
	}
	if (const auto* v = obj.if_contains("size"); v && v->is_object()) {
		const auto& sizeObj = v->as_object();
		spec.size = QSizeF{jsonValueOr<double>(sizeObj, "width", 0.0), jsonValueOr<double>(sizeObj, "height", 0.0)};
	}
	if (const auto* v = obj.if_contains("preview_area"); v && v->is_object()) {
		spec.previewArea = JsonRect::fromJson(*v);
	}
	if (const auto* styleVal = obj.if_contains("node_style"); styleVal && styleVal->is_object()) {
		const auto& styleObj = styleVal->as_object();
		if (const auto* colorVal = styleObj.if_contains("color"); colorVal && colorVal->is_string()) {
			spec.color = QString::fromStdString(boost::json::value_to<std::string>(*colorVal));
		}
	}
	return spec;
}

auto JsonPoint::toJson(const QPointF& point) -> boost::json::value
{
	return boost::json::object{{"x", point.x()}, {"y", point.y()}};
}

auto JsonPoint::fromJson(const boost::json::value& value) -> QPointF
{
	if (value.is_array() && value.as_array().size() >= 2) {
		const auto& arr = value.as_array();
		return {boost::json::value_to<double>(arr.at(0)), boost::json::value_to<double>(arr.at(1))};
	}
	if (value.is_object()) {
		const auto& obj = value.as_object();
		return {jsonValueOr<double>(obj, "x", 0.0), jsonValueOr<double>(obj, "y", 0.0)};
	}
	throw std::runtime_error("Point value must be an array [x, y] or an object with x/y fields");
}

auto JsonRect::toJson(const QRectF& rect) -> boost::json::value
{
	return boost::json::object{{"x", rect.x()}, {"y", rect.y()}, {"width", rect.width()}, {"height", rect.height()}};
}

auto JsonRect::fromJson(const boost::json::value& value) -> QRectF
{
	if (value.is_array() && value.as_array().size() >= 4) {
		const auto& arr = value.as_array();
		return {boost::json::value_to<double>(arr.at(0)),
		        boost::json::value_to<double>(arr.at(1)),
		        boost::json::value_to<double>(arr.at(2)),
		        boost::json::value_to<double>(arr.at(3))};
	}
	if (value.is_object()) {
		const auto& obj = value.as_object();
		return {jsonValueOr<double>(obj, "x", 0.0),
		        jsonValueOr<double>(obj, "y", 0.0),
		        jsonValueOr<double>(obj, "width", 0.0),
		        jsonValueOr<double>(obj, "height", 0.0)};
	}
	throw std::runtime_error("Rect value must be an array [x, y, width, height] or an object with x/y/width/height fields");
}

auto JsonParamsObject::validateObject(const QString& text) -> void
{
	const auto parsed = boost::json::parse(text.toStdString());
	if (!parsed.is_object()) {
		throw std::runtime_error("Parameters JSON must be an object");
	}
}

auto CompilerPortSpecDto::fromJson(const boost::json::value& value) -> CompilerPortSpecDto
{
	if (!value.is_object()) {
		throw std::runtime_error("Port spec must be an object");
	}
	const auto& obj = value.as_object();
	const auto* name = obj.if_contains("name");
	const auto* direction = obj.if_contains("direction");
	if (!name || !name->is_string() || !direction || !direction->is_string()) {
		throw std::runtime_error("Port spec must include string fields 'name' and 'direction'");
	}

	CompilerPortSpecDto spec;
	spec.name = boost::json::value_to<std::string>(*name);
	spec.direction = boost::json::value_to<std::string>(*direction);
	if (const auto* st = obj.if_contains("signal_type"); st && st->is_string()) {
		spec.signalType = boost::json::value_to<std::string>(*st);
	}
	if (const auto* dm = obj.if_contains("domain"); dm && dm->is_string()) {
		spec.domain = boost::json::value_to<std::string>(*dm);
	}
	if (const auto* mc = obj.if_contains("max_connections"); mc && mc->is_int64()) {
		spec.maxConnections = static_cast<int>(mc->as_int64());
	} else if (const auto* mc = obj.if_contains("max_connections"); mc && mc->is_uint64()) {
		spec.maxConnections = static_cast<int>(mc->as_uint64());
	} else {
		spec.maxConnections = spec.direction == "input" ? 1 : 1024;
	}
	return spec;
}

auto CompilerNodeDto::fromJson(const boost::json::value& value) -> CompilerNodeDto
{
	if (!value.is_object()) {
		throw std::runtime_error("Node entry must be an object");
	}
	const auto& obj = value.as_object();

	CompilerNodeDto node;
	if (const auto* id = obj.if_contains("id"); id && id->is_string()) {
		node.id = boost::json::value_to<std::string>(*id);
	}
	if (const auto* type = obj.if_contains("type"); type && type->is_string()) {
		node.type = boost::json::value_to<std::string>(*type);
	}
	if (const auto* visual = obj.if_contains("visual"); visual && visual->is_bool()) {
		node.visual = visual->as_bool();
	}
	if (const auto* in = obj.if_contains("input_ports"); in && in->is_int64()) {
		node.inputPorts = static_cast<int>(in->as_int64());
	} else if (const auto* in = obj.if_contains("input_ports"); in && in->is_uint64()) {
		node.inputPorts = static_cast<int>(in->as_uint64());
	}
	if (const auto* out = obj.if_contains("output_ports"); out && out->is_int64()) {
		node.outputPorts = static_cast<int>(out->as_int64());
	} else if (const auto* out = obj.if_contains("output_ports"); out && out->is_uint64()) {
		node.outputPorts = static_cast<int>(out->as_uint64());
	}

	if (const auto* portsVal = obj.if_contains("ports"); portsVal && portsVal->is_array()) {
		for (const auto& p : portsVal->as_array()) {
			node.ports.push_back(CompilerPortSpecDto::fromJson(p));
		}
	}
	return node;
}

auto CompilerEndpointDto::fromJson(const boost::json::value& value) -> CompilerEndpointDto
{
	if (!value.is_object()) {
		throw std::runtime_error("Endpoint must be an object");
	}
	const auto& obj = value.as_object();
	const auto* node = obj.if_contains("node");
	const auto* connector = obj.if_contains("connector");
	if (!node || !node->is_string() || !connector || !connector->is_string()) {
		throw std::runtime_error("Endpoint must include string fields 'node' and 'connector'");
	}
	return CompilerEndpointDto{
		.node = boost::json::value_to<std::string>(*node),
		.connector = boost::json::value_to<std::string>(*connector)};
}

auto CompilerWireDto::fromJson(const boost::json::value& value) -> CompilerWireDto
{
	if (!value.is_object()) {
		throw std::runtime_error("Wire must be an object");
	}
	const auto& obj = value.as_object();
	const auto* from = obj.if_contains("from");
	const auto* to = obj.if_contains("to");
	if (!from || !to) {
		throw std::runtime_error("Wire must contain fields 'from' and 'to'");
	}
	return CompilerWireDto{.from = CompilerEndpointDto::fromJson(*from), .to = CompilerEndpointDto::fromJson(*to)};
}

auto CompilerNetDto::fromJson(const boost::json::value& value) -> CompilerNetDto
{
	if (!value.is_object()) {
		throw std::runtime_error("Net entry must be an object");
	}
	const auto* conns = value.as_object().if_contains("connections");
	if (!conns || !conns->is_array()) {
		throw std::runtime_error("Net must contain array field 'connections'");
	}
	CompilerNetDto out;
	for (const auto& endpoint : conns->as_array()) {
		out.connections.push_back(CompilerEndpointDto::fromJson(endpoint));
	}
	return out;
}

auto CompilerRuntimeGraphDto::fromJson(const boost::json::value& value) -> CompilerRuntimeGraphDto
{
	if (!value.is_object()) {
		throw std::runtime_error("Runtime graph root must be an object");
	}
	const auto& obj = value.as_object();
	const auto* nodes = obj.if_contains("nodes");
	const auto* nets = obj.if_contains("nets");
	if (!nodes || !nodes->is_array()) {
		throw std::runtime_error("Runtime graph must contain array field 'nodes'");
	}
	if (!nets || !nets->is_array()) {
		throw std::runtime_error("Runtime graph must contain array field 'nets'");
	}

	CompilerRuntimeGraphDto out;
	for (const auto& node : nodes->as_array()) {
		out.nodes.push_back(CompilerNodeDto::fromJson(node));
	}
	for (const auto& net : nets->as_array()) {
		out.nets.push_back(CompilerNetDto::fromJson(net));
	}
	return out;
}

auto CompilerDiagramDto::fromJson(const boost::json::value& value) -> CompilerDiagramDto
{
	if (!value.is_object()) {
		throw std::runtime_error("Diagram document root must be an object");
	}

	const auto& doc = value.as_object();
	const auto* diagramVal = doc.if_contains("diagram");
	const auto* root = (diagramVal && diagramVal->is_object()) ? &diagramVal->as_object() : &doc;
	const auto* nodes = root->if_contains("nodes");
	const auto* wires = root->if_contains("wires");
	if (!nodes || !nodes->is_array()) {
		throw std::runtime_error("Diagram must contain array field 'nodes'");
	}

	CompilerDiagramDto out;
	for (const auto& node : nodes->as_array()) {
		out.nodes.push_back(CompilerNodeDto::fromJson(node));
	}
	if (wires && wires->is_array()) {
		for (const auto& wire : wires->as_array()) {
			out.wires.push_back(CompilerWireDto::fromJson(wire));
		}
	}
	return out;
}

} // namespace s7ui::dto



