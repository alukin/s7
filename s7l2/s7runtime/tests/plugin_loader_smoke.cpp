// SPDX-FileCopyrightText: Oleksiy Lukin and S7 contributors
// SPDX-License-Identifier: LGPL-3.0-or-later

// Plugin registration is all or nothing, and unloading removes a plugin's
// element types before its code goes away.
#include <algorithm>
#include <chrono>
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

#if !defined(_WIN32) && defined(S7_INSTALL_PLUGIN_SUBDIR)
    // Stage only our fixture in the build tree's installed-layout directory.
    // Discovery must work without an explicit path, including Ubuntu multiarch.
    const auto installed_directory = *executable / ".." / S7_INSTALL_PLUGIN_SUBDIR;
    std::filesystem::create_directories(installed_directory);
    const auto installed_plugin = installed_directory / "libs7-discovery-smoke.so";
    expect(!std::filesystem::exists(installed_plugin), "discovery fixture does not overwrite an existing file");
    std::filesystem::copy_file(S7_TEST_PLUGIN, installed_plugin);
    struct FixtureCleanup {
        std::filesystem::path path;
        ~FixtureCleanup() { std::error_code error; std::filesystem::remove(path, error); }
    } cleanup{installed_plugin};
    {
        s7::PluginLoader discovered;
        discovered.load_plugins(registry);
        const auto info = discovered.loaded_plugin_info();
        expect(std::any_of(info.begin(), info.end(), [&](const auto& plugin) {
            return std::filesystem::equivalent(plugin.path, installed_plugin);
        }), "configured distribution library directory is searched for installed plugins");
        expect(registry.find("TestPluginConstant") != nullptr, "installed-layout plugin registers its element");
        discovered.unload_all();
    }
#endif
#if defined(_WIN32)
    // A plugin in a child directory must resolve an extra dependency beside the
    // executable. The executable itself does not import that dependency DLL.
    const auto nested_directory = *executable / ("plugin-search-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(nested_directory);
    const auto nested_plugin = nested_directory / std::filesystem::path(S7_NESTED_PLUGIN).filename();
    std::filesystem::copy_file(S7_NESTED_PLUGIN, nested_plugin);
    struct NestedCleanup {
        std::filesystem::path plugin, directory;
        ~NestedCleanup() {
            std::error_code error;
            std::filesystem::remove(plugin, error);
            std::filesystem::remove(directory, error);
        }
    } nested_cleanup{nested_plugin, nested_directory};
    {
        s7::PluginLoader nested;
        expect(nested.load_plugin(nested_plugin, registry), "nested plugin resolves application-directory dependency");
        nested.unload_all();
    }
#endif
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
