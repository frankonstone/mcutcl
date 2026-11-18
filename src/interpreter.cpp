#include <mcutcl/interpreter.hpp>
#include <mcutcl/list.hpp>
#include "evaluator.hpp"
#include "collections.hpp"
#include "poll.hpp"
#include "text.hpp"
#include <array>
#include <cstdint>

#include <utility>
#include <optional>

namespace mcutcl {
namespace {
Error failure(ErrorCode code, std::string message) { return {code, std::move(message), {}}; }
bool horizontal(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }
bool name_character(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9') || c == '_';
}
using detail::utf8;
std::expected<std::string_view, Error> scalar_name(std::string_view name) {
    if (name.starts_with("::")) { name.remove_prefix(2); }
    if (name.find("::") != std::string_view::npos
        || (!name.empty() && name.back() == ')' && name.find('(') != std::string_view::npos)) {
        return std::unexpected(failure(ErrorCode::unsupported, "arrays and namespaces are not implemented"));
    }
    if (!utf8(name)) { return std::unexpected(failure(ErrorCode::unsupported, "invalid UTF-8 name")); }
    return name;
}
} // namespace
namespace detail {
struct Node;
using Script = std::vector<Node>;
struct Part final {
    enum class Kind { literal, variable, command } kind{};
    std::string text;
    Script script;
    std::size_t offset{};
};
using Word = std::vector<Part>;
struct Node final { std::vector<Word> words; std::size_t offset{}; };
} // namespace detail
namespace {
using detail::Node;
using detail::Word;
using detail::Part;

class Parser final {
public:
    Parser(std::string_view source, const Limits& limits, std::size_t start = 0, RunControl* run = nullptr)
        : text_{source}, limits_{limits}, position_{start}, poll_{run} {}
    [[nodiscard]] bool incomplete() const { return incomplete_ || trailing_continuation_; }
    [[nodiscard]] std::size_t position() const { return position_; }
    std::expected<Word, Error> operand(std::size_t depth) { return word(false, depth, true); }
    std::expected<std::optional<Node>, Error> next(bool bracket = false, std::size_t depth = 0) {
        if (depth > limits_.nesting) { return bad(ErrorCode::limit, "nesting limit exceeded"); }
        for (;;) {
            spaces();
            if (cancelled()) { return bad(ErrorCode::cancelled,"source scan cancelled"); }
            if (position_ == text_.size() || (bracket && text_[position_] == ']')) { return std::optional<Node>{}; }
            if (text_[position_] == ';' || text_[position_] == '\n') { ++position_; continue; }
            if (text_[position_] != '#') { break; }
            while (position_ < text_.size() && text_[position_] != '\n') {
                if (cancelled()) { return bad(ErrorCode::cancelled,"source scan cancelled"); }
                if (continuation()) { consume_continuation(); } else { ++position_; }
            }
        }
        Node node;
        node.offset = position_;
        while (position_ < text_.size() && text_[position_] != ';' && text_[position_] != '\n'
            && !(bracket && text_[position_] == ']')) {
            if (node.words.size() >= limits_.words) { return bad(ErrorCode::limit, "word limit exceeded"); }
            auto parsed = word(bracket, depth);
            if (!parsed) { return std::unexpected(parsed.error()); }
            node.words.push_back(std::move(*parsed));
            spaces();
        }
        return std::optional<Node>{std::move(node)};
    }
private:
    std::string_view text_;
    const Limits& limits_;
    std::size_t position_{};
    detail::BlockPoll poll_;
    bool stopped_{};
    bool cancelled() {
        if (!stopped_) { stopped_=poll_.cancelled(position_); }
        return stopped_;
    }
    bool incomplete_{}, trailing_continuation_{};
    template<class T = std::optional<Node>> std::expected<T, Error> bad(ErrorCode code, std::string message) const {
        auto error = failure(code, std::move(message)); error.offset = position_;
        return std::unexpected(std::move(error));
    }
    template<class T> std::expected<T, Error> missing(std::string message) {
        incomplete_ = true;
        return bad<T>(ErrorCode::syntax, std::move(message));
    }
    bool continuation() const { return position_ + 1 < text_.size() && text_[position_] == '\\' && text_[position_ + 1] == '\n'; }
    void consume_continuation() {
        position_ += 2;
        trailing_continuation_ = position_ == text_.size();
        while (position_ < text_.size() && !cancelled() && (text_[position_] == ' ' || text_[position_] == '\t')) { ++position_; }
    }
    void spaces() {
        while (position_ < text_.size() && !cancelled()) {
            if (horizontal(text_[position_])) { ++position_; }
            else if (continuation()) { consume_continuation(); }
            else { break; }
        }
    }
    bool separator(bool bracket) const {
        return position_ == text_.size() || horizontal(text_[position_]) || continuation() || text_[position_] == '\n'
            || text_[position_] == ';' || (bracket && text_[position_] == ']');
    }
    std::expected<Word, Error> word(bool bracket, std::size_t depth, bool operand = false) {
        Word result;
        std::string literal;
        const auto start = position_;
        const auto flush = [&] {
            if (!literal.empty()) { result.push_back({Part::Kind::literal, std::move(literal), {}, start}); literal.clear(); }
        };
        if (text_[position_] == '{') {
            if (!operand && text_.substr(position_, 3) == "{*}" && position_ + 3 < text_.size()) {
                const auto c = text_[position_ + 3];
                const bool continued = c == '\\' && position_ + 4 < text_.size() && text_[position_ + 4] == '\n';
                if (!horizontal(c) && !continued && c != '\n' && c != ';' && !(bracket && c == ']')) {
                    return bad<Word>(ErrorCode::unsupported, "argument expansion is not implemented");
                }
            }
            ++position_;
            std::size_t nesting = 1;
            while (position_ < text_.size() && nesting != 0) {
                if (cancelled()) { return bad<Word>(ErrorCode::cancelled,"source scan cancelled"); }
                const auto c = text_[position_++];
                if (c == '\\' && position_ < text_.size()) {
                    if (text_[position_] == '\n') { --position_; consume_continuation(); literal += ' '; }
                    else { literal += c; literal += text_[position_++]; }
                } else if (c == '{') {
                    if (++nesting > limits_.nesting) { return bad<Word>(ErrorCode::limit, "brace nesting limit exceeded"); }
                    literal += c;
                } else if (c == '}') { if (--nesting != 0) { literal += c; } }
                else { literal += c; }
            }
            if (nesting != 0) { return missing<Word>("missing close-brace"); }
            if (!operand && !separator(bracket)) { return bad<Word>(ErrorCode::syntax, "extra characters after close-brace"); }
            result.push_back({Part::Kind::literal, std::move(literal), {}, start});
            return result;
        }
        const bool quoted = text_[position_] == '"';
        if (quoted) { ++position_; }
        bool closed = !quoted;
        while (position_ < text_.size()) {
            if (cancelled()) { return bad<Word>(ErrorCode::cancelled,"source scan cancelled"); }
            const auto c = text_[position_];
            if (quoted && c == '"') { ++position_; closed = true; break; }
            if (!quoted && (separator(bracket) || continuation())) { break; }
            ++position_;
            if (c == '\\') {
                if (position_ == text_.size()) { literal += '\\'; continue; }
                const auto escaped = text_[position_++];
                if (escaped == '\n') { position_ -= 2; consume_continuation(); literal += ' '; }
                else if (escaped == 'x' || escaped == 'u' || escaped == 'U' || (escaped >= '0' && escaped <= '7')) {
                    return bad<Word>(ErrorCode::unsupported, "numeric backslash escapes are not implemented");
                } else {
                    switch (escaped) {
                    case 'a': literal += '\a'; break; case 'b': literal += '\b'; break;
                    case 'f': literal += '\f'; break; case 'n': literal += '\n'; break;
                    case 'r': literal += '\r'; break; case 't': literal += '\t'; break;
                    case 'v': literal += '\v'; break; default: literal += escaped; break;
                    }
                }
            } else if (c == '$') {
                const auto at = position_ - 1;
                std::string name;
                if (position_ < text_.size() && text_[position_] == '{') {
                    const auto begin = ++position_;
                    while (position_ < text_.size() && text_[position_] != '}') {
                        if (cancelled()) { return bad<Word>(ErrorCode::cancelled,"source scan cancelled"); }
                        ++position_;
                    }
                    if (position_ == text_.size()) { return missing<Word>("missing close-brace for variable name"); }
                    name = text_.substr(begin, position_++ - begin);
                } else {
                    const auto begin = position_;
                    while (position_ < text_.size()) {
                        if (cancelled()) { return bad<Word>(ErrorCode::cancelled,"source scan cancelled"); }
                        if (name_character(text_[position_])) { ++position_; }
                        else if (text_[position_] == ':' && position_ + 1 < text_.size() && text_[position_ + 1] == ':') {
                            position_ += 2;
                            while (position_ < text_.size() && text_[position_] == ':') { ++position_; }
                        } else { break; }
                    }
                    name = text_.substr(begin, position_ - begin);
                    if (position_ < text_.size() && text_[position_] == '(') {
                        return bad<Word>(ErrorCode::unsupported, "array substitution is not implemented");
                    }
                    if (name.empty()) {
                        if (operand && !quoted) { return bad<Word>(ErrorCode::syntax, "expected expression variable name"); }
                        literal += '$'; continue;
                    }
                }
                flush(); result.push_back({Part::Kind::variable, std::move(name), {}, at});
            } else if (c == '[') {
                flush();
                Part part{Part::Kind::command, {}, {}, position_ - 1};
                for (;;) {
                    auto child = next(true, depth + 1);
                    if (!child) { return std::unexpected(child.error()); }
                    if (!*child) { break; }
                    part.script.push_back(std::move(**child));
                }
                if (position_ == text_.size()) { return missing<Word>("missing close-bracket"); }
                ++position_;
                result.push_back(std::move(part));
            } else { literal += c; }
            if (operand && !quoted) { break; }
        }
        if (!closed) { return missing<Word>("missing close-quote"); }
        if (quoted && !operand && !separator(bracket)) { return bad<Word>(ErrorCode::syntax, "extra characters after close-quote"); }
        flush();
        if (result.empty()) { result.push_back({Part::Kind::literal, {}, {}, start}); }
        return result;
    }
};
} // namespace

// @req REQ-0002 REQ-0008 REQ-0009 REQ-0016
InputInspection inspect_input(std::string_view script, std::string_view source, Limits limits) {
    const auto invalid = [&](Error error) {
        error.source = source;
        for (std::size_t i = 0; i < error.offset && i < script.size(); ++i) {
            if (script[i] == '\n') { ++error.line; error.column = 1; } else { ++error.column; }
        }
        return InputInspection{InputState::invalid, std::move(error)};
    };
    if (script.size() > limits.source_bytes) { return invalid(failure(ErrorCode::limit, "source limit exceeded")); }
    if (!utf8(script)) { return invalid(failure(ErrorCode::unsupported, "source must be valid UTF-8")); }
    Parser parser{script, limits};
    for (;;) {
        const auto node = parser.next();
        if (!node) {
            auto result = invalid(node.error());
            if (parser.incomplete()) { result.state = InputState::incomplete; }
            return result;
        }
        if (!*node) {
            if (parser.incomplete()) {
                auto error = failure(ErrorCode::syntax, "unfinished backslash-newline continuation");
                error.offset = script.size();
                auto result = invalid(std::move(error));
                result.state = InputState::incomplete;
                return result;
            }
            return {};
        }
    }
}

// @req REQ-0001 REQ-0004 REQ-0010
Interpreter::Interpreter(Limits limits) : limits_{limits} {
    commands_.emplace("expr", Builtin::expression);
    commands_.emplace("if", Builtin::conditional);
    commands_.emplace("foreach", Builtin::foreach_loop);
    commands_.emplace("while", Builtin::while_loop);
    commands_.emplace("lappend", Builtin::list_append);
    commands_.emplace("lset", Builtin::list_set);
    commands_.emplace("dict", Builtin::dictionary);
    commands_.emplace("proc", Builtin::procedure);
    commands_.emplace("catch", Builtin::catch_script);
    commands_.emplace("return", Builtin::return_value);
    commands_.emplace("break", Builtin::break_loop);
    commands_.emplace("continue", Builtin::continue_loop);
    commands_.emplace("error", [](Interpreter&, std::span<const std::string> args) -> Result {
        if (args.size() != 2) {
            return std::unexpected(failure(ErrorCode::unsupported, "supported form: error message"));
        }
        return std::unexpected(failure(ErrorCode::script, args[1]));
    });
    commands_.emplace("set", [](Interpreter& self, std::span<const std::string> args) -> Result {
        if (args.size() != 2 && args.size() != 3) {
            return std::unexpected(failure(ErrorCode::wrong_arguments, "wrong # args: should be set varName ?newValue?"));
        }
        if (args.size() == 2) { return self.variable(args[1]); }
        auto assigned = self.set_variable(args[1], args[2]);
        if (!assigned) { return std::unexpected(assigned.error()); }
        return args[2];
    });
    commands_.emplace("unset", [](Interpreter& self, std::span<const std::string> args) -> Result {
        auto names = args.subspan(1);
        const bool quiet = !names.empty() && names.front() == "-nocomplain";
        if (quiet) { names = names.subspan(1); }
        if (!names.empty() && names.front() == "--") { names = names.subspan(1); }
        for (const auto& name : names) {
            if (self.stop_requested()) { return std::unexpected(failure(ErrorCode::cancelled, "evaluation cancelled")); }
            auto key = scalar_name(name);
            if (!key) { return std::unexpected(key.error()); }
            auto& scope = self.locals_ && !name.starts_with("::") ? *self.locals_ : self.variables_;
            auto found = scope.find(*key);
            if (found == scope.end()) {
                if (!quiet) { return std::unexpected(failure(ErrorCode::undefined_variable, "can't unset \"" + name + "\": no such variable")); }
                continue;
            }
            self.state_bytes_ -= found->first.size() + found->second.size();
            --self.variable_count_;
            scope.erase(found);
        }
        return std::string{};
    });
    commands_.emplace("list", [limits](Interpreter& self, std::span<const std::string> args) -> Result {
        return make_list(args.subspan(1), limits, &self.active_->control());
    });
    commands_.emplace("llength", [limits](Interpreter& self, std::span<const std::string> args) -> Result {
        if (args.size() != 2) { return std::unexpected(failure(ErrorCode::wrong_arguments, "llength requires one list")); }
        const auto elements = parse_list(args[1], limits, &self.active_->control());
        if (!elements) { return std::unexpected(elements.error()); }
        return std::to_string(elements->size());
    });
    commands_.emplace("lindex", [limits](Interpreter& self, std::span<const std::string> args) -> Result {
        if (args.size() != 3) { return std::unexpected(failure(ErrorCode::unsupported, "initial lindex requires one canonical decimal index")); }
        const auto index = detail::list_index(args[2]);
        if (!index) { return std::unexpected(index.error()); }
        const auto selected = *index;
        const auto elements = parse_list(args[1], limits, &self.active_->control());
        if (!elements) { return std::unexpected(elements.error()); }
        if (selected < 0 || static_cast<std::uint64_t>(selected) >= elements->size()) { return std::string{}; }
        return (*elements)[static_cast<std::size_t>(selected)];
    });
}

// @req REQ-0010 REQ-0015 REQ-0008
std::expected<void, Error> Interpreter::set_arguments(std::string source, std::span<const std::string> arguments) {
    if (active_) { return std::unexpected(failure(ErrorCode::reentrant, "cannot replace arguments during evaluation")); }
    if (!utf8(source)) { return std::unexpected(failure(ErrorCode::unsupported, "argv0 must be valid UTF-8")); }
    if (source.size() > limits_.value_bytes || limits_.value_bytes < 5) {
        return std::unexpected(failure(ErrorCode::limit, "argument context exceeds value limit"));
    }
    auto encoded = make_list(arguments, limits_);
    if (!encoded) { return std::unexpected(encoded.error()); }
    const auto count = std::to_string(arguments.size());
    const std::array<std::pair<std::string_view, std::string_view>, 3> updates{{
        {"argv0", source}, {"argc", count}, {"argv", *encoded}}};
    auto available = limits_.state_bytes - state_bytes_;
    std::size_t missing{};
    for (const auto& [name, value] : updates) {
        const auto old = variables_.find(name);
        if (old != variables_.end()) { available += name.size() + old->second.size(); }
        else { ++missing; }
    }
    if (missing > limits_.variables - variable_count_ || count.size() > limits_.value_bytes) {
        return std::unexpected(failure(ErrorCode::limit, "argument context exceeds variable limit"));
    }
    for (const auto& [name, value] : updates) {
        if (!detail::take_bytes(available, name.size()) || !detail::take_bytes(available, value.size())) {
            return std::unexpected(failure(ErrorCode::limit, "argument context exceeds state limit"));
        }
    }
    // Recoverable validation completes before any of the three variables change.
    variables_.insert_or_assign("argv0", std::move(source));
    variables_.insert_or_assign("argc", count);
    variables_.insert_or_assign("argv", std::move(*encoded));
    variable_count_ += missing;
    state_bytes_ = limits_.state_bytes - available;
    return {};
}

// @req REQ-0004 REQ-0010
Result Interpreter::variable(std::string_view name) const {
    auto value = lookup(name);
    if (!value) { return std::unexpected(std::move(value.error())); }
    return std::string(*value);
}

std::expected<std::string_view, Error> Interpreter::lookup(std::string_view name) const {
    auto canonical = scalar_name(name);
    if (!canonical) { return std::unexpected(canonical.error()); }
    const auto& scope = locals_ && !name.starts_with("::") ? *locals_ : variables_;
    const auto found = scope.find(*canonical);
    if (found == scope.end()) { return std::unexpected(failure(ErrorCode::undefined_variable, "undefined variable: " + std::string(*canonical))); }
    return std::string_view(found->second);
}

// @req REQ-0004 REQ-0008
std::expected<void, Error> Interpreter::set_variable(std::string name, std::string value) {
    if (active_ && !active_->is_current()) {
        return std::unexpected(failure(ErrorCode::reentrant, "cannot write to a suspended interpreter"));
    }
    if (name.size() > limits_.value_bytes || value.size() > limits_.value_bytes) {
        return std::unexpected(failure(ErrorCode::limit, "variable limit exceeded"));
    }
    auto canonical = scalar_name(name);
    if (!canonical) { return std::unexpected(canonical.error()); }
    auto& scope = locals_ && !name.starts_with("::") ? *locals_ : variables_;
    const auto old = scope.find(*canonical);
    if (old == scope.end() && variable_count_ >= limits_.variables) {
        return std::unexpected(failure(ErrorCode::limit, "variable limit exceeded"));
    }
    auto available = limits_.state_bytes - state_bytes_;
    if (old != scope.end()) { available += canonical->size() + old->second.size(); }
    if (!detail::take_bytes(available, canonical->size()) || !detail::take_bytes(available, value.size())) {
        return std::unexpected(failure(ErrorCode::limit, "variable exceeds state limit"));
    }
    if (!utf8(value)) { return std::unexpected(failure(ErrorCode::unsupported, "invalid UTF-8 value")); }
    if (old == scope.end()) { ++variable_count_; }
    if (name.starts_with("::")) { name.erase(0, 2); }
    scope.insert_or_assign(std::move(name), std::move(value));
    state_bytes_ = limits_.state_bytes - available;
    return {};
}

// @req REQ-0010
std::expected<void, Error> Interpreter::register_command(std::string name, Command command) {
    if (active_) { return std::unexpected(failure(ErrorCode::reentrant, "cannot modify command registry during evaluation")); }
    if (name.starts_with("::")) { name.erase(0, 2); }
    if (name.empty() || !utf8(name) || !command || commands_.contains(name)) {
        return std::unexpected(failure(ErrorCode::host, "empty, invalid or duplicate command registration"));
    }
    commands_.emplace(std::move(name), std::move(command));
    return {};
}

namespace {
Error locate(Error error, std::string_view text, std::string_view source, std::size_t at) {
    if (error.source.empty()) {
        error.source = source;
        error.offset = at;
        error.line = 1;
        error.column = 1;
        for (std::size_t i = 0; i < at && i < text.size(); ++i) {
            if (text[i] == '\n') {
                ++error.line;
                error.column = 1;
            } else {
                ++error.column;
            }
        }
    }
    return error;
}
} // namespace

CompletionResult detail::Evaluator::expand(std::span<const Part> word, std::string_view text,
                                           std::string_view source, std::size_t level,
                                           Boundary boundary, std::size_t& available) {
    const auto located = [&](Error error, std::size_t at) {
        return locate(std::move(error), text, source, at);
    };
    const auto& limits = interpreter_.limits_;
    std::string value;
    for (const auto& part : word) {
        if (stop_requested()) {
            return std::unexpected(
                located(failure(ErrorCode::cancelled, "evaluation cancelled"), part.offset));
        }
        std::string substituted;
        std::string_view expanded = part.text;
        if (part.kind == Part::Kind::variable) {
            auto result = interpreter_.lookup(part.text);
            if (!result) {
                return std::unexpected(located(result.error(), part.offset));
            }
            expanded = *result;
        } else if (part.kind == Part::Kind::command) {
            for (const auto& command : part.script) {
                auto result = execute(command, text, source, level + 1, boundary);
                if (!result || result->code != CompletionCode::ok) {
                    return result;
                }
                substituted = std::move(result->value);
            }
            expanded = substituted;
        }
        if (expanded.size() > limits.value_bytes - value.size() ||
            !take_bytes(available, expanded.size())) {
            return std::unexpected(
                located(failure(ErrorCode::limit, "value limit exceeded"), part.offset));
        }
        if (auto paid = run_.consume_work(expanded.size()); !paid) {
            return std::unexpected(located(paid.error(), part.offset));
        }
        value += expanded;
    }
    return Completion{CompletionCode::ok, std::move(value)};
}

CompletionResult detail::Evaluator::execute(const Node& node, std::string_view text,
                                            std::string_view source, std::size_t level,
                                            Boundary boundary) {
    const auto located = [&](Error error, std::size_t at) {
        return locate(std::move(error), text, source, at);
    };
    if (auto ready = check_dispatch(); !ready) {
        return std::unexpected(located(ready.error(), node.offset));
    }
    std::vector<std::string> args;
    std::size_t available = interpreter_.limits_.argument_bytes;
    for (const auto& word : node.words) {
        auto value = expand(word, text, source, level, boundary, available);
        if (!value || value->code != CompletionCode::ok) {
            return value;
        }
        args.push_back(std::move(value->value));
    }
    auto result = dispatch(args, source, level);
    if (!result) {
        return std::unexpected(located(result.error(), node.offset));
    }
    if (boundary == Boundary::frame && (result->code == CompletionCode::break_loop ||
                                        result->code == CompletionCode::continue_loop)) {
        return std::unexpected(located(escaped_loop(result->code), node.offset));
    }
    return result;
}

CompletionResult detail::Evaluator::expression_operand(std::string_view input,
                                                       std::size_t& consumed,
                                                       std::string_view source, std::size_t depth,
                                                       bool evaluate) {
    auto limits = interpreter_.limits_;
    limits.nesting = depth_limit_;
    Parser parser{input, limits, consumed, &run_};
    auto word = parser.operand(depth);
    if (!word) {
        return std::unexpected(locate(word.error(), input, source, word.error().offset));
    }
    auto charge = run_.consume_work(parser.position() - consumed);
    if (!charge) {
        return std::unexpected(locate(charge.error(), input, source, consumed));
    }
    consumed = parser.position();
    if (!evaluate) {
        return Completion{};
    }
    auto available = limits.argument_bytes;
    return expand(*word, input, source, depth, Boundary::body, available);
}

// @req REQ-0001 REQ-0002 REQ-0003 REQ-0004 REQ-0008 REQ-0009 REQ-0010
CompletionResult detail::Evaluator::script(std::string_view text, std::string_view source,
                                           std::size_t depth, Boundary boundary) {
    const auto located = [&](Error error, std::size_t at) {
        return locate(std::move(error), text, source, at);
    };
    auto limits = interpreter_.limits_;
    limits.nesting = depth_limit_;
    if (stop_requested()) {
        return std::unexpected(located(failure(ErrorCode::cancelled, "evaluation cancelled"), 0));
    }
    if (depth > depth_limit_) { return std::unexpected(located(failure(ErrorCode::limit, "nesting limit exceeded"), 0)); }
    if (text.size() > limits.source_bytes) { return std::unexpected(located(failure(ErrorCode::limit, "source limit exceeded"), 0)); }
    if (auto paid = run_.consume_work(text.size()); !paid) { return std::unexpected(located(paid.error(), 0)); }
    if (!utf8(text)) { return std::unexpected(located(failure(ErrorCode::unsupported, "source must be valid UTF-8"), 0)); }
    Parser parser{text, limits, 0, &run_};
    CompletionResult result{Completion{}};
    for (;;) {
        auto node = parser.next(false, depth);
        if (!node) { return std::unexpected(located(node.error(), node.error().offset)); }
        if (!*node) { return result; }
        result = execute(**node, text, source, depth, boundary);
        if (!result || result->code != CompletionCode::ok) { return result; }
    }
}

std::string_view error_name(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::syntax: return "syntax"; case ErrorCode::unsupported: return "unsupported";
    case ErrorCode::unknown_command: return "unknown_command"; case ErrorCode::wrong_arguments: return "wrong_arguments";
    case ErrorCode::undefined_variable: return "undefined_variable"; case ErrorCode::limit: return "limit";
    case ErrorCode::host: return "host"; case ErrorCode::reentrant: return "reentrant";
    case ErrorCode::cancelled: return "cancelled"; case ErrorCode::script: return "script";
    }
    return "invalid_error_code";
}
} // namespace mcutcl
