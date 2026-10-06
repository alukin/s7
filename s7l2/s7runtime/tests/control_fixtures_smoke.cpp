// Runs the control protocol conformance script (tools/gen_control_fixtures.py,
// s7runtime/tests/fixtures/control_v1.json) against the C++ control plane:
// every response must have its type, its members with their JSON types, and
// the exact values the script gives.
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <s7/canonical_json.hpp>
#include <s7/control_plane_mvp.hpp>
#include <s7/transport.hpp>

namespace {

namespace json = boost::json;

// Accepts every configuration (the script checks the protocol, not graphs).
class AcceptingHost final : public s7::DeploymentHost {
public:
    auto stage(const s7::NodePart&) -> std::unique_ptr<s7::StagedConfiguration> override
    {
        return std::make_unique<s7::StagedConfiguration>();
    }
    void activate(s7::StagedConfiguration&) override {}
    auto execution_state() const -> std::string override { return "not_stepping"; }
};

// The member at a path such as "a.b", "a[0].b"; nothing if absent.
auto member(const json::value& root, const std::string& path) -> std::optional<json::value>
{
    const json::value* at = &root;
    std::size_t position = 0;
    while (position < path.size()) {
        if (path[position] == '.') {
            ++position;
        }
        if (path[position] == '[') {
            const auto close = path.find(']', position);
            const auto index = std::stoul(path.substr(position + 1, close - position - 1));
            if (!at->is_array() || index >= at->as_array().size()) {
                return std::nullopt;
            }
            at = &at->as_array()[index];
            position = close + 1;
            continue;
        }
        const auto end = path.find_first_of(".[", position);
        const auto key = path.substr(position, end == std::string::npos ? std::string::npos : end - position);
        if (!at->is_object() || !at->as_object().contains(key)) {
            return std::nullopt;
        }
        at = &at->as_object().at(key);
        position = end == std::string::npos ? path.size() : end;
    }
    return *at;
}

auto json_type(const json::value& value) -> std::string
{
    switch (value.kind()) {
        case json::kind::string: return "string";
        case json::kind::int64:
        case json::kind::uint64: return "integer";
        case json::kind::double_: return "number";
        case json::kind::bool_: return "boolean";
        case json::kind::object: return "object";
        case json::kind::array: return "array";
        case json::kind::null: return "null";
    }
    return "?";
}

// Replaces "${name}" strings with captured values.
auto substitute(json::value value, const std::map<std::string, json::value>& captured) -> json::value
{
    if (value.is_string()) {
        const std::string text(value.as_string());
        if (text.starts_with("${") && text.ends_with("}")) {
            return captured.at(text.substr(2, text.size() - 3));
        }
    } else if (value.is_object()) {
        for (auto& [key, item] : value.as_object()) {
            item = substitute(item, captured);
        }
    } else if (value.is_array()) {
        for (auto& item : value.as_array()) {
            item = substitute(item, captured);
        }
    }
    return value;
}

} // namespace

int main()
try {
    std::ifstream in(S7_CONTROL_FIXTURES);
    const auto fixtures = s7::parse_json_precise(std::string(std::istreambuf_iterator<char>(in), {})).as_object();
    const auto& node = fixtures.at("node").as_object();

    // The fixture node's stream: a consumer that faulted with UNDERRUN.
    auto network = std::make_shared<s7::transport::InMemoryRuntimeNetEndpoint>();
    s7::stream::StreamConfig stream;
    stream.stream_id = s7::wire::parse_stream_id(std::string(node.at("stream").as_object().at("stream_id").as_string()));
    stream.max_wait = std::chrono::milliseconds(1);
    double value = 0;
    static_cast<void>(network->receive(network->open_consumer(stream), &value, 1));

    const auto studio = s7::transport::inspect_certificate_file(std::string(S7_TEST_CERT_DIR) + "/s7_studio.cert.pem");
    s7::ControlPlaneMvp control{
        s7::ControlNodeDescriptor{.node_uuid = std::string(node.at("node_uuid").as_string()),
                                  .node_name = std::string(node.at("node_name").as_string()),
                                  .listen_url = std::string(node.at("listen_url").as_string())},
        s7::ControlPlaneOptions{.authorization = {.studio_key_purpose_oid = "2.25.4266950079767543379770189370854073200",
                                                  .studio_certificate_sha256 = studio.fingerprint_sha256},
                                .deployment_host = std::make_shared<AcceptingHost>(),
                                .network = network,
                                .max_configuration_bytes = node.at("max_configuration_bytes").to_number<std::size_t>()}};
    const std::map<std::string, s7::transport::ControlPeer> peers{
        {"anonymous", {}},
        {"studio", {.tls = true, .certificate = studio}},
    };

    std::map<std::string, json::value> captured;
    int failures = 0;
    int checked = 0;
    for (const auto& entry : fixtures.at("steps").as_array()) {
        const auto& step = entry.as_object();
        const auto name = std::string(step.at("name").as_string());
        const auto request = substitute(step.at("request"), captured);
        const auto text = control.handle_request_json(peers.at(std::string(step.at("peer").as_string())), json::serialize(request));
        const auto response = s7::parse_json_precise(text);
        const auto& expected = step.at("response").as_object();
        std::string problems;
        if (response.as_object().at("type") != expected.at("type")) {
            problems += " type";
        }
        for (const auto& [path, type] : expected.at("fields").as_object()) {
            const auto found = member(response, path);
            const std::string allowed(type.as_string());
            if (!found) {
                problems += " missing " + std::string(path);
            } else if (("|" + allowed + "|").find("|" + json_type(*found) + "|") == std::string::npos) {
                problems += " " + std::string(path) + " is " + json_type(*found) + " not " + allowed;
            }
        }
        if (const auto* equals = expected.if_contains("equals")) {
            for (const auto& [path, wanted] : equals->as_object()) {
                const auto found = member(response, path);
                if (!found || *found != substitute(wanted, captured)) {
                    problems += " " + std::string(path) + " != " + json::serialize(wanted);
                }
            }
        }
        if (const auto* capture = step.if_contains("capture")) {
            for (const auto& [key, path] : capture->as_object()) {
                if (const auto found = member(response, std::string(path.as_string()))) {
                    captured[key] = *found;
                }
            }
        }
        if (!problems.empty() || std::getenv("S7_CONTROL_FIXTURE_DUMP")) {
            std::cout << (problems.empty() ? "ok   " : "FAIL ") << name << ":" << problems << "\n     " << text << '\n';
        }
        failures += problems.empty() ? 0 : 1;
        ++checked;
    }
    std::cout << checked - failures << " of " << checked << " control steps pass\n";
    return failures == 0 ? 0 : 1;
} catch (const std::exception& ex) {
    std::cerr << "s7runtime control fixtures: " << ex.what() << '\n';
    return 1;
}
