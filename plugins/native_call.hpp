// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "extension_host.hpp"
#include <planedraft/pd_plugin_api.h>
#include <planedraft/plugin.h>

namespace pd::abi {
std::string statusText(PdStatus);
void invoke(PdRunV2 run, void* user, ExtensionContext&, std::string_view args);
void invoke(const PdPluginV1&, ExtensionContext&, std::string_view args);
} // namespace pd::abi
