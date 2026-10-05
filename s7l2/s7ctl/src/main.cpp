// s7ctl: command-line control of S7 runtime nodes.
//
//   s7ctl deploy <diagram> [tls] [--deployment-id <id>] [--write <file>]
//   s7ctl config <url> [tls]                 GET_NETWORK_CONFIGURATION
//   s7ctl streams <url> [tls]                GET_STREAM_STATUS
//   s7ctl recover <url> <stream_id> [tls]    RECOVER_STREAM
//
//   tls: --cert <pem> --key <pem> --ca <pem> (the Studio certificate for
//   deploy and recover; required for wss:// URLs)
#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "s7/builtin_element_registry.hpp"
#include "s7/canonical_json.hpp"
#include "s7/deployment_client.hpp"
#include "s7/net.hpp"

namespace {

namespace json = boost::json;

void usage(std::ostream& out)
{
    out << "Usage:\n"
           "  s7ctl deploy <diagram> [tls] [--deployment-id <id>] [--write <file>]\n"
           "  s7ctl config <url> [tls]\n"
           "  s7ctl streams <url> [tls]\n"
           "  s7ctl recover <url> <stream_id> [tls]\n"
           "tls: --cert <pem> --key <pem> --ca <pem>\n";
}

struct Arguments {
    std::vector<std::string> positional;
    s7::transport::TlsConfig tls;
    std::string deployment_id;
    std::string write_to;
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
    if (command == "deploy") {
        return deploy(args);
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
