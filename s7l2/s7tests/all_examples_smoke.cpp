// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Every general example diagram (examples/**.s7diagram) loads through the
// production schema loader, validates, compiles and steps headlessly for up
// to its saved run length (at most 2000 steps). Examples that use element
// types from optional plugins (spectral analysis, noise, video, sound, SDR)
// are skipped and listed: they are covered by their plugin tests and the
// bundled-example GUI shards. No hardware is opened: hardware elements live
// in plugins this test does not load.

#include <s7/builtin_element_registry.hpp>
#include <s7/compiler.hpp>
#include <s7/net.hpp>
#include <s7/rate.hpp>
#include <s7/runtime.hpp>
#include <s7/schema_library.hpp>
#include <s7/validation.hpp>

#include <boost/json.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

boost::json::object read_document(const fs::path& path)
{
    std::ifstream stream(path);
    std::stringstream text;
    text << stream.rdbuf();
    return boost::json::parse(text.str()).as_object();
}

// Element types the document needs that the registry does not know.
void unknown_types(const boost::json::value& schema, std::set<std::string>& missing)
{
    if (!schema.is_object()) return;
    const auto* elements = schema.as_object().if_contains("elements");
    if (!elements || !elements->is_array()) return;
    for (const auto& value : elements->as_array()) {
        const auto& element = value.as_object();
        if (const auto* nested = element.if_contains("schema")) {
            unknown_types(*nested, missing);
            continue;
        }
        const auto type = std::string(element.at("type").as_string());
        if (!s7::ElementRegistry::instance().find(type)) missing.insert(type);
    }
}

} // namespace

int main()
{
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    s7::register_net_elements(registry);

    std::vector<fs::path> diagrams;
    for (const auto& entry : fs::recursive_directory_iterator(S7_EXAMPLES_DIR)) {
        if (entry.is_regular_file() && entry.path().extension() == ".s7diagram") diagrams.push_back(entry.path());
    }
    std::ranges::sort(diagrams);

    std::size_t ran = 0;
    std::vector<std::string> failures;
    for (const auto& path : diagrams) {
        const auto name = fs::relative(path, S7_EXAMPLES_DIR).generic_string();
        try {
            const auto document = read_document(path);
            std::set<std::string> missing;
            unknown_types(document.at("schema"), missing);
            if (!missing.empty()) {
                std::string list;
                for (const auto& type : missing) list += (list.empty() ? "" : ", ") + type;
                std::cout << "skipped " << name << " (plugin types: " << list << ")\n";
                continue;
            }
            const double dt = document.at("dt").to_number<double>();
            auto schema = s7::SchemaLibraryLoader::build_schema(document.at("schema"));
            if (!schema.rate()) schema.set_rate(s7::Rate::from_seconds(dt));
            const auto validation = s7::Validator::validate(schema);
            if (!validation.ok()) {
                std::string message = name + ": validation failed";
                for (const auto& error : validation.errors) message += "\n   - " + error;
                failures.push_back(message);
                continue;
            }
            s7::NodeRuntime runtime(s7::Compiler::compile(schema));
            runtime.set_model_time_rate(s7::Rate::from_seconds(dt));
            const auto* run = document.at("studio_run").as_object().if_contains("runtime_seconds");
            const double seconds = run ? run->to_number<double>() : 0.0;
            const auto steps = seconds > 0 ? std::min<std::size_t>(2000, static_cast<std::size_t>(std::ceil(seconds / dt))) : 2000;
            for (std::size_t step = 0; step < steps; ++step) runtime.step();
            ++ran;
        } catch (const std::exception& error) {
            failures.push_back(name + ": " + error.what());
        }
    }

    std::cout << ran << " of " << diagrams.size() << " example diagrams ran\n";
    for (const auto& failure : failures) std::cerr << "FAILED " << failure << '\n';
    if (diagrams.size() < 90) {
        std::cerr << "expected the bundled examples in " << S7_EXAMPLES_DIR << '\n';
        return 1;
    }
    return failures.empty() ? 0 : 1;
}
