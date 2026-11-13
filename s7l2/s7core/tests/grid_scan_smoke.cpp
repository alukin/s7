// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later
#include <iostream>
#include <limits>
#include <s7/core.hpp>
#include <s7/grid_scan.hpp>
#include <stdexcept>
namespace {
void require(bool ok, const char *what) {
    if (!ok)
        throw std::runtime_error(what);
}
template <class F> void rejects(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    require(failed, "Invalid scan accepted");
}
} // namespace
int main() {
    try {
        s7::GridScan source(-2, 4, 3, 7, 3, 2);
        for (int i = 0; i < 9; ++i) {
            source.step();
            const auto &ports = source.output_ports();
            require(ports.size() == 6, "Port count");
            require(ports[0].as<double>() == (i < 6 ? -2 + 3 * (i % 3) : 4), "X-fast ordering / end hold");
            require(ports[1].as<double>() == (i < 3 ? 3 : 7), "Row ordering");
            require(ports[2].as<double>() == std::min(i, 5), "Cell identity");
            require(ports[3].as<double>() == 3 && ports[4].as<double>() == 2, "Explicit dimensions");
            require(ports[5].as<double>() == (i < 6 ? 1 : 0), "Finite scan active flag");
        }
        rejects([] { s7::GridScan bad(-1, 1, -1, 1, -1, 2); });
        rejects([] { s7::GridScan bad(-1, 1, -1, 1, 514, 2); });
        rejects([] { s7::GridScan bad(1, 1); });
        rejects([] {
            s7::GridScan bad(-std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
        });
        s7::register_builtin_elements(s7::ElementRegistry::instance());
        const auto *desc = s7::ElementRegistry::instance().find("GridScan");
        require(desc != nullptr, "Registry entry");
        s7::Schema schema("scan");
        schema.set_rate(s7::Rate::from_hz(2000));
        schema.add_element(s7::make_element_spec("scan", *desc, {{"columns", 3}, {"rows", 2}}));
        require(s7::Validator::validate(schema).ok(), "Scan validation");
        s7::NodeRuntime runtime(s7::Compiler::compile(schema));
        runtime.step();
        require(runtime.real_output_of("scan", 5) == 1, "Compiled source");
        std::cout << "Grid scan checks passed\n";
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
