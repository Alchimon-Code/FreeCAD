// SPDX-License-Identifier: LGPL-2.1-or-later
#include "extension_host.hpp"

#include <algorithm>
#include <stdexcept>

namespace pd {
namespace {
bool validName(std::string_view s, bool russian) {
    unsigned length = 0;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i++]);
        if (!(c >= 'A' && c <= 'Z') && !(length && (c == '_' || (c >= '0' && c <= '9')))) {
            if (!russian || c != 0xd0 || i == s.size())
                return false;
            auto next = static_cast<unsigned char>(s[i++]);
            if (next != 0x81 && (next < 0x90 || next > 0xaf))
                return false; // Ё, А..Я
        }
        if (++length > 32)
            return false;
    }
    return length != 0;
}
} // namespace
CommandRegistry::Batch::Batch(CommandRegistry& r) : registry_(r) {
    if (r.installing_)
        throw std::logic_error("reentrant command registration");
    r.installing_ = true;
}
CommandRegistry::Batch::~Batch() { registry_.installing_ = false; }
void CommandRegistry::Batch::stage(ExtensionCommand cmd) {
    if (!validName(cmd.global, false) || (!cmd.ru.empty() && !validName(cmd.ru, true)) || !cmd.run ||
        !cmd.owner)
        throw std::invalid_argument("invalid command registration: " + cmd.global);
    for (const auto* name : {&cmd.global, &cmd.ru}) {
        if (name->empty())
            continue;
        auto taken = [&](const Entries& b) {
            return std::any_of(b.begin(), b.end(), [&](const auto& entry) {
                return entry.first == *name || entry.second.ru == *name;
            });
        };
        if (registry_.reserved_.contains(*name) || taken(registry_.commands_) || taken(pending_))
            throw CommandConflict("command name in use: " + *name);
    }
    auto key = cmd.global;
    pending_.emplace(std::move(key), std::move(cmd));
}
void CommandRegistry::Batch::publish() noexcept { registry_.commands_.merge(pending_); }
void CommandRegistry::remove(const void* owner) noexcept {
    std::erase_if(commands_, [&](const auto& entry) { return entry.second.owner == owner; });
}
std::string CommandRegistry::resolve(std::string_view s, bool ru) const {
    if (auto it = commands_.find(s); it != commands_.end())
        return it->first;
    if (ru && !s.empty())
        for (const auto& [name, cmd] : commands_)
            if (cmd.ru == s)
                return name;
    return {};
}
void CommandRegistry::run(std::string_view name, ExtensionContext& ctx, std::string_view args) const {
    auto it = commands_.find(name);
    if (it == commands_.end())
        throw std::invalid_argument("unknown extension command");
    auto run = it->second.run; // keeps the module/code alive even during a synchronous log callback
    run(ctx, args);
}
} // namespace pd
