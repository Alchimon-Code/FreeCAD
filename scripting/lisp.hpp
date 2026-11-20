// SPDX-License-Identifier: LGPL-2.1-or-later
#pragma once
#include "script_engine.hpp"
#include <optional>
#include <stdexcept>

namespace pd::lisp {
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Node {
    enum Kind { Symbol, String, Form } kind;
    unsigned line, column;
    std::string text;
    std::vector<Node> children;
};
std::vector<Node> parse(std::string_view source, std::string_view name);
struct DocumentRef {};
struct Value;
using List = std::vector<Value>;
using SharedList = std::shared_ptr<const List>;
struct Value {
    std::variant<bool, double, uint64_t, std::string, SharedList, DocumentRef> data = false;
    Value() = default;
    Value(bool b) : data(b) {}
    Value(double n) : data(n) {}
    Value(uint64_t n) : data(n) {}
    Value(std::string s) : data(std::move(s)) {}
    Value(const char* s) : data(std::string(s)) {}
    Value(List list);
    Value(DocumentRef doc) : data(doc) {}
    double number() const;
    uint64_t integer() const;
    bool boolean() const;
    const std::string& string() const;
    const List& list() const;
    void document() const;

  private:
    unsigned depth_ = 0;
};
std::optional<Value> literal(std::string_view);
std::string display(const Value&);
void arity(const List&, std::size_t min, std::size_t max);
// The caller supplies a writer only within with-transaction. Return nullopt for non-DOM names.
std::optional<Value> dom(std::string_view op, const List& args, ExtensionContext&, Transaction& stage,
                         bool writing);
} // namespace pd::lisp
