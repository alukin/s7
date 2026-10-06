#include "s7/plugin_loader.hpp"
#include "s7/element_registry.hpp"
#include "s7/element_plugin.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <boost/json.hpp>

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <unordered_set>
#include <sstream>
#include <utility>
#include <system_error>

namespace fs = std::filesystem;

namespace s7 {

namespace {

auto path_key(const fs::path& path) -> std::string
{
    std::error_code ec;
    const auto canonical = fs::weakly_canonical(path, ec);
    if (!ec) {
        return canonical.string();
    }
    return path.lexically_normal().string();
}

auto read_manifest_info(const std::string& manifest_json, const fs::path& plugin_path) -> PluginLoader::LoadedPluginInfo
{
    boost::system::error_code ec;
    const auto parsed = boost::json::parse(manifest_json, ec);
    if (ec) {
        throw std::runtime_error("Plugin manifest parsing failed for '" + plugin_path.string() + "': " + ec.message());
    }
    if (!parsed.is_object()) {
        throw std::runtime_error("Plugin manifest must be a JSON object for '" + plugin_path.string() + "'.");
    }

    const auto& root = parsed.as_object();
    PluginLoader::LoadedPluginInfo info;
    info.path = plugin_path;

    const auto read_string = [&root](const char* field, const std::string& fallback = {}) -> std::string {
        const auto it = root.find(field);
        if (it == root.end()) {
            return fallback;
        }
        if (!it->value().is_string()) {
            throw std::runtime_error(std::string{"Plugin manifest field '"} + field + "' must be a string.");
        }
        return std::string(it->value().as_string());
    };

    const auto api_version_it = root.find("api_version");
    if (api_version_it != root.end() && api_version_it->value().is_uint64()) {
        const auto api_version = static_cast<std::uint32_t>(api_version_it->value().as_uint64());
        if (api_version != S7_PLUGIN_API_VERSION) {
            throw std::runtime_error("Plugin manifest API version mismatch for '" + plugin_path.string() + "'.");
        }
    }

    info.name = read_string("plugin_name", plugin_path.filename().string());
    info.version = read_string("plugin_version", "unknown");
    info.author = read_string("author");
    info.description = read_string("description");

    const auto elements_it = root.find("elements");
    if (elements_it != root.end()) {
        if (!elements_it->value().is_array()) {
            throw std::runtime_error("Plugin manifest field 'elements' must be an array.");
        }
        for (const auto& element_value : elements_it->value().as_array()) {
            if (!element_value.is_string()) {
                throw std::runtime_error("Plugin manifest field 'elements' must contain strings.");
            }
            info.elements.emplace_back(element_value.as_string().c_str());
        }
    }

    const auto count_it = root.find("element_count");
    if (count_it != root.end() && count_it->value().is_uint64()) {
        const auto declared = static_cast<std::size_t>(count_it->value().as_uint64());
        if (!info.elements.empty() && declared != info.elements.size()) {
            throw std::runtime_error("Plugin manifest element_count does not match elements array for '" + plugin_path.string() + "'.");
        }
        if (info.elements.empty()) {
            info.elements.resize(declared);
        }
    }

    return info;
}

auto read_query_info(const S7PluginInfo* info, const fs::path& plugin_path) -> PluginLoader::LoadedPluginInfo
{
    if (!info) {
        throw std::runtime_error("Plugin query returned null info for '" + plugin_path.string() + "'.");
    }

    PluginLoader::LoadedPluginInfo out;
    out.name = info->plugin_name ? info->plugin_name : plugin_path.filename().string();
    out.version = info->plugin_version ? info->plugin_version : "unknown";
    out.author = info->author ? info->author : std::string{};
    out.path = plugin_path;
    out.elements.resize(info->element_count);
    return out;
}

auto resolve_symbol(void* handle, const char* symbol_name) -> void*
{
    dlerror();
    void* symbol = dlsym(handle, symbol_name);
    const char* err = dlerror();
    if (err || !symbol) {
        return nullptr;
    }
    return symbol;
}

} // namespace

PluginLoader::PluginLoader(std::vector<fs::path> search_paths, std::ostream& log)
    : search_paths_(std::move(search_paths))
    , log_(log)
{
}

PluginLoader::~PluginLoader() {
    // Keep plugin libraries loaded until process exit.
    // Unloading can invalidate code/data referenced by registry-held factories.
}

void PluginLoader::add_search_path(fs::path path)
{
    if (path.empty()) {
        return;
    }
    search_paths_.insert(search_paths_.begin(), std::move(path));
}

std::vector<std::filesystem::path> PluginLoader::get_plugin_search_paths() const {
    std::vector<std::filesystem::path> paths;

    for (const auto& path : search_paths_) {
        paths.push_back(path);
    }

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
) {
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

    log_ << "Plugin search paths:\n";
    for (const auto& path : search_paths) {
        log_ << "  - " << path << "\n";
    }

    for (const auto& search_dir : search_paths) {
        auto plugins = discover_in_directory(search_dir);

        for (const auto& plugin_path : plugins) {
            log_ << "Found plugin: " << plugin_path << "\n";
            load_plugin(plugin_path, registry);
        }
    }

    log_ << "Loaded " << loaded_plugins_.size() << " plugins\n";
}

bool PluginLoader::load_plugin(
    const std::filesystem::path& plugin_path,
    ElementRegistry& registry
) {
    const auto normalized_path = path_key(plugin_path);
    if (loaded_paths_.contains(normalized_path)) {
        return true;
    }

    // Open shared library
    dlerror();  // Clear any previous errors
    void* handle = dlopen(plugin_path.c_str(), RTLD_LAZY | RTLD_LOCAL);

    if (!handle) {
        const char* err = dlerror();
        std::cerr << "Failed to load plugin " << plugin_path << ": "
                  << (err ? err : "Unknown error") << "\n";
        return false;
    }

    LoadedPluginInfo info;

    if (const auto manifest_symbol = resolve_symbol(handle, "s7_plugin_manifest_json")) {
        auto manifest_func = reinterpret_cast<S7PluginManifestJsonFunc>(manifest_symbol);
        try {
            const char* manifest_json = manifest_func();
            if (manifest_json) {
                info = read_manifest_info(manifest_json, plugin_path);
            }
        } catch (const std::exception& e) {
            std::cerr << "Plugin " << plugin_path << " manifest error: " << e.what() << "\n";
            dlclose(handle);
            return false;
        }
    }

    if (info.name.empty()) {
        const auto query_symbol = resolve_symbol(handle, "s7_plugin_query");
        if (!query_symbol) {
            std::cerr << "Plugin " << plugin_path << " missing s7_plugin_query\n";
            dlclose(handle);
            return false;
        }
        auto query_func = reinterpret_cast<S7PluginQueryFunc>(query_symbol);
        const auto* query_info = query_func();
        if (!query_info) {
            std::cerr << "Plugin " << plugin_path << " returned null info\n";
            dlclose(handle);
            return false;
        }
        if (query_info->api_version != S7_PLUGIN_API_VERSION) {
            std::cerr << "Plugin " << plugin_path << " API version mismatch: expected "
                      << S7_PLUGIN_API_VERSION << ", got " << query_info->api_version << "\n";
            dlclose(handle);
            return false;
        }
        info = read_query_info(query_info, plugin_path);
    }

    log_ << "  Plugin: " << info.name
              << " v" << info.version
              << (info.author.empty() ? "" : (" by " + info.author))
              << " (" << info.elements.size() << " elements)\n";

    const auto register_symbol = resolve_symbol(handle, "s7_register_elements");
    if (!register_symbol) {
        std::cerr << "Plugin " << plugin_path << " missing s7_register_elements\n";
        dlclose(handle);
        return false;
    }
    auto register_func = reinterpret_cast<S7RegisterElementsFunc>(register_symbol);

    // Register elements. Registration is all or nothing: on failure, the
    // types it added are removed before the library is unloaded, so no
    // descriptor keeps code from an unloaded library.
    const auto before = registry.element_types();
    auto added_types = [&] {
        std::vector<std::string> added;
        for (const auto& type : registry.element_types()) {
            if (std::ranges::find(before, type) == before.end()) {
                added.push_back(type);
            }
        }
        return added;
    };
    try {
        register_func(&registry);
    } catch (...) {
        std::string reason = "unknown exception";
        try {
            throw;
        } catch (const std::exception& e) {
            reason = e.what();
        } catch (...) {
        }
        const auto added = added_types();
        registry.unregister(added);
        std::cerr << "Plugin " << plugin_path << " registration failed: " << reason << " (" << added.size()
                  << " partial registrations removed)\n";
        dlclose(handle);
        return false;
    }

    // Track loaded plugin
    info.path = plugin_path;
    loaded_paths_.insert(normalized_path);
    LoadedPlugin loaded;
    loaded.info = std::move(info);
    loaded.handle = handle;
    loaded.registry = &registry;
    loaded.registered_types = added_types();
    loaded_plugins_.push_back(std::move(loaded));

    log_ << "  Successfully loaded " << loaded_plugins_.back().info.elements.size() << " elements\n";
    return true;
}

std::vector<std::string> PluginLoader::loaded_plugins() const {
    std::vector<std::string> names;
    for (const auto& plugin : loaded_plugins_) {
        names.push_back(plugin.info.name);
    }
    return names;
}

std::vector<PluginLoader::LoadedPluginInfo> PluginLoader::loaded_plugin_info() const {
    std::vector<LoadedPluginInfo> info;
    info.reserve(loaded_plugins_.size());
    for (const auto& plugin : loaded_plugins_) {
        info.push_back(plugin.info);
    }
    return info;
}

void PluginLoader::unload_all() {
    for (auto& plugin : loaded_plugins_) {
        if (plugin.registry) {
            plugin.registry->unregister(plugin.registered_types);
        }
        if (plugin.handle) {
            dlclose(plugin.handle);
            plugin.handle = nullptr;
        }
    }
    loaded_plugins_.clear();
    loaded_paths_.clear();
}

} // namespace s7

