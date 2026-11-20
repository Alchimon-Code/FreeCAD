// SPDX-License-Identifier: LGPL-2.1-or-later
#include "lisp.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>

namespace pd::lisp {
namespace {
class Reader {
  public:
    Reader(std::string_view source, std::string_view name) : s(source), name(name) {}
    std::vector<Node> read() {
        if (s.size() > 1024 * 1024)
            bad("source exceeds 1 MiB");
        if (!validText(s))
            bad("expected UTF-8 source without NUL");
        if (s.starts_with("\xef\xbb\xbf"))
            s.remove_prefix(3);
        std::vector<Node> result;
        space();
        while (!s.empty()) {
            result.push_back(node(0));
            space();
        }
        return result;
    }

  private:
    std::string_view s, name;
    unsigned line = 1, col = 1, nodes = 0;
    [[noreturn]] void bad(const char* msg) const {
        throw Error(std::string(name) + ":" + std::to_string(line) + ":" + std::to_string(col) + ": " + msg);
    }
    char take() {
        char c = s.front();
        s.remove_prefix(1);
        if (c == '\n') {
            ++line;
            col = 1;
        } else
            ++col;
        return c;
    }
    static bool blank(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
    void space() {
        while (!s.empty()) {
            if (blank(s.front()))
                take();
            else if (s.front() == ';') {
                while (!s.empty() && take() != '\n') {
                }
            } else
                break;
        }
    }
    Node node(unsigned depth) {
        if (depth > 96 || ++nodes > 100000)
            bad("expression limit exceeded");
        Node n{Node::Symbol, line, col, {}, {}};
        char first = s.front();
        if (first == '(') {
            take();
            n.kind = Node::Form;
            space();
            while (!s.empty() && s.front() != ')') {
                n.children.push_back(node(depth + 1));
                space();
            }
            if (s.empty())
                bad("missing )");
            take();
        } else if (first == ')')
            bad("unexpected )");
        else if (first == '\"') {
            take();
            n.kind = Node::String;
            while (!s.empty() && s.front() != '\"') {
                char c = take();
                if (c == '\\') {
                    if (s.empty())
                        bad("incomplete escape");
                    c = take();
                    switch (c) {
                    case 'n':
                        c = '\n';
                        break;
                    case 't':
                        c = '\t';
                        break;
                    case '\\':
                    case '\"':
                        break;
                    default:
                        bad("unknown escape");
                    }
                }
                n.text += c;
            }
            if (s.empty())
                bad("unterminated string");
            take();
        } else {
            while (!s.empty() && !blank(s.front()) && s.front() != '(' && s.front() != ')' &&
                   s.front() != ';' && s.front() != '\"')
                n.text += take();
        }
        return n;
    }
};
} // namespace
std::vector<Node> parse(std::string_view source, std::string_view name) {
    return Reader(source, name).read();
}
std::optional<Value> literal(std::string_view s) {
    if (s == "#t")
        return true;
    if (s == "#f")
        return false;
    if (s.empty())
        return {};
    auto end = s.data() + s.size();
    uint64_t id = 0;
    auto whole = std::from_chars(s.data(), end, id);
    if (whole.ec == std::errc{} && whole.ptr == end)
        return id;
    double n = 0;
    auto real = std::from_chars(s.data(), end, n);
    if (real.ec == std::errc{} && real.ptr == end && std::isfinite(n))
        return n;
    return {};
}
Value::Value(List list) {
    depth_ = 1;
    for (const auto& item : list)
        depth_ = std::max(depth_, item.depth_ + 1);
    if (depth_ > 96)
        throw std::invalid_argument("list nesting limit exceeded");
    data = std::make_shared<const List>(std::move(list));
}
double Value::number() const {
    if (auto* d = std::get_if<double>(&data))
        return *d;
    if (auto* i = std::get_if<uint64_t>(&data))
        return static_cast<double>(*i);
    throw std::invalid_argument("expected number");
}
uint64_t Value::integer() const {
    if (auto i = std::get_if<uint64_t>(&data))
        return *i;
    auto d = number();
    if (d < 0 || d > 9007199254740991.0 || d != std::trunc(d))
        throw std::invalid_argument("expected exact unsigned integer");
    return static_cast<uint64_t>(d);
}
bool Value::boolean() const {
    if (auto p = std::get_if<bool>(&data))
        return *p;
    throw std::invalid_argument("expected boolean");
}
const std::string& Value::string() const {
    if (auto p = std::get_if<std::string>(&data))
        return *p;
    throw std::invalid_argument("expected string");
}
const List& Value::list() const {
    if (auto p = std::get_if<SharedList>(&data))
        return **p;
    throw std::invalid_argument("expected list");
}
void Value::document() const {
    if (!std::holds_alternative<DocumentRef>(data))
        throw std::invalid_argument("expected active document");
}
std::string display(const Value& value) {
    std::string out;
    auto append = [&](auto& self, const Value& v, unsigned depth) -> void {
        if (depth > 96 || out.size() > 65536)
            throw std::runtime_error("output limit exceeded");
        if (auto p = std::get_if<SharedList>(&v.data)) {
            out += '(';
            bool first = true;
            for (auto& item : **p) {
                if (!first)
                    out += ' ';
                self(self, item, depth + 1);
                first = false;
            }
            out += ')';
        } else if (auto s = std::get_if<std::string>(&v.data))
            out += *s;
        else if (auto i = std::get_if<uint64_t>(&v.data))
            out += std::to_string(*i);
        else if (auto d = std::get_if<double>(&v.data)) {
            char buf[64];
            auto r = std::to_chars(buf, buf + 64, *d);
            out.append(buf, r.ptr);
        } else if (auto b = std::get_if<bool>(&v.data))
            out += *b ? "#t" : "#f";
        else
            out += "#<document>";
    };
    append(append, value, 0);
    return out;
}
void arity(const List& args, std::size_t min, std::size_t max) {
    if (args.size() < min || args.size() > max)
        throw std::invalid_argument("wrong argument count");
}
} // namespace pd::lisp
