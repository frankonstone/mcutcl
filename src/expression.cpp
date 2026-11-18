#include "evaluator.hpp"
#include "numeric.hpp"
#include "poll.hpp"

namespace mcutcl::detail {
namespace {
constexpr bool digit(char c) { return c >= '0' && c <= '9'; }
constexpr bool letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
struct Operator {
    std::string_view name;
    int precedence;
};
constexpr Operator operators[] = {{"**", 14}, {"<<", 11}, {">>", 11}, {"<=", 10}, {">=", 10},
                                  {"lt", 10}, {"le", 10}, {"gt", 10}, {"ge", 10}, {"==", 9},
                                  {"!=", 9},  {"eq", 9},  {"ne", 9},  {"&&", 4},  {"||", 3},
                                  {"*", 13},  {"/", 13},  {"%", 13},  {"+", 12},  {"-", 12},
                                  {"<", 10},  {">", 10},  {"&", 8},   {"^", 7},   {"|", 6}};
bool normal(const CompletionResult& result) { return result && result->code == CompletionCode::ok; }
}

// Parse the whole expression once before allowing substitutions, then use the same
// grammar with an evaluation flag to short-circuit unselected operands.
class ExpressionParser final {
public:
    ExpressionParser(Evaluator& owner, std::string_view text, std::string_view source,
                     std::size_t depth)
        : owner_(owner), text_(text), source_(source), depth_(depth), poll_(&owner.run_) {}
    CompletionResult run() {
        if (text_.size() > owner_.limits().source_bytes) {
            return error(ErrorCode::limit, "expression source limit exceeded");
        }
        for (bool active : {false, true}) {
            position_ = 0;
            auto charged = owner_.run_.consume_work(text_.size());
            if (!charged) {
                return std::unexpected(charged.error());
            }
            auto result = parse(1, depth_, active);
            if (!normal(result)) {
                return located(std::move(result), position_);
            }
            spaces();
            if (stopped_) {
                return error(ErrorCode::cancelled, "expression cancelled");
            }
            if (position_ != text_.size()) {
                return error(ErrorCode::syntax, "unexpected token in expression");
            }
            if (active) {
                return located(as_completion(expression_value(std::move(result->value))),
                               position_);
            }
        }
        return Completion{};
    }

private:
    Evaluator& owner_;
    std::string_view text_, source_;
    std::size_t depth_, position_{};
    BlockPoll poll_;
    bool stopped_{};
    std::unexpected<Error> error_at(ErrorCode code, std::string message, std::size_t offset) const {
        Error e{code, std::move(message), std::string(source_), offset};
        for (std::size_t i = 0; i < offset; ++i) {
            if (text_[i] == '\n') {
                ++e.line;
                e.column = 1;
            } else {
                ++e.column;
            }
        }
        return std::unexpected(std::move(e));
    }
    std::unexpected<Error> error(ErrorCode code, std::string message) const {
        return error_at(code, std::move(message), position_);
    }
    CompletionResult located(CompletionResult result, std::size_t at) const {
        if (!result && result.error().source.empty()) {
            return error_at(result.error().code, std::move(result.error().message), at);
        }
        return result;
    }
    void spaces() {
        scan([](char c) {
            return std::string_view(" \t\r\n\f\v").find(c) != std::string_view::npos;
        });
    }
    template <class Predicate> void scan(Predicate accepts) {
        while (!stopped_ && position_ < text_.size() && accepts(text_[position_])) {
            stopped_ = poll_.cancelled(position_);
            if (!stopped_) {
                ++position_;
            }
        }
    }
    char peek() const { return position_ < text_.size() ? text_[position_] : '\0'; }
    bool take(char c) {
        spaces();
        if (peek() != c) {
            return false;
        }
        ++position_;
        return true;
    }
    const Operator* next_operator() {
        spaces();
        for (const auto& op : operators) {
            if (!text_.substr(position_).starts_with(op.name)) {
                continue;
            }
            const auto end = position_ + op.name.size();
            if (letter(op.name.front()) && end < text_.size() && letter(text_[end])) {
                continue;
            }
            return &op;
        }
        return nullptr;
    }
    CompletionResult parse(int minimum, std::size_t depth, bool active) {
        if (depth > owner_.depth_limit_) {
            return error(ErrorCode::limit, "expression nesting limit exceeded");
        }
        auto paid = owner_.run_.consume_work(1);
        if (!paid) {
            return std::unexpected(paid.error());
        }
        auto left = primary(depth, active);
        if (!normal(left)) {
            return left;
        }
        while (const auto* op = next_operator()) {
            if (op->precedence < minimum) {
                break;
            }
            const auto at = position_;
            position_ += op->name.size();
            const bool logical = op->name == "&&" || op->name == "||";
            bool need_right = active;
            if (active && logical) {
                auto test = truth(left->value);
                if (!test) {
                    return std::unexpected(test.error());
                }
                need_right = op->name == "&&" ? *test : !*test;
                left->value = *test ? "1" : "0";
            }
            auto right = parse(op->precedence + (op->name != "**"), depth + 1, need_right);
            if (!normal(right)) {
                return right;
            }
            if (need_right) {
                if (logical) {
                    auto test = truth(right->value);
                    if (!test) {
                        return std::unexpected(test.error());
                    }
                    left->value = *test ? "1" : "0";
                } else {
                    for (auto* operand : {&left->value, &right->value}) {
                        if (auto work = owner_.run_.consume_work(operand->size()); !work) {
                            return std::unexpected(work.error());
                        }
                    }
                    left = located(
                        as_completion(numeric_binary(op->name, left->value, right->value)), at);
                    if (!normal(left)) {
                        return left;
                    }
                }
            }
        }
        if (minimum == 1 && take('?')) {
            bool select = false;
            if (active) {
                auto test = truth(left->value);
                if (!test) {
                    return std::unexpected(test.error());
                }
                select = *test;
            }
            auto yes = parse(1, depth + 1, active && select);
            if (!normal(yes)) {
                return yes;
            }
            if (!take(':')) {
                return error(ErrorCode::syntax, "missing colon in conditional expression");
            }
            auto no = parse(1, depth + 1, active && !select);
            if (!normal(no)) {
                return no;
            }
            return select ? std::move(yes) : std::move(no);
        }
        return left;
    }
    CompletionResult primary(std::size_t depth, bool active) {
        spaces();
        if (stopped_) {
            return error(ErrorCode::cancelled, "expression cancelled");
        }
        const char c = peek();
        if (c == '+' || c == '-' || c == '!' || c == '~') {
            const auto op = text_.substr(position_++, 1);
            auto value = parse(15, depth + 1, active);
            if (!normal(value) || !active) {
                return value;
            }
            return as_completion(numeric_unary(op, value->value));
        }
        if (take('(')) {
            auto value = parse(1, depth + 1, active);
            if (!normal(value)) {
                return value;
            }
            if (!take(')')) {
                return error(ErrorCode::syntax, "missing closing parenthesis");
            }
            return value;
        }
        if (c == '$' || c == '[' || c == '"' || c == '{') {
            return owner_.expression_operand(text_, position_, source_, depth, active);
        }
        const auto begin = position_;
        if (letter(c)) {
            scan([](char c) { return letter(c) || digit(c); });
            if (stopped_) {
                return error(ErrorCode::cancelled, "expression cancelled");
            }
            auto word = text_.substr(begin, position_ - begin);
            if (take('(')) {
                return function(word, depth, active);
            }
            if (!boolean_literal(word)) {
                if (auto valid = check_numeric_literal(word); !valid) {
                    return error(ErrorCode::syntax, "invalid expression bareword");
                }
            }
            return Completion{CompletionCode::ok, active ? std::string(word) : std::string{}};
        }
        if (digit(c) || c == '.') {
            const auto digits = [&](bool hex = false) {
                scan([hex](char c) {
                    return digit(c) || c == '_' ||
                           (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
                });
            };
            const auto prefix = text_.substr(begin, 2);
            if (prefix.size() == 2 && c == '0' &&
                std::string_view("xXbBoOdD").find(prefix[1]) != text_.npos) {
                position_ += 2;
                digits(prefix[1] == 'x' || prefix[1] == 'X');
            } else {
                digits();
                if (peek() == '.') {
                    ++position_;
                    digits();
                }
                if ((peek() == 'e' || peek() == 'E') &&
                    !text_.substr(position_).starts_with("eq")) {
                    ++position_;
                    if (peek() == '-' || peek() == '+') {
                        ++position_;
                    }
                    digits();
                }
            }
            auto literal = text_.substr(begin, position_ - begin);
            if (stopped_) {
                return error(ErrorCode::cancelled, "expression cancelled");
            }
            auto valid = check_numeric_literal(literal);
            if (!valid && (active || valid.error().code != ErrorCode::limit)) {
                return error(valid.error().code, valid.error().message);
            }
            return Completion{CompletionCode::ok, active ? std::string(literal) : std::string{}};
        }
        return error(ErrorCode::syntax, "expected an expression operand");
    }
    CompletionResult function(std::string_view name, std::size_t depth, bool active) {
        std::vector<std::string> values;
        std::size_t count = 0, available = owner_.limits().argument_bytes;
        if (!take(')')) {
            do {
                if (++count > owner_.limits().words) {
                    return error(ErrorCode::limit, "math function argument limit exceeded");
                }
                auto arg = parse(1, depth + 1, active);
                if (!normal(arg)) {
                    return arg;
                }
                if (active) {
                    if (!take_bytes(available, arg->value.size())) {
                        return error(ErrorCode::limit, "math function byte limit exceeded");
                    }
                    values.push_back(std::move(arg->value));
                }
            } while (take(','));
            if (!take(')')) {
                return error(ErrorCode::syntax, "missing closing function parenthesis");
            }
        }
        return active ? as_completion(math_function(name, values, owner_.run_))
                      : CompletionResult{Completion{}};
    }
};

// @req REQ-0006 REQ-0007 REQ-0008 REQ-0018 REQ-0021 REQ-0022
CompletionResult Evaluator::expression(std::string_view text, std::string_view source,
                                       std::size_t depth) {
    const auto label = std::string(source) + "::<expr>";
    ExpressionParser parser{*this, text, label, depth};
    return parser.run();
}
} // namespace mcutcl::detail
