// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "extension_host.hpp"
#include <filesystem>

namespace pd {
class PluginManager {
  public:
    explicit PluginManager(CommandRegistry& registry) : registry_(registry) {}
    ~PluginManager();
    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;
    void load(const std::filesystem::path& absoluteFile);
    // Only direct children of a caller-selected, trusted directory. A rejection doesn't stop discovery.
    void loadDirectory(const std::filesystem::path&,
                       const std::function<void(bool, std::string_view)>& report);
    void unload(const std::filesystem::path& absoluteFile);

  private:
    struct Module;
    CommandRegistry& registry_;
    using Modules = std::map<std::filesystem::path, std::shared_ptr<Module>>;
    Modules::iterator findLoaded(const std::filesystem::path& canonicalPath);
    Modules modules_;
};
} // namespace pd
