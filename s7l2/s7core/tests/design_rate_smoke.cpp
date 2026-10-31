// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Rate-designed elements (doc/S7_Sample_Rate_Model.md section 8.2): FIR and
// IIR coefficients record the rate they were designed for. At that rate they
// run; at another rate they are refused, because raw coefficients cannot be
// recalculated. Without a recorded rate they run as given. Studio records the
// rate from a document's saved dt (record_design_rates), never from a run's.

#include <s7/builtin_element_registry.hpp>
#include <s7/core.hpp>
#include <s7/element_registry.hpp>
#include <s7/schema_library.hpp>

#include <boost/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string errors(const s7::Schema& schema) {
    std::string text;
    for (const auto& error : s7::Validator::validate(schema).errors) text += error + "\n";
    return text;
}

s7::Schema filter_graph(const char* type, boost::json::object params, double run_hz) {
    auto& registry = s7::ElementRegistry::instance();
    s7::Schema schema("filtered");
    schema.set_rate(s7::Rate::from_hz(run_hz));
    schema.add_element(s7::make_element_spec("source", *registry.find("Constant"), {{"value", 1.0}}));
    schema.add_element(s7::make_element_spec("filter", *registry.find(type), std::move(params)));
    schema.connect("source", 0, "filter", 0);
    return schema;
}

void filters_check_their_design_rate() {
    for (const auto& [type, coefficients] : {std::pair{"FirFilter", boost::json::object{{"taps", boost::json::array{0.5, 0.5}}}},
                                              std::pair{"IirFilter", boost::json::object{{"b0", 0.2}, {"b1", 0.2}, {"a1", -0.6}}}}) {
        auto designed = coefficients;
        designed["design_rate_hz"] = 1000;
        require(errors(filter_graph(type, designed, 1000)).empty(), std::string(type) + " at its design rate");
        const auto refused = errors(filter_graph(type, designed, 2000));
        require(refused.find("'filter'") != std::string::npos && refused.find("cannot be recalculated") != std::string::npos &&
                    refused.find("Resampler") != std::string::npos,
                std::string(type) + " at another rate is refused: " + refused);
        require(errors(filter_graph(type, coefficients, 2000)).empty(),
                std::string(type) + " without a recorded design rate runs as given");
    }
    // The design rate is checked, not pinned: it does not choose the run rate.
    s7::Schema unseeded("unseeded");
    auto& registry = s7::ElementRegistry::instance();
    unseeded.add_element(s7::make_element_spec("filter", *registry.find("FirFilter"),
                                               {{"taps", boost::json::array{1.0}}, {"design_rate_hz", 1000}}));
    unseeded.add_element(s7::make_element_spec("source", *registry.find("Constant"), {{"value", 1.0}}));
    unseeded.connect("source", 0, "filter", 0);
    require(errors(unseeded).find("cannot be determined") != std::string::npos, "A design rate is not a rate pin");
}

void documents_record_their_design_rate() {
    auto document = boost::json::parse(R"({"name":"doc","dt":0.001,"elements":[
        {"id":"fir","type":"FirFilter","params":{"taps":[0.5,0.5]}},
        {"id":"kept","type":"IirFilter","params":{"design_rate_hz":48000}},
        {"id":"gain","type":"Gain","params":{}},
        {"id":"nested","type":"Schema","schema":{"name":"inner","dt":0.0005,
            "elements":[{"id":"inner_fir","type":"FirFilter","params":{"taps":[1.0]}}],"connections":[]}}],
        "connections":[]})");
    require(s7::record_design_rates(document, 0.001), "Missing design rates recorded");
    const auto& elements = document.at("elements").as_array();
    require(elements[0].at("params").at("design_rate_hz").to_number<double>() == 1000, "FIR records the document rate");
    require(elements[1].at("params").at("design_rate_hz").to_number<double>() == 48000, "A recorded rate is kept");
    require(!elements[2].at("params").as_object().contains("design_rate_hz"), "Other elements are untouched");
    require(elements[3].at("schema").at("elements").as_array()[0].at("params").at("design_rate_hz").to_number<double>() == 2000,
            "A nested schema records its own rate");
    require(!s7::record_design_rates(document, 0.001), "Recording twice changes nothing");

    // Opened at 1 kHz and run at 2 kHz: the run's dt reseeds the schema, the
    // recorded design rate does not move, and the filter is refused.
    auto saved = boost::json::parse(R"({"name":"saved","dt":0.001,"elements":[
        {"id":"source","type":"Constant","params":{"value":1.0}},
        {"id":"fir","type":"FirFilter","params":{"taps":[0.5,0.5]}}],
        "connections":[{"from_element":"source","from_port":0,"to_element":"fir","to_port":0}]})");
    s7::record_design_rates(saved, 0.001);
    auto at_design = saved;
    s7::configure_schema_discretization(at_design, 0.001);
    require(errors(s7::SchemaLibraryLoader::build_schema(at_design, s7::ElementRegistry::instance())).empty(),
            "The saved document runs at its own dt");
    s7::configure_schema_discretization(saved, 0.0005);
    const auto refused = errors(s7::SchemaLibraryLoader::build_schema(saved, s7::ElementRegistry::instance()));
    require(refused.find("'fir'") != std::string::npos && refused.find("cannot be recalculated") != std::string::npos,
            "A run at another dt refuses the recorded design: " + refused);
}
} // namespace

int main() try {
    s7::register_builtin_elements(s7::ElementRegistry::instance());
    filters_check_their_design_rate();
    documents_record_their_design_rate();
    std::cout << "Rate-designed filters: design rate checked, recorded from documents, refused at other rates\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
