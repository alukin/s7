#ifndef S7RUNTIME_INCLUDE_S7_PLUGIN_LOADER_HPP
#define S7RUNTIME_INCLUDE_S7_PLUGIN_LOADER_HPP

#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <cstddef>

namespace s7 {

class ElementRegistry;

// Plugin loader for dynamic element libraries
class PluginLoader {
public:
    PluginLoader() = default;
    ~PluginLoader();

    // Prevent copy/move
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // Discover and load all plugins
    void load_plugins(ElementRegistry& registry);

    // Load specific plugin
    bool load_plugin(
        const std::filesystem::path& plugin_path,
        ElementRegistry& registry
    );

    // Get loaded plugin count
    [[nodiscard]] size_t loaded_count() const { return loaded_plugins_.size(); }

    // Get list of loaded plugin names
    [[nodiscard]] std::vector<std::string> loaded_plugins() const;

private:
    struct LoadedPlugin {
        std::string name;
        std::string version;
        void* handle;  // dlopen handle
    };

    std::vector<LoadedPlugin> loaded_plugins_;

    // Plugin search paths
    [[nodiscard]] std::vector<std::filesystem::path> get_plugin_search_paths() const;

    // Discover plugins in directory
    [[nodiscard]] std::vector<std::filesystem::path> discover_in_directory(
        const std::filesystem::path& dir
    ) const;

    // Unload all plugins
    void unload_all();
};

} // namespace s7

#endif // S7RUNTIME_INCLUDE_S7_PLUGIN_LOADER_HPP

