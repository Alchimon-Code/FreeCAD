// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include <planedraft/document.hpp>

#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace pd {
// Private C++ host interfaces, never passed to a DLL. All operations belong to the document thread.
struct ExtensionContext {
    Document& doc;
    std::function<std::vector<EntityId>()> selection;
    std::function<void(bool stderrStream, std::string_view)> output;
};
struct ExtensionCommand {
    std::string global, ru;
    std::function<void(ExtensionContext&, std::string_view)> run;
    const void* owner;
};
class CommandConflict : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};
class CommandRegistry {
  public:
    using Entries = std::map<std::string, ExtensionCommand, std::less<>>;
    explicit CommandRegistry(std::set<std::string> reserved = {}) : reserved_(std::move(reserved)) {}
    CommandRegistry(const CommandRegistry&) = delete;
    CommandRegistry& operator=(const CommandRegistry&) = delete;
    // One installer through evaluation/init AND document commit/notification. A log/observer cannot
    // sneak a colliding installation between name validation and the non-allocating node transfer.
    class Batch {
      public:
        explicit Batch(CommandRegistry& registry);
        ~Batch();
        Batch(const Batch&) = delete;
        Batch& operator=(const Batch&) = delete;
        void stage(ExtensionCommand);
        void publish() noexcept;
        [[nodiscard]] bool empty() const { return pending_.empty(); }

      private:
        CommandRegistry& registry_;
        Entries pending_;
    };
    void remove(const void* owner) noexcept;
    [[nodiscard]] const Entries& entries() const { return commands_; }
    [[nodiscard]] std::string resolve(std::string_view uppercase, bool allowRussian) const;
    void run(std::string_view global, ExtensionContext&, std::string_view args) const;

  private:
    bool installing_ = false;
    Entries commands_;
    std::set<std::string> reserved_;
};
} // namespace pd
