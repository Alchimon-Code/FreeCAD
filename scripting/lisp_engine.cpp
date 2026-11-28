// SPDX-License-Identifier: LGPL-2.1-or-later
#include "lisp.hpp"

#include <cmath>
#include <numbers>

namespace pd::lisp {
namespace {
using Forms = std::vector<Node>;
using Env = std::map<std::string, Value, std::less<>>;
struct Function {
    std::vector<std::string> params;
    Forms body;
    Env closure;
};
using Functions = std::map<std::string, std::shared_ptr<const Function>, std::less<>>;
struct Program {
    std::string source;
    Forms body;
    Env closure;
    Functions functions;
};
using Define = std::function<void(std::string, std::string, Program)>;
const std::string& symbol(const Node& n) {
    if (n.kind != Node::Symbol || n.text.empty())
        throw std::invalid_argument("expected symbol");
    return n.text;
}
class Evaluation {
  public:
    Evaluation(const Program& p, ExtensionContext& c, Transaction& t, Define define)
        : program_(p), ctx_(c), tx_(t), define_(std::move(define)), functions_(p.functions) {}
    void run(std::string_view args) {
        auto env = program_.closure;
        env["doc"] = DocumentRef{};
        env["args"] = std::string(args);
        env["pi"] = std::numbers::pi;
        // Format an error once. Catch/rethrow at every recursive frame can exhaust the
        // default Windows thread stack while the exception itself is being dispatched.
        try {
            sequence(program_.body, env, 0);
        } catch (const std::exception& e) {
            throw Error(program_.source + ":" + std::to_string(site_.first) + ":" +
                        std::to_string(site_.second) + ": " + e.what());
        }
    }

  private:
    const Program& program_;
    ExtensionContext& ctx_;
    Transaction& tx_;
    Define define_;
    Functions functions_;
    std::pair<unsigned, unsigned> site_{1, 1};
    unsigned fuel_ = 250000;
    size_t outputBytes_ = 0;
    bool writing_ = false;
    Value sequence(std::span<const Node> body, Env& env, unsigned depth) {
        Value result;
        for (auto& n : body)
            result = eval(n, env, depth);
        return result;
    }
    Value eval(const Node& n, Env& env, unsigned depth) {
        const auto parent = site_;
        site_ = {n.line, n.column};
        // Only restore the parent on success: run() needs the innermost failed location.
        auto result = evaluate(n, env, depth);
        site_ = parent;
        return result;
    }
    Value evaluate(const Node& n, Env& env, unsigned depth) {
        if (!fuel_ || depth > 64)
            throw std::runtime_error("evaluation limit exceeded");
        --fuel_;
        if (n.kind == Node::String)
            return n.text;
        if (n.kind == Node::Symbol) {
            if (auto it = env.find(n.text); it != env.end())
                return it->second;
            if (auto v = literal(n.text))
                return *v;
            throw std::invalid_argument("unbound symbol: " + n.text);
        }
        if (n.children.empty())
            return List{};
        auto& op = symbol(n.children[0]);
        return form(op, std::span(n.children).subspan(1), env, depth + 1);
    }
    Value form(const std::string& op, std::span<const Node> nodes, Env& env, unsigned depth) {
        auto atLeast = [&](size_t count) {
            if (nodes.size() < count)
                throw std::invalid_argument(op + ": too few arguments");
        };
        auto val = [&](size_t i) { return eval(nodes[i], env, depth); };
        if (op == "begin")
            return sequence(nodes, env, depth);
        if (op == "if") {
            if (nodes.size() != 3)
                throw std::invalid_argument("if: expected condition and two branches");
            return val(val(0).boolean() ? 1 : 2);
        }
        if (op == "define") {
            if (nodes.size() != 2)
                throw std::invalid_argument("define: expected name and value");
            auto v = val(1);
            env[symbol(nodes[0])] = v;
            return v;
        }
        if (op == "let" || op == "let*") {
            atLeast(2);
            if (nodes[0].kind != Node::Form)
                throw std::invalid_argument("expected bindings");
            auto local = env;
            for (auto& b : nodes[0].children) {
                if (b.children.size() != 2)
                    throw std::invalid_argument("invalid binding");
                auto v = eval(b.children[1], op == "let" ? env : local, depth);
                local[symbol(b.children[0])] = std::move(v);
            }
            return sequence(nodes.subspan(1), local, depth);
        }
        if (op == "foreach" || op == "for") {
            atLeast(2);
            auto& binding = nodes[0].children;
            if (binding.size() != 2)
                throw std::invalid_argument("expected (variable sequence/count)");
            auto& key = symbol(binding[0]);
            auto v = eval(binding[1], env, depth);
            auto local = env;
            Value result;
            if (op == "foreach") {
                for (auto& item : v.list()) {
                    local[key] = item;
                    result = sequence(nodes.subspan(1), local, depth);
                }
            } else {
                auto count = v.integer();
                for (uint64_t i = 0; i < count; ++i) {
                    local[key] = i;
                    result = sequence(nodes.subspan(1), local, depth);
                }
            }
            return result;
        }
        if (op == "with-transaction") {
            atLeast(3);
            val(0).document();
            (void)val(1).string();
            if (writing_)
                throw std::logic_error("nested with-transaction");
            writing_ = true;
            auto result = sequence(nodes.subspan(2), env, depth);
            writing_ = false;
            return result;
        }
        if (op == "defun") {
            atLeast(3);
            if (nodes[1].kind != Node::Form)
                throw std::invalid_argument("expected parameter list");
            auto f = std::make_shared<Function>();
            for (auto& p : nodes[1].children)
                f->params.push_back(symbol(p));
            f->closure = env;
            f->body.assign(nodes.begin() + 2, nodes.end());
            functions_[symbol(nodes[0])] = std::move(f);
            return true;
        }
        if (op == "defcommand") {
            atLeast(3);
            auto name = val(0).string(), ru = val(1).string();
            define_(std::move(name), std::move(ru),
                    {program_.source, Forms(nodes.begin() + 2, nodes.end()), env, functions_});
            return true;
        }
        List args;
        args.reserve(nodes.size());
        for (auto& n : nodes)
            args.push_back(eval(n, env, depth));
        if (auto it = functions_.find(op); it != functions_.end()) {
            auto f = it->second;
            arity(args, f->params.size(), f->params.size());
            auto local = f->closure;
            for (size_t i = 0; i < args.size(); ++i)
                local[f->params[i]] = std::move(args[i]);
            return sequence(f->body, local, depth);
        }
        return apply(op, args);
    }
    Value apply(std::string_view op, const List& a) {
        if (op == "list")
            return a;
        if (op == "length") {
            arity(a, 1, 1);
            return uint64_t(a[0].list().size());
        }
        if (op == "nth") {
            arity(a, 2, 2);
            return a[0].list().at(a[1].integer());
        }
        if (op == "get") {
            arity(a, 2, 2);
            for (auto& entry : a[0].list()) {
                auto& pair = entry.list();
                arity(pair, 2, 2);
                if (pair[0].string() == a[1].string())
                    return pair[1];
            }
            throw std::invalid_argument("no such field");
        }
        if (op == "print" || op == "eprint") {
            std::string s;
            for (auto& v : a) {
                if (!s.empty())
                    s += ' ';
                s += display(v);
                if (s.size() > 65536)
                    throw std::runtime_error("output line too large");
            }
            outputBytes_ += s.size() + 1;
            if (outputBytes_ > 1024 * 1024)
                throw std::runtime_error("output limit exceeded");
            ctx_.output(op == "eprint", s);
            return true;
        }
        if (op == "error") {
            arity(a, 1, 1);
            throw std::runtime_error(a[0].string());
        }
        if (op == "number") {
            arity(a, 1, 1);
            auto v = literal(a[0].string());
            if (!v)
                throw std::invalid_argument("invalid number");
            (void)v->number();
            return *v;
        }
        if (op == "sin" || op == "cos") {
            arity(a, 1, 1);
            return op == "sin" ? std::sin(a[0].number()) : std::cos(a[0].number());
        }
        if (op == "=" || op == "<" || op == ">") {
            arity(a, 2, 2);
            if (op == "=") {
                if (auto s = std::get_if<std::string>(&a[0].data))
                    return *s == a[1].string();
                if (auto i = std::get_if<uint64_t>(&a[0].data);
                    i && std::holds_alternative<uint64_t>(a[1].data))
                    return *i == a[1].integer();
            }
            auto x = a[0].number(), y = a[1].number();
            return op == "=" ? x == y : op == "<" ? x < y : x > y;
        }
        if (op == "+" || op == "-" || op == "*" || op == "/") {
            arity(a, 1, a.max_size());
            double result = a.front().number();
            if (a.size() == 1 && op == "-")
                result = -result;
            else if (a.size() == 1 && op == "/")
                result = 1 / result;
            for (size_t i = 1; i < a.size(); ++i) {
                double v = a[i].number();
                if (op == "+")
                    result += v;
                else if (op == "-")
                    result -= v;
                else if (op == "*")
                    result *= v;
                else
                    result /= v;
            }
            if (!std::isfinite(result))
                throw std::domain_error("non-finite arithmetic result");
            return result;
        }
        if (auto v = dom(op, a, ctx_, tx_, writing_))
            return *v;
        throw std::invalid_argument("unknown function: " + std::string(op));
    }
};
class LispEngine final : public IScriptEngine {
  public:
    explicit LispEngine(CommandRegistry& r) : registry_(r) {}
    ~LispEngine() override { registry_.remove(this); }
    void execute(std::string_view source, std::string_view name, ExtensionContext& ctx) override {
        try {
            executeProgram({std::string(name), parse(source, name), {}, {}}, ctx, {});
        } catch (const std::exception& e) {
            ctx.output(true, e.what());
            throw;
        }
    }

  private:
    CommandRegistry& registry_;
    void executeProgram(const Program& p, ExtensionContext& ctx, std::string_view args) {
        CommandRegistry::Batch definitions(registry_);
        ctx.doc.transact([&](Transaction& tx) {
            Evaluation eval(p, ctx, tx, [&](std::string name, std::string ru, Program captured) {
                definitions.stage({std::move(name), std::move(ru),
                                   [this, body = std::make_shared<const Program>(std::move(captured))](
                                       ExtensionContext& c, std::string_view tail) {
                                       try {
                                           executeProgram(*body, c, tail);
                                       } catch (const std::exception& e) {
                                           c.output(true, e.what());
                                           throw;
                                       }
                                   },
                                   this});
            });
            eval.run(args);
        }); // syntax, evaluation, validation or publication failure drops both private edits and definitions
        definitions.publish();
    }
};
} // namespace
} // namespace pd::lisp
namespace pd {
std::unique_ptr<IScriptEngine> makeLispEngine(CommandRegistry& r) {
    return std::make_unique<lisp::LispEngine>(r);
}
} // namespace pd
