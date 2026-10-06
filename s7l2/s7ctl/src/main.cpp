// s7ctl: command-line control of S7 runtime nodes.
//
//   s7ctl deploy <diagram> [tls] [elements] [--deployment-id <id>] [--write <file>]
//   s7ctl check <diagram> [elements] [--write <file>]   streams and node parts, offline
//   s7ctl info <url> [tls]                   GET_NODE_INFO (identity, clock, pacing)
//   s7ctl config <url> [tls]                 GET_NETWORK_CONFIGURATION
//   s7ctl streams <url> [tls]                GET_STREAM_STATUS
//   s7ctl recover <url> <stream_id> [tls]    RECOVER_STREAM
//
//   tls: --cert <pem> --key <pem> --ca <pem> (the Studio certificate for
//   deploy and recover; required for wss:// URLs)
//   elements: --plugin <file>, --plugin-dir <dir>, --library <file> (each
//   repeatable): element types beyond the built-in ones, needed to infer the
//   diagram's stream types. Plugins are also found where s7runtime looks
//   (~/.s7/plugins, /opt/s7/plugins, ./plugins, $S7_PLUGIN_PATH).
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "s7/builtin_element_registry.hpp"
#include "s7/canonical_json.hpp"
#include "s7/deployment_client.hpp"
#include "s7/net.hpp"
#include "s7/plugin_loader.hpp"
#include "s7/schema_library.hpp"

namespace {

namespace json = boost::json;

void usage(std::ostream& out)
{
    out << "Usage:\n"
           "  s7ctl deploy <diagram> [tls] [elements] [--deployment-id <id>] [--write <file>]\n"
           "  s7ctl check <diagram> [elements] [--write <file>]\n"
           "  s7ctl info <url> [tls]\n"
           "  s7ctl config <url> [tls]\n"
           "  s7ctl streams <url> [tls]\n"
           "  s7ctl recover <url> <stream_id> [tls]\n"
           "tls: --cert <pem> --key <pem> --ca <pem>\n"
           "elements: --plugin <file> --plugin-dir <dir> --library <file> (repeatable)\n";
}

struct Arguments {
    std::vector<std::string> positional;
    s7::transport::TlsConfig tls;
    std::string deployment_id;
    std::string write_to;
    std::vector<std::filesystem::path> plugins;
    std::vector<std::filesystem::path> plugin_dirs;
    std::vector<std::filesystem::path> libraries;
};

auto parse_arguments(const int argc, char** argv) -> Arguments
{
    Arguments args;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::invalid_argument(arg + " needs a value.");
            }
            return argv[++i];
        };
        if (arg == "--cert") {
            args.tls.cert_file = value();
        } else if (arg == "--key") {
            args.tls.key_file = value();
        } else if (arg == "--ca") {
            args.tls.ca_file = value();
        } else if (arg == "--deployment-id") {
            args.deployment_id = value();
        } else if (arg == "--write") {
            args.write_to = value();
        } else if (arg == "--plugin") {
            args.plugins.emplace_back(value());
        } else if (arg == "--plugin-dir") {
            args.plugin_dirs.emplace_back(value());
        } else if (arg == "--library") {
            args.libraries.emplace_back(value());
        } else if (arg.starts_with("--")) {
            throw std::invalid_argument("Unknown option " + arg + ".");
        } else {
            args.positional.push_back(arg);
        }
    }
    args.tls.enabled = !args.tls.cert_file.empty();
    return args;
}

auto read_json_file(const std::string& path) -> json::object
{
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot read " + path + ".");
    }
    std::stringstream text;
    text << in.rdbuf();
    const auto value = s7::parse_json_precise(text.str());
    if (!value.is_object()) {
        throw std::invalid_argument(path + " is not a JSON object.");
    }
    return value.as_object();
}

auto request(const std::string& url, const s7::transport::TlsConfig& tls, json::object message) -> json::object
{
    message["request_id"] = "s7ctl-1";
    message["protocol_version"] = 1;
    s7::deployment::BeastControlRequester requester(tls);
    return requester(s7::deployment::RuntimeNode{{}, {}, url}, message);
}

int print(const json::object& response)
{
    std::cout << json::serialize(response) << '\n';
    return response.contains("type") && response.at("type") == "ERROR" ? 1 : 0;
}

// The element types the diagram may use beyond the built-in ones, loaded
// like s7runtime loads them; progress goes to stderr.
void load_element_types(const Arguments& args, s7::PluginLoader& loader)
{
    auto& registry = s7::ElementRegistry::instance();
    loader.load_plugins(registry);
    for (const auto& plugin : args.plugins) {
        if (!loader.load_plugin(plugin, registry)) {
            throw std::runtime_error("Cannot load plugin " + plugin.string() + ".");
        }
    }
    for (const auto& library : args.libraries) {
        s7::SchemaLibraryLoader::register_library_file(library, registry);
    }
}

// Assigns the diagram's streams and extracts each node's part without
// contacting any node: what deploy would send.
int check(const Arguments& args)
{
    if (args.positional.size() != 1) {
        throw std::invalid_argument("check needs one diagram file.");
    }
    auto diagram = read_json_file(args.positional[0]);
    const auto errors = s7::deployment::assign_streams(diagram, s7::deployment::infer_output_types(diagram));
    for (const auto& error : errors) {
        std::cerr << "error: " << error << '\n';
    }
    if (!errors.empty()) {
        return 1;
    }
    for (const auto& stream : s7::deployment::parse_streams(diagram)) {
        std::cout << "stream " << s7::wire::to_string(stream.stream_id) << " " << s7::signal_type_name(stream.data_type) << " "
                  << stream.producer.element << ":" << stream.producer.port << " (" << stream.producer.node_uuid << ") ->";
        for (const auto& consumer : stream.consumers) {
            std::cout << " " << consumer.element << ":" << consumer.port << " (" << consumer.node_uuid << ")";
        }
        std::cout << '\n';
    }
    for (const auto& node : s7::deployment::placed_nodes(diagram)) {
        const auto part = s7::deployment::extract_node_schema(diagram, node);
        std::cout << "node " << node << " " << part.at("elements").as_array().size() << " elements\n";
    }
    if (!args.write_to.empty()) {
        std::ofstream(args.write_to) << json::serialize(diagram) << '\n';
    }
    return 0;
}

int deploy(const Arguments& args)
{
    if (args.positional.size() != 1) {
        throw std::invalid_argument("deploy needs one diagram file.");
    }
    auto diagram = read_json_file(args.positional[0]);
    const auto types = s7::deployment::infer_output_types(diagram);
    s7::deployment::BeastControlRequester requester(args.tls);
    const auto result = s7::deployment::deploy_diagram(
        diagram, types, [&](const auto& node, const auto& message) { return requester(node, message); },
        s7::deployment::DeployOptions{.deployment_id = args.deployment_id, .project_revision = {}});
    for (const auto& node : result.nodes) {
        std::cout << "node " << node.node_uuid << " " << node.endpoint << " " << node.status
                  << (node.message.empty() ? "" : ": " + node.message) << '\n';
    }
    for (const auto& error : result.errors) {
        std::cerr << "error: " << error << '\n';
    }
    if (!args.write_to.empty() && !result.diagram.empty()) {
        // The diagram with its streams, so the next deployment continues them.
        std::ofstream(args.write_to) << json::serialize(result.diagram) << '\n';
    }
    if (result.committed) {
        std::cout << "deployed " << result.deployment_id << " revision " << result.revision << " " << result.configuration_digest
                  << '\n';
        if (result.activate_at_ns) {
            std::cout << "activate_at_ns " << *result.activate_at_ns << '\n';
        }
    }
    return result.committed ? 0 : 1;
}

} // namespace

int main(const int argc, char** argv)
try {
    if (argc < 2 || std::string(argv[1]) == "help" || std::string(argv[1]) == "--help") {
        usage(std::cout);
        return argc < 2 ? 2 : 0;
    }
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);

    const std::string command = argv[1];
    const auto args = parse_arguments(argc, argv);
    s7::PluginLoader loader(args.plugin_dirs, std::cerr);
    if (command == "deploy" || command == "check") {
        load_element_types(args, loader);
        return command == "deploy" ? deploy(args) : check(args);
    }
    if (command == "info" && args.positional.size() == 1) {
        return print(request(args.positional[0], args.tls, json::object{{"type", "GET_NODE_INFO"}}));
    }
    if (command == "config" && args.positional.size() == 1) {
        return print(request(args.positional[0], args.tls, json::object{{"type", "GET_NETWORK_CONFIGURATION"}}));
    }
    if (command == "streams" && args.positional.size() == 1) {
        return print(request(args.positional[0], args.tls, json::object{{"type", "GET_STREAM_STATUS"}}));
    }
    if (command == "recover" && args.positional.size() == 2) {
        return print(request(args.positional[0], args.tls,
                             json::object{{"type", "RECOVER_STREAM"}, {"stream_id", args.positional[1]}}));
    }
    usage(std::cerr);
    return 2;
} catch (const std::exception& ex) {
    std::cerr << "s7ctl: " << ex.what() << '\n';
    return 1;
}
