// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "extension_host.hpp"

namespace pd {
class IScriptEngine {
  public:
    virtual ~IScriptEngine() = default;
    // Synchronous UTF-8 source. Diagnostics include source name and line/column, go to stderr
    // and propagate as a host-side exception. The registry outlives the engine.
    virtual void execute(std::string_view source, std::string_view sourceName, ExtensionContext&) = 0;
};
std::unique_ptr<IScriptEngine> makeLispEngine(CommandRegistry&);
} // namespace pd
