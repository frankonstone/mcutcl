#include "evaluator.hpp"
#include "numeric.hpp"
#include <mcutcl/list.hpp>

namespace mcutcl::detail {
namespace {
std::unexpected<Error> invalid(std::string message) {
    return std::unexpected(Error{ErrorCode::wrong_arguments, std::move(message), {}});
}
}

// @req REQ-0007 REQ-0008 REQ-0021 REQ-0022
CompletionResult Evaluator::conditional(Words words, std::string_view source, std::size_t depth) {
    std::optional<std::string_view> selected;
    std::size_t pos = 1;
    for (;;) {
        if (pos >= words.size()) {
            return invalid("missing if condition");
        }
        const auto& condition = words[pos++];
        if (pos < words.size() && words[pos] == "then") {
            ++pos;
        }
        if (pos >= words.size()) {
            return invalid("missing if body");
        }
        const auto& body = words[pos++];
        if (!selected) {
            auto result = expression(condition, source, depth);
            if (!result || result->code != CompletionCode::ok) {
                return result;
            }
            auto test = truth(result->value);
            if (!test) {
                return std::unexpected(test.error());
            }
            if (*test) {
                selected = body;
            }
        }
        if (pos == words.size()) {
            break;
        }
        if (words[pos] == "elseif") {
            ++pos;
            continue;
        }
        if (words[pos] == "else") {
            ++pos;
        }
        if (words.size() - pos != 1) {
            return invalid("expected a single else body");
        }
        if (!selected) {
            selected = words[pos];
        }
        break;
    }
    return selected ? script(*selected, std::string(source) + "::<if>", depth)
                    : CompletionResult{Completion{}};
}

// @req REQ-0007 REQ-0008 REQ-0021 REQ-0022
CompletionResult Evaluator::loop(Interpreter::Builtin kind, Words words, std::string_view source,
                                 std::size_t depth) {
    const bool counting = kind == Interpreter::Builtin::foreach_loop;
    if (words.size() != (counting ? 4U : 3U)) {
        return invalid(counting ? "expected foreach varName list body"
                                : "expected while test body");
    }
    std::vector<std::string> elements;
    std::string variable;
    if (counting) {
        auto names = parse_list(words[1], limits(), &run_);
        if (!names) {
            return std::unexpected(names.error());
        }
        if (names->size() != 1) {
            return std::unexpected(
                Error{ErrorCode::unsupported, "foreach requires one scalar variable", {}});
        }
        variable = std::move(names->front());
        auto list = parse_list(words[2], limits(), &run_);
        if (!list) {
            return std::unexpected(list.error());
        }
        elements = std::move(*list);
    }
    const std::string label = std::string(source) + (counting ? "::<foreach>" : "::<while>");
    for (std::size_t index = 0;; ++index) {
        if (counting) {
            if (index == elements.size()) {
                break;
            }
        } else {
            auto result = expression(words[1], source, depth);
            if (!result || result->code != CompletionCode::ok) {
                return result;
            }
            auto test = truth(result->value);
            if (!test) {
                return std::unexpected(test.error());
            }
            if (!*test) {
                break;
            }
        }
        // Each entered iteration consumes a command unit, including an empty body.
        if (auto paid = charge_command(); !paid) {
            return std::unexpected(paid.error());
        }
        if (counting) {
            if (auto assigned = interpreter_.set_variable(variable, std::move(elements[index]));
                !assigned) {
                return std::unexpected(assigned.error());
            }
        }
        auto result = script(words.back(), label, depth);
        if (!result || result->code == CompletionCode::returned) {
            return result;
        }
        if (result->code == CompletionCode::break_loop) {
            break;
        }
    }
    return Completion{};
}
} // namespace mcutcl::detail
