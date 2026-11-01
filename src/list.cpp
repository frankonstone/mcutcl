#include <mcutcl/list.hpp>
#include "text.hpp"

namespace mcutcl {
namespace {
Error error(ErrorCode code, std::string message, std::size_t offset = 0) {
    Error result{code, std::move(message), {}};
    result.offset = offset;
    return result;
}
bool space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }
}

// @req REQ-0005 REQ-0008 REQ-0010
Result make_list(std::span<const std::string> elements, Limits limits) {
    if (elements.size() > limits.words) { return std::unexpected(error(ErrorCode::limit, "list element limit exceeded")); }
    std::string result;
    const auto append = [&](std::string_view value) {
        if (value.size() > limits.value_bytes - result.size()) { return false; }
        result += value;
        return true;
    };
    bool first = true;
    for (const auto& element : elements) {
        if (element.size() > limits.value_bytes) { return std::unexpected(error(ErrorCode::limit, "list value limit exceeded")); }
        if (!detail::utf8(element)) { return std::unexpected(error(ErrorCode::unsupported, "list values must be valid UTF-8")); }
        if (!first && !append(" ")) { return std::unexpected(error(ErrorCode::limit, "encoded list limit exceeded")); }
        first = false;
        if (element.empty()) {
            if (!append("{}")) { return std::unexpected(error(ErrorCode::limit, "encoded list limit exceeded")); }
            continue;
        }
        for (const char c : element) {
            std::string_view escaped;
            switch (c) {
            case '\a': escaped = "\\a"; break; case '\b': escaped = "\\b"; break;
            case '\n': escaped = "\\n"; break; case '\r': escaped = "\\r"; break;
            case '\t': escaped = "\\t"; break; case '\v': escaped = "\\v"; break;
            case '\f': escaped = "\\f"; break;
            default: break;
            }
            bool okay{};
            if (!escaped.empty()) { okay = append(escaped); }
            else if (c == ' ' || c == '{' || c == '}' || c == '[' || c == ']' || c == '$'
                || c == ';' || c == '\\' || c == '"' || c == '#') {
                const char bytes[]{'\\', c};
                okay = append(std::string_view(bytes, 2));
            } else { okay = append(std::string_view(&c, 1)); }
            if (!okay) { return std::unexpected(error(ErrorCode::limit, "encoded list limit exceeded")); }
        }
    }
    return result;
}

// @req REQ-0005 REQ-0008
std::expected<std::vector<std::string>, Error> parse_list(std::string_view text, Limits limits) {
    if (text.size() > limits.value_bytes) { return std::unexpected(error(ErrorCode::limit, "list value limit exceeded")); }
    if (!detail::utf8(text)) { return std::unexpected(error(ErrorCode::unsupported, "list must be valid UTF-8")); }
    std::vector<std::string> elements;
    std::size_t position{};
    while (position < text.size()) {
        while (position < text.size() && space(text[position])) { ++position; }
        if (position == text.size()) { break; }
        if (elements.size() >= limits.words) { return std::unexpected(error(ErrorCode::limit, "list element limit exceeded", position)); }
        std::string value;
        const bool braced = text[position] == '{';
        const bool quoted = text[position] == '"';
        if (braced || quoted) { ++position; }
        bool closed = !braced && !quoted;
        std::size_t nesting = 1;
        if (braced && limits.nesting == 0) { return std::unexpected(error(ErrorCode::limit, "list brace nesting limit exceeded", position)); }
        while (position < text.size()) {
            const char c = text[position++];
            if (!braced && !quoted && space(c)) { break; }
            if (braced) {
                if (c == '\\' && position < text.size()) { value += c; value += text[position++]; }
                else if (c == '{') {
                    if (++nesting > limits.nesting) { return std::unexpected(error(ErrorCode::limit, "list brace nesting limit exceeded", position)); }
                    value += c;
                } else if (c == '}') {
                    if (--nesting == 0) { closed = true; break; }
                    value += c;
                } else { value += c; }
            } else if (quoted && c == '"') { closed = true; break; }
            else if (c == '\\' && position < text.size()) {
                const char escaped = text[position++];
                if (escaped == '\n') {
                    value += ' ';
                    while (position < text.size() && (text[position] == ' ' || text[position] == '\t')) { ++position; }
                } else if (escaped == 'x' || escaped == 'u' || escaped == 'U' || (escaped >= '0' && escaped <= '7')) {
                    return std::unexpected(error(ErrorCode::unsupported, "numeric list escapes are not implemented", position));
                } else {
                    switch (escaped) {
                    case 'a': value += '\a'; break; case 'b': value += '\b'; break;
                    case 'f': value += '\f'; break; case 'n': value += '\n'; break;
                    case 'r': value += '\r'; break; case 't': value += '\t'; break;
                    case 'v': value += '\v'; break; default: value += escaped; break;
                    }
                }
            } else { value += c; }
        }
        if (!closed) { return std::unexpected(error(ErrorCode::syntax, "unmatched list brace or quote", position)); }
        if ((braced || quoted) && position < text.size() && !space(text[position])) {
            return std::unexpected(error(ErrorCode::syntax, "characters after grouped list element", position));
        }
        elements.push_back(std::move(value));
    }
    return elements;
}
} // namespace mcutcl
