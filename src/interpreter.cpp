#include <mcutcl/interpreter.hpp>
#include <mcutcl/list.hpp>
#include "text.hpp"
#include <charconv>
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
std::expected<std::string, Error> scalar_name(std::string_view name) {
    if (name.starts_with("::")) { name.remove_prefix(2); }
    if (name.find("::") != std::string_view::npos
        || (!name.empty() && name.back() == ')' && name.find('(') != std::string_view::npos)) {
        return std::unexpected(failure(ErrorCode::unsupported, "arrays and namespaces are not implemented"));
    }
    if (!utf8(name)) { return std::unexpected(failure(ErrorCode::unsupported, "invalid UTF-8 name")); }
    return std::string(name);
}
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

class Parser final {
public:
    Parser(std::string_view source, const Limits& limits) : text_{source}, limits_{limits} {}
    [[nodiscard]] bool incomplete() const { return incomplete_ || trailing_continuation_; }
    std::expected<std::optional<Node>, Error> next(bool bracket = false, std::size_t depth = 0) {
        if (depth > limits_.nesting) { return bad(ErrorCode::limit, "nesting limit exceeded"); }
        for (;;) {
            spaces();
            if (position_ == text_.size() || (bracket && text_[position_] == ']')) { return std::optional<Node>{}; }
            if (text_[position_] == ';' || text_[position_] == '\n') { ++position_; continue; }
            if (text_[position_] != '#') { break; }
            while (position_ < text_.size() && text_[position_] != '\n') {
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
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t')) { ++position_; }
    }
    void spaces() {
        while (position_ < text_.size()) {
            if (horizontal(text_[position_])) { ++position_; }
            else if (continuation()) { consume_continuation(); }
            else { break; }
        }
    }
    bool separator(bool bracket) const {
        return position_ == text_.size() || horizontal(text_[position_]) || continuation() || text_[position_] == '\n'
            || text_[position_] == ';' || (bracket && text_[position_] == ']');
    }
    std::expected<Word, Error> word(bool bracket, std::size_t depth) {
        Word result;
        std::string literal;
        const auto start = position_;
        const auto flush = [&] {
            if (!literal.empty()) { result.push_back({Part::Kind::literal, std::move(literal), {}, start}); literal.clear(); }
        };
        if (text_[position_] == '{') {
            if (text_.substr(position_, 3) == "{*}" && position_ + 3 < text_.size()) {
                const auto c = text_[position_ + 3];
                const bool continued = c == '\\' && position_ + 4 < text_.size() && text_[position_ + 4] == '\n';
                if (!horizontal(c) && !continued && c != '\n' && c != ';' && !(bracket && c == ']')) {
                    return bad<Word>(ErrorCode::unsupported, "argument expansion is not implemented");
                }
            }
            ++position_;
            std::size_t nesting = 1;
            while (position_ < text_.size() && nesting != 0) {
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
            if (!separator(bracket)) { return bad<Word>(ErrorCode::syntax, "extra characters after close-brace"); }
            result.push_back({Part::Kind::literal, std::move(literal), {}, start});
            return result;
        }
        const bool quoted = text_[position_] == '"';
        if (quoted) { ++position_; }
        bool closed = !quoted;
        while (position_ < text_.size()) {
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
                    while (position_ < text_.size() && text_[position_] != '}') { ++position_; }
                    if (position_ == text_.size()) { return missing<Word>("missing close-brace for variable name"); }
                    name = text_.substr(begin, position_++ - begin);
                } else {
                    const auto begin = position_;
                    while (position_ < text_.size()) {
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
                    if (name.empty()) { literal += '$'; continue; }
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
        }
        if (!closed) { return missing<Word>("missing close-quote"); }
        if (quoted && !separator(bracket)) { return bad<Word>(ErrorCode::syntax, "extra characters after close-quote"); }
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
    commands_.emplace("set", [](Interpreter& self, std::span<const std::string> args) -> Result {
        if (args.size() != 2 && args.size() != 3) {
            return std::unexpected(failure(ErrorCode::wrong_arguments, "wrong # args: should be set varName ?newValue?"));
        }
        if (args.size() == 2) { return self.variable(args[1]); }
        auto assigned = self.set_variable(args[1], args[2]);
        if (!assigned) { return std::unexpected(assigned.error()); }
        return args[2];
    });
    commands_.emplace("list", [limits](Interpreter&, std::span<const std::string> args) -> Result {
        return make_list(args.subspan(1), limits);
    });
    commands_.emplace("llength", [limits](Interpreter&, std::span<const std::string> args) -> Result {
        if (args.size() != 2) { return std::unexpected(failure(ErrorCode::wrong_arguments, "llength requires one list")); }
        const auto elements = parse_list(args[1], limits);
        if (!elements) { return std::unexpected(elements.error()); }
        return std::to_string(elements->size());
    });
    commands_.emplace("lindex", [limits](Interpreter&, std::span<const std::string> args) -> Result {
        if (args.size() != 3) { return std::unexpected(failure(ErrorCode::unsupported, "initial lindex requires one canonical decimal index")); }
        std::string_view index = args[2];
        auto digits = index;
        if (digits.starts_with('-')) { digits.remove_prefix(1); }
        if (digits.empty() || (digits.size() > 1 && digits.front() == '0') || index == "-0") {
            return std::unexpected(failure(ErrorCode::unsupported, "unsupported lindex index form"));
        }
        for (const auto c : digits) {
            if (c < '0' || c > '9') { return std::unexpected(failure(ErrorCode::unsupported, "unsupported lindex index form")); }
        }
        std::int64_t selected{};
        const auto converted = std::from_chars(index.data(), index.data()+index.size(), selected);
        if (converted.ec != std::errc{} || converted.ptr != index.data()+index.size()) {
            return std::unexpected(failure(ErrorCode::unsupported, "lindex index exceeds supported integer range"));
        }
        const auto elements = parse_list(args[1], limits);
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
    std::size_t missing{};
    for (const auto* name : {"argv0", "argc", "argv"}) { if (!variables_.contains(name)) { ++missing; } }
    if (missing > limits_.variables - variables_.size() || count.size() > limits_.value_bytes) {
        return std::unexpected(failure(ErrorCode::limit, "argument context exceeds variable limit"));
    }
    // Recoverable validation completes before any of the three variables change.
    variables_.insert_or_assign("argv0", std::move(source));
    variables_.insert_or_assign("argc", count);
    variables_.insert_or_assign("argv", std::move(*encoded));
    return {};
}

// @req REQ-0004 REQ-0010
Result Interpreter::variable(std::string_view name) const {
    auto canonical = scalar_name(name);
    if (!canonical) { return std::unexpected(canonical.error()); }
    const auto found = variables_.find(*canonical);
    if (found == variables_.end()) { return std::unexpected(failure(ErrorCode::undefined_variable, "undefined variable: " + *canonical)); }
    return found->second;
}

// @req REQ-0004 REQ-0008
std::expected<void, Error> Interpreter::set_variable(std::string name, std::string value) {
    auto canonical = scalar_name(name);
    if (!canonical) { return std::unexpected(canonical.error()); }
    if (name.size() > limits_.value_bytes || value.size() > limits_.value_bytes
        || (!variables_.contains(*canonical) && variables_.size() >= limits_.variables)) {
        return std::unexpected(failure(ErrorCode::limit, "variable limit exceeded"));
    }
    if (!utf8(value)) { return std::unexpected(failure(ErrorCode::unsupported, "invalid UTF-8 value")); }
    variables_.insert_or_assign(std::move(*canonical), std::move(value));
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

// @req REQ-0001 REQ-0002 REQ-0003 REQ-0004 REQ-0008 REQ-0009 REQ-0010
Result Interpreter::evaluate(std::string_view script, std::string_view source) {
    const auto annotate = [&](Error error, std::size_t at) {
        if (error.source.empty()) {
            error.source = source; error.offset = at; error.line = 1; error.column = 1;
            for (std::size_t i = 0; i < at && i < script.size(); ++i) {
                if (script[i] == '\n') { ++error.line; error.column = 1; } else { ++error.column; }
            }
        }
        return error;
    };
    if (active_) { return std::unexpected(annotate(failure(ErrorCode::reentrant, "public evaluation cannot re-enter an active interpreter"), 0)); }
    commands_executed_ = 0;
    if (script.size() > limits_.source_bytes) { return std::unexpected(annotate(failure(ErrorCode::limit, "source limit exceeded"), 0)); }
    if (!utf8(script)) { return std::unexpected(annotate(failure(ErrorCode::unsupported, "source must be valid UTF-8"), 0)); }
    active_ = true;
    struct Release final { bool& flag; ~Release() { flag = false; } } release{active_};
    const auto execute = [&](auto&& self, const Node& node) -> Result {
        std::vector<std::string> args;
        for (const auto& word : node.words) {
            std::string value;
            for (const auto& part : word) {
                Result expanded{part.text};
                if (part.kind == Part::Kind::variable) { expanded = variable(part.text); }
                else if (part.kind == Part::Kind::command) {
                    expanded = std::string{};
                    for (const auto& command : part.script) {
                        expanded = self(self, command);
                        if (!expanded) { break; }
                    }
                }
                if (!expanded) { return std::unexpected(annotate(expanded.error(), part.offset)); }
                if (expanded->size() > limits_.value_bytes - value.size()) {
                    return std::unexpected(annotate(failure(ErrorCode::limit, "value limit exceeded"), part.offset));
                }
                value += *expanded;
            }
            args.push_back(std::move(value));
        }
        if (commands_executed_ >= limits_.commands) {
            return std::unexpected(annotate(failure(ErrorCode::limit, "command budget exceeded"), node.offset));
        }
        ++commands_executed_;
        std::string_view command = args.front();
        if (command.starts_with("::")) { command.remove_prefix(2); }
        const auto found = commands_.find(command);
        if (found == commands_.end()) {
            return std::unexpected(annotate(failure(ErrorCode::unknown_command, "unknown or unimplemented command: " + args.front()), node.offset));
        }
        auto result = found->second(*this, args);
        if (!result) { return std::unexpected(annotate(result.error(), node.offset)); }
        if (result->size() > limits_.value_bytes || !utf8(*result)) {
            return std::unexpected(annotate(failure(ErrorCode::limit, "invalid or oversized command result"), node.offset));
        }
        return result;
    };
    Parser parser{script, limits_};
    Result result{std::string{}};
    for (;;) {
        auto node = parser.next();
        if (!node) { return std::unexpected(annotate(node.error(), node.error().offset)); }
        if (!*node) { return result; }
        result = execute(execute, **node);
        if (!result) { return result; }
    }
}

std::string_view error_name(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::syntax: return "syntax"; case ErrorCode::unsupported: return "unsupported";
    case ErrorCode::unknown_command: return "unknown_command"; case ErrorCode::wrong_arguments: return "wrong_arguments";
    case ErrorCode::undefined_variable: return "undefined_variable"; case ErrorCode::limit: return "limit";
    case ErrorCode::host: return "host"; case ErrorCode::reentrant: return "reentrant";
    }
    return "invalid_error_code";
}
} // namespace mcutcl
