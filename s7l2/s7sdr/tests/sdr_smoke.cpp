// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <s7/sdr.hpp>
#include <s7/compiler.hpp>
#include <cassert>
#include <iostream>
int main() {
    auto& registry=s7::ElementRegistry::instance(); s7::sdr::register_elements(registry);
    s7::Schema schema("offline SDR");
    schema.add_element(s7::make_element_spec("rx",*registry.find("SdrSource"),
        {{"type","CI12"},{"backend","unavailable"},{"device","explicit"}}));
    auto graph=s7::Compiler::compile(schema); assert(graph.elements.at("rx")->output_ports()[0].type==s7::SignalType::CI12);
    bool rejected=false; try { s7::sdr::provider("unavailable"); } catch(const std::invalid_argument&) { rejected=true; } assert(rejected);
    std::cout<<"SDR offline types/metadata and unavailable-provider rejection passed\n";
}
