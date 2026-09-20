#include "s7/plugin_loader.hpp"
#include "s7/element_registry.hpp"
#include "s7/element_plugin.hpp"

#include <dlfcn.h>
#include <iostream>
#include <cstring>
#include <cstdlib>

namespace s7 {

PluginLoader::~PluginLoader() {
    // Keep plugin libraries loaded until process exit.
    // Unloading can invalidate code/data referenced by registry-held factories.
}

std::vector<std::filesystem::path> PluginLoader::get_plugin_search_paths() const {
    std::vector<std::filesystem::path> paths;

    // 1. ~/.s7/plugins/
    if (const char* home = std::getenv("HOME")) {
        paths.push_back(std::filesystem::path(home) / ".s7" / "plugins");
    }

    // 2. /opt/s7/plugins/
    paths.push_back("/opt/s7/plugins");

    // 3. ./plugins/ (relative to current directory)
    paths.push_back("./plugins");

    // 4. $S7_PLUGIN_PATH environment variable
    if (const char* plugin_path = std::getenv("S7_PLUGIN_PATH")) {
        paths.push_back(plugin_path);
    }

    return paths;
}

std::vector<std::filesystem::path> PluginLoader::discover_in_directory(
    const std::filesystem::path& dir
) const {
    std::vector<std::filesystem::path> plugins;

    if (!std::filesystem::exists(dir)) {
        return plugins;
    }

    try {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            if (std::filesystem::is_symlink(entry.symlink_status())) {
                continue;
            }

            const auto& path = entry.path();
            const auto& filename = path.filename().string();

            // Match libs7*.so, libs7*.dylib, s7*.dll
            bool matches = false;

            #ifdef __APPLE__
            matches = filename.find("libs7") == 0 && filename.find(".dylib") != std::string::npos;
            #elif defined(_WIN32)
            matches = filename.find("s7") == 0 && filename.find(".dll") != std::string::npos;
            #else  // Linux
            matches = filename.find("libs7") == 0 &&
                     (filename.find(".so") != std::string::npos);
            #endif

            if (matches) {
                plugins.push_back(path);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "Warning: Error scanning plugin directory " << dir
                  << ": " << e.what() << "\n";
    }

    return plugins;
}

void PluginLoader::load_plugins(ElementRegistry& registry) {
    auto search_paths = get_plugin_search_paths();

    std::cout << "Plugin search paths:\n";
    for (const auto& path : search_paths) {
        std::cout << "  - " << path << "\n";
    }

    for (const auto& search_dir : search_paths) {
        auto plugins = discover_in_directory(search_dir);

        for (const auto& plugin_path : plugins) {
            std::cout << "Found plugin: " << plugin_path << "\n";
            load_plugin(plugin_path, registry);
        }
    }

    std::cout << "Loaded " << loaded_plugins_.size() << " plugins\n";
}

bool PluginLoader::load_plugin(
    const std::filesystem::path& plugin_path,
    ElementRegistry& registry
) {
    // Open shared library
    dlerror();  // Clear any previous errors
    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY | RTLD_LOCAL);

    if (!handle) {
        const char* err = dlerror();
        std::cerr << "Failed to load plugin " << plugin_path << ": "
                  << (err ? err : "Unknown error") << "\n";
        return false;
    }

    // Resolve query symbol first, then cast after validating dlsym state.
    dlerror();
    void* query_symbol = dlsym(handle, "s7_plugin_query");
    const char* query_err = dlerror();
    if (query_err || !query_symbol) {
        std::cerr << "Plugin " << plugin_path << " missing s7_plugin_query: "
                  << (query_err ? query_err : "Unknown error") << "\n";
        dlclose(handle);
        return false;
    }
    auto query_func = reinterpret_cast<S7PluginQueryFunc>(query_symbol);

    // Get plugin info
    const auto* info = query_func();
    if (!info) {
        std::cerr << "Plugin " << plugin_path << " returned null info\n";
        dlclose(handle);
        return false;
    }

    // Check API version
    if (info->api_version != S7_PLUGIN_API_VERSION) {
        std::cerr << "Plugin " << plugin_path << " API version mismatch: "
                  << "expected " << S7_PLUGIN_API_VERSION
                  << ", got " << info->api_version << "\n";
        dlclose(handle);
        return false;
    }

    std::cout << "  Plugin: " << info->plugin_name
              << " v" << info->plugin_version
              << " (" << info->element_count << " elements)\n";

    // Resolve registration symbol first, then cast after validating dlsym state.
    dlerror();
    void* register_symbol = dlsym(handle, "s7_register_elements");
    const char* register_err = dlerror();
    if (register_err || !register_symbol) {
        std::cerr << "Plugin " << plugin_path << " missing s7_register_elements: "
                  << (register_err ? register_err : "Unknown error") << "\n";
        dlclose(handle);
        return false;
    }
    auto register_func = reinterpret_cast<S7RegisterElementsFunc>(register_symbol);

    // Register elements
    try {
        register_func(&registry);
    } catch (const std::exception& e) {
        std::cerr << "Plugin " << plugin_path << " registration failed: "
                  << e.what() << "\n";
        dlclose(handle);
        return false;
    }

    // Track loaded plugin
    loaded_plugins_.push_back({
        info->plugin_name ? info->plugin_name : "unknown",
        info->plugin_version ? info->plugin_version : "unknown",
        handle
    });

    std::cout << "  Successfully loaded " << info->element_count << " elements\n";
    return true;
}

std::vector<std::string> PluginLoader::loaded_plugins() const {
    std::vector<std::string> names;
    for (const auto& plugin : loaded_plugins_) {
        names.push_back(plugin.name);
    }
    return names;
}

void PluginLoader::unload_all() {
    for (auto& plugin : loaded_plugins_) {
        if (plugin.handle) {
            dlclose(plugin.handle);
            plugin.handle = nullptr;
        }
    }
    loaded_plugins_.clear();
}

} // namespace s7

