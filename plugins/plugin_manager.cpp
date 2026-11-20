// SPDX-License-Identifier: LGPL-2.1-or-later
#include "plugin_manager.h"
#include "native_call.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace pd {
namespace fs = std::filesystem;
namespace {
std::string utf8(const fs::path& p) {
    auto u = p.u8string();
    return {u.begin(), u.end()};
}
bool libraryFile(const fs::path& path) {
#ifdef _WIN32
    auto ext = path.extension().wstring();
    for (auto& c : ext)
        if (c >= L'A' && c <= L'Z')
            c += L'a' - L'A';
    return ext == L".dll";
#elif defined(__APPLE__)
    return path.extension() == ".dylib" || path.extension() == ".so";
#else
    return path.extension() == ".so";
#endif
}
class Library {
  public:
    explicit Library(const fs::path& path) {
#ifdef _WIN32
        handle_ = LoadLibraryExW(path.c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!handle_)
            throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                    "LoadLibraryExW");
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle_)
            throw std::runtime_error(dlerror());
#endif
    }
    ~Library() {
#ifdef _WIN32
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
    }
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    template <class T> T symbol(const char* name) const {
#ifdef _WIN32
        return reinterpret_cast<T>(GetProcAddress(handle_, name));
#else
        return reinterpret_cast<T>(dlsym(handle_, name));
#endif
    }

  private:
#ifdef _WIN32
    HMODULE handle_{};
#else
    void* handle_{};
#endif
};
// A defective C++ extension is not entitled to throw. Translate while its module is STILL mapped.
template <class F> decltype(auto) boundary(F&& f) {
    try {
        return f();
    } catch (const std::exception& e) {
        throw std::runtime_error(e.what());
    } catch (...) {
        throw std::runtime_error("extension threw an unknown exception");
    }
}
struct Registration;
thread_local Registration* registrar = nullptr;
struct RegistrationScope {
    Registration* previous;
    explicit RegistrationScope(Registration* current) : previous(registrar) { registrar = current; }
    ~RegistrationScope() { registrar = previous; }
};
struct Registration {
    std::function<void(const char*, const char*, PdRunV2, void*)> add;
    char error[256]{};
};
PdStatus PD_CALL registerCommand(void* ctx, const char* name, const char* ru, PdRunV2 run,
                                 void* user) noexcept {
    if (!registrar || ctx != registrar)
        return PD_BAD_STATE;
    auto& r = *static_cast<Registration*>(ctx);
    try {
        if (!name || !run)
            throw std::invalid_argument("missing command name/callback");
        r.add(name, ru ? ru : "", run, user);
        return PD_OK;
    } catch (const CommandConflict& e) {
        std::snprintf(r.error, sizeof r.error, "%s", e.what());
        return PD_CONFLICT;
    } catch (const std::invalid_argument& e) {
        std::snprintf(r.error, sizeof r.error, "%s", e.what());
        return PD_INVALID_ARGUMENT;
    } catch (const std::bad_alloc&) {
        std::snprintf(r.error, sizeof r.error, "allocation failed");
        return PD_NO_MEMORY;
    } catch (...) {
        std::snprintf(r.error, sizeof r.error, "registration failed");
        return PD_ERROR;
    }
}
} // namespace
struct PluginManager::Module {
    explicit Module(const fs::path& p) : library(p) {}
    Library library;
    const PdPluginV2* api = nullptr;
    void* instance = nullptr;
    ~Module() {
        if (api) {
            try {
                api->shutdown(instance);
            } catch (...) { /* ABI violation; never throw from teardown */
            }
        }
    }
};
PluginManager::~PluginManager() {
    for (auto& [_, m] : modules_)
        registry_.remove(m.get());
}
void PluginManager::load(const fs::path& path) {
    if (!path.is_absolute() || !fs::is_regular_file(path) || !libraryFile(path))
        throw std::invalid_argument("expected an absolute library path");
    const auto key = fs::canonical(path);
    if (findLoaded(key) != modules_.end())
        throw std::invalid_argument("plugin already loaded");
    CommandRegistry::Batch pending(registry_);
    auto m = std::make_shared<Module>(key);
    if (auto query = m->library.symbol<PdQueryV2Fn>("pd_plugin_query_v2")) {
        auto api = boundary([&] { return query(PD_ABI_V2); });
        if (!api)
            throw std::runtime_error("plugin rejected host ABI v2");
        if (api->api_version != PD_ABI_V2)
            throw std::runtime_error("incompatible v2 descriptor: expected API 2, got " +
                                     std::to_string(api->api_version));
        if (api->struct_size < PD_PLUGIN_V2_MIN_SIZE)
            throw std::runtime_error("incompatible v2 descriptor: struct_size " +
                                     std::to_string(api->struct_size) + " < " +
                                     std::to_string(PD_PLUGIN_V2_MIN_SIZE));
        if (!api->init || !api->shutdown)
            throw std::runtime_error("v2 descriptor is missing init/shutdown");
        Registration registration{[&](const char* name, const char* ru, PdRunV2 run, void* user) {
            pending.stage({name, ru,
                           [m, run, user](ExtensionContext& c, std::string_view args) {
                               boundary([&] { abi::invoke(run, user, c, args); });
                           },
                           m.get()});
        }};
        const PdRegistrarV2 registrar{PD_ABI_V2, sizeof(PdRegistrarV2), &registration, registerCommand};
        m->api = api;
        auto status = boundary([&] {
            RegistrationScope scope(&registration);
            return api->init(&registrar, &m->instance);
        });
        if (registration.error[0])
            throw std::runtime_error(registration.error);
        if (status != PD_OK)
            throw std::runtime_error("plugin init returned " + abi::statusText(status));
    } else {
        auto queryV1 = m->library.symbol<PdQueryFn>("pd_plugin_query");
        auto api = queryV1 ? boundary([&] { return queryV1(PD_ABI_V1); }) : nullptr;
        if (!api || api->abi_version != PD_ABI_V1 || api->struct_size < sizeof(PdPluginV1) || !api->command ||
            !api->run)
            throw std::runtime_error("missing or incompatible v1/v2 descriptor");
        pending.stage({api->command,
                       {},
                       [m, api](ExtensionContext& c, std::string_view args) {
                           boundary([&] { abi::invoke(*api, c, args); });
                       },
                       m.get()});
    }
    if (pending.empty())
        throw std::runtime_error("plugin registered no commands");
    modules_.emplace(key, m);
    pending.publish();
}
PluginManager::Modules::iterator PluginManager::findLoaded(const fs::path& key) {
    if (auto it = modules_.find(key); it != modules_.end())
        return it;
    // Hard links and case variants can denote the same mapped DLL. Do not initialize it twice.
    return std::find_if(modules_.begin(), modules_.end(), [&](const auto& entry) {
        std::error_code ec;
        return fs::equivalent(entry.first, key, ec);
    });
}
void PluginManager::unload(const fs::path& path) {
    if (!path.is_absolute())
        throw std::invalid_argument("expected an absolute library path");
    auto it = findLoaded(fs::weakly_canonical(path));
    if (it == modules_.end())
        throw std::invalid_argument("plugin is not loaded");
    registry_.remove(it->second.get());
    modules_.erase(it);
}
void PluginManager::loadDirectory(const fs::path& path,
                                  const std::function<void(bool, std::string_view)>& report) {
    if (!path.is_absolute())
        throw std::invalid_argument("expected an absolute plugin directory");
    std::vector<fs::path> files;
    for (const auto& f : fs::directory_iterator(path))
        if (!f.is_symlink() && f.is_regular_file() && libraryFile(f.path()))
            files.push_back(f.path());
    std::sort(files.begin(), files.end());
    for (auto& file : files) {
        std::string name = utf8(file.filename());
        try {
            load(file);
        } catch (const std::exception& e) {
            report(true, name + ": " + e.what());
            continue;
        }
        report(false, name + ": OK");
    }
}
} // namespace pd
