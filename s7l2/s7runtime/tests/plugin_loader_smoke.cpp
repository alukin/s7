// Plugin registration is all or nothing, and unloading removes a plugin's
// element types before its code goes away.
#include <iostream>
#include <stdexcept>
#include <string>

#include <s7/builtin_element_registry.hpp>
#include <s7/core.hpp>
#include <s7/platform.hpp>
#include <s7/element_registry.hpp>

#include "s7/plugin_loader.hpp"

namespace {

void expect(const bool condition, const std::string& what)
{
    if (!condition) {
        throw std::runtime_error("Check failed: " + what);
    }
}

} // namespace

int main()
try {
    const auto executable = s7::platform::executable_directory();
    expect(executable && executable->is_absolute() && std::filesystem::is_directory(*executable), "executable directory is available");
    const auto previous = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    expect(s7::platform::executable_directory() == executable, "executable location is independent of cwd");
    std::filesystem::current_path(previous);
    auto& registry = s7::ElementRegistry::instance();
    s7::register_builtin_elements(registry);
    const auto builtin_count = registry.element_count();

    {
        s7::PluginLoader loader;
        expect(!loader.load_plugin(S7_FAILING_PLUGIN, registry), "a plugin whose registration fails is not loaded");
        expect(registry.find("TestPluginConstant") == nullptr, "its partial registration is removed");
        expect(registry.element_count() == builtin_count, "nothing else changed");
        expect(registry.find("Constant") != nullptr, "the built-in Constant it collided with stays");
        expect(loader.loaded_plugins().empty(), "not tracked as loaded");
    }

    // The process keeps running: a good plugin loads, runs, and unloads cleanly.
    s7::PluginLoader loader;
    expect(loader.load_plugin(S7_TEST_PLUGIN, registry), "the good plugin loads");
    const auto* descriptor = registry.find("TestPluginConstant");
    expect(descriptor != nullptr, "its element is registered");
    {
        s7::Schema schema{"plugin"};
        schema.set_rate(s7::Rate::from_hz(1));
        schema.add_element(s7::make_element_spec("c", *descriptor, {}));
        s7::NodeRuntime runtime{s7::Compiler::compile(schema)};
        runtime.step();
        expect(runtime.real_output_of("c") == 42.0, "the plugin element runs");
    }
    loader.unload_all();
    expect(registry.find("TestPluginConstant") == nullptr, "unloading removes the plugin's element types");
    expect(registry.element_count() == builtin_count, "only the plugin's types were removed");
    return 0;
} catch (const std::exception& ex) {
    std::cerr << "s7runtime plugin loader smoke: " << ex.what() << '\n';
    return 1;
}
