#ifndef S7RUNTIME_INCLUDE_S7_PLUGIN_LOADER_HPP
#define S7RUNTIME_INCLUDE_S7_PLUGIN_LOADER_HPP

#include <string>
#include <vector>
#include <filesystem>
#include <iostream>
#include <memory>
#include <cstddef>
#include <unordered_set>

namespace s7 {

class ElementRegistry;

// Plugin loader for dynamic element libraries
class PluginLoader {
public:
    struct LoadedPluginInfo {
        std::string name;
        std::string version;
        std::string author;
        std::string description;
        std::vector<std::string> elements;
        std::filesystem::path path;
    };

    // Progress messages go to `log` (errors always to std::cerr).
    explicit PluginLoader(std::vector<std::filesystem::path> search_paths = {}, std::ostream& log = std::cout);
    ~PluginLoader();

    // Prevent copy/move
    PluginLoader(const PluginLoader&) = delete;
    PluginLoader& operator=(const PluginLoader&) = delete;

    // Discover and load all plugins
    void load_plugins(ElementRegistry& registry);

    // Add one more directory to the front of the discovery list.
    void add_search_path(std::filesystem::path path);

    // Load specific plugin
    bool load_plugin(
        const std::filesystem::path& plugin_path,
        ElementRegistry& registry
    );

    // Get loaded plugin count
    [[nodiscard]] size_t loaded_count() const { return loaded_plugins_.size(); }

    // Get list of loaded plugin names
    [[nodiscard]] std::vector<std::string> loaded_plugins() const;

    // Get the full loaded plugin metadata.
    [[nodiscard]] std::vector<LoadedPluginInfo> loaded_plugin_info() const;

    // Removes every plugin's element types from the registry it registered
    // into, then unloads the libraries. Elements created from them must be
    // destroyed first. (The destructor keeps libraries loaded.)
    void unload_all();

private:
    struct LoadedPlugin {
        LoadedPlugin() : info{}, handle(nullptr) {}
        LoadedPluginInfo info;
        void* handle;  // platform::open_library handle
        ElementRegistry* registry{nullptr};
        std::vector<std::string> registered_types; // what its registration added
    };

    std::vector<std::filesystem::path> search_paths_;
    std::ostream& log_;
    std::vector<LoadedPlugin> loaded_plugins_;
    std::unordered_set<std::string> loaded_paths_;

    // Plugin search paths
    [[nodiscard]] std::vector<std::filesystem::path> get_plugin_search_paths() const;

    // Discover plugins in directory
    [[nodiscard]] static std::vector<std::filesystem::path> discover_in_directory(
        const std::filesystem::path& dir
    );

};

} // namespace s7

#endif // S7RUNTIME_INCLUDE_S7_PLUGIN_LOADER_HPP

