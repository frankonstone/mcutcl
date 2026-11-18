#include "numeric.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>

namespace mcutcl::detail {
namespace {
using U = std::uint64_t;
constexpr U largest = std::numeric_limits<U>::max();
// Sign and magnitude cover the whole unsigned device range and signed timestamps.
struct Integer {
    U magnitude{};
    bool negative{};
};
using Number = std::variant<Integer, double>;
template <class T> using Checked = std::expected<T, Error>;

std::unexpected<Error> reject(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message), {}});
}
std::unexpected<Error> growth() {
    return reject(ErrorCode::limit, "integer magnitude exceeds 64 bits");
}
Integer integer(U magnitude, bool negative = false) {
    return {magnitude, negative && magnitude != 0};
}
std::string_view trim(std::string_view s) {
    constexpr std::string_view spaces = " \t\n\r\f\v";
    const auto first = s.find_first_not_of(spaces);
    return first == s.npos ? std::string_view{}
                           : s.substr(first, s.find_last_not_of(spaces) - first + 1);
}
bool same_ascii(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char c = a[i];
        if ((c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c) != b[i]) {
            return false;
        }
    }
    return true;
}
std::optional<bool> boolean_word(std::string_view word) {
    if (word.empty()) {
        return {};
    }
    for (std::string_view name : {"true", "false", "yes", "no", "on", "off"}) {
        if (word.size() <= name.size() && !(word.size() == 1 && name.front() == 'o') &&
            same_ascii(word, name.substr(0, word.size()))) {
            return name == "true" || name == "yes" || name == "on";
        }
    }
    return {};
}
Checked<std::optional<Number>> read_number(std::string_view raw) {
    auto s = trim(raw);
    const bool negative = s.starts_with('-');
    if (negative || s.starts_with('+')) {
        s.remove_prefix(1);
    }
    if (s.empty()) {
        return std::nullopt;
    }
    if (same_ascii(s, "inf") || same_ascii(s, "infinity")) {
        return Number{
            std::copysign(std::numeric_limits<double>::infinity(), negative ? -1.0 : 1.0)};
    }
    if (same_ascii(s, "nan")) {
        return Number{std::numeric_limits<double>::quiet_NaN()};
    }
    if ((s.front() < '0' || s.front() > '9') && s.front() != '.') {
        return std::nullopt;
    }
    int base = 10;
    bool prefixed = false;
    if (s.size() >= 2 && s[0] == '0') {
        switch (s[1]) {
        case 'x':
        case 'X':
            base = 16;
            break;
        case 'o':
        case 'O':
            base = 8;
            break;
        case 'b':
        case 'B':
            base = 2;
            break;
        case 'd':
        case 'D':
            break;
        default:
            break;
        }
        prefixed = base != 10 || s[1] == 'd' || s[1] == 'D';
        if (prefixed) {
            s.remove_prefix(2);
        }
    }
    const bool floating = !prefixed && s.find_first_of(".eE") != s.npos;
    if (s.empty()) {
        return std::nullopt;
    }
    std::string digits;
    if (s.find('_') != s.npos) {
        const auto is_digit = [&](char c) {
            return (c >= '0' && c <= '9') ||
                   (base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
        };
        digits.reserve(s.size());
        for (std::size_t i = 0; i < s.size();) {
            if (s[i] != '_') {
                digits.push_back(s[i++]);
                continue;
            }
            if (digits.empty() || !is_digit(digits.back())) {
                return std::nullopt;
            }
            while (i < s.size() && s[i] == '_') {
                ++i;
            }
            if (i == s.size() || !is_digit(s[i])) {
                return std::nullopt;
            }
        }
        s = digits;
    }
    const auto* last = s.data() + s.size();
    if (floating) {
        double d{};
        auto [end, ec] = std::from_chars(s.data(), last, d, std::chars_format::general);
        if (end != last || ec == std::errc::invalid_argument) {
            return std::nullopt;
        }
        if (ec != std::errc{}) {
            return reject(ErrorCode::limit,
                          "decimal floating literal outside binary64 conversion range");
        }
        return Number{negative ? -d : d};
    }
    U value{};
    auto [end, ec] = std::from_chars(s.data(), last, value, base);
    if (end != last || ec == std::errc::invalid_argument) {
        return std::nullopt;
    }
    if (ec != std::errc{}) {
        return growth();
    }
    return Number{integer(value, negative)};
}
Checked<Number> require_number(std::string_view value) {
    auto n = read_number(value);
    if (!n) {
        return std::unexpected(n.error());
    }
    if (!*n) {
        return reject(ErrorCode::script, "expected a numeric operand");
    }
    return **n;
}
double real(Number n) {
    if (const auto* i = std::get_if<Integer>(&n)) {
        const double magnitude = static_cast<double>(i->magnitude);
        return i->negative ? -magnitude : magnitude;
    }
    return std::get<double>(n);
}
bool nan(Number n) {
    const auto* d = std::get_if<double>(&n);
    return d && std::isnan(*d);
}
Result render(Number n) {
    if (const auto* i = std::get_if<Integer>(&n)) {
        return (i->negative ? "-" : "") + std::to_string(i->magnitude);
    }
    const double d = std::get<double>(n);
    if (std::isnan(d)) {
        return reject(ErrorCode::script, "floating-point domain error");
    }
    if (std::isinf(d)) {
        return std::string(d < 0 ? "-Inf" : "Inf");
    }
    // Start with the shortest round-trippable significand, then use Tcl's decimal
    // interval and exponent spelling independently of the process locale.
    char buffer[48];
    auto converted =
        std::to_chars(buffer, buffer + sizeof(buffer), std::abs(d), std::chars_format::scientific);
    if (converted.ec != std::errc{}) {
        return reject(ErrorCode::script, "cannot format double");
    }
    const std::string_view scientific(buffer, converted.ptr);
    const auto marker = scientific.find('e');
    const auto power = scientific.substr(marker + 1);
    int exponent{};
    std::from_chars(power.data() + (power.front() == '+'), power.data() + power.size(), exponent);
    std::string result = std::signbit(d) ? "-" : "";
    if (exponent < -4 || exponent > 16) {
        result += scientific.substr(0, marker);
        result += exponent < 0 ? "e" : "e+";
        return result + std::to_string(exponent);
    }
    std::string digits(scientific.substr(0, marker));
    if (digits.size() > 1) {
        digits.erase(1, 1);
    }
    if (exponent < 0) {
        result += "0.";
        result.append(static_cast<std::size_t>(-exponent - 1), '0');
        return result + digits;
    }
    const auto before_point = static_cast<std::size_t>(exponent + 1);
    if (before_point >= digits.size()) {
        result += digits;
        result.append(before_point - digits.size(), '0');
        return result + ".0";
    }
    digits.insert(before_point, 1, '.');
    return result + digits;
}
template <class T> int ordering(T a, T b) { return (a > b) - (a < b); }
int compare_integer(Integer a, Integer b) {
    if (a.negative != b.negative) {
        return a.negative ? -1 : 1;
    }
    const int magnitude = ordering(a.magnitude, b.magnitude);
    return a.negative ? -magnitude : magnitude;
}
// Compare the integer exactly, even when its neighbor has the same binary64 rounding.
int compare_mixed(Integer i, double d) {
    if (i.negative != (d < 0)) {
        return i.negative ? -1 : 1;
    }
    double magnitude = std::abs(d);
    int cmp = -1;
    if (magnitude < 0x1p64) {
        const U truncated = static_cast<U>(magnitude);
        cmp = ordering(i.magnitude, truncated);
        if (!cmp && std::trunc(magnitude) != magnitude) {
            cmp = -1;
        }
    }
    return i.negative ? -cmp : cmp;
}
int compare(Number a, Number b) {
    const auto* x = std::get_if<Integer>(&a);
    const auto* y = std::get_if<Integer>(&b);
    if (x && y) {
        return compare_integer(*x, *y);
    }
    if (x) {
        return compare_mixed(*x, std::get<double>(b));
    }
    if (y) {
        return -compare_mixed(*y, std::get<double>(a));
    }
    return ordering(std::get<double>(a), std::get<double>(b));
}
bool comparison(std::string_view op, int cmp) {
    if (op == "==" || op == "eq") {
        return cmp == 0;
    }
    if (op == "!=" || op == "ne") {
        return cmp != 0;
    }
    if (op == "<" || op == "lt") {
        return cmp < 0;
    }
    if (op == ">" || op == "gt") {
        return cmp > 0;
    }
    if (op == "<=" || op == "le") {
        return cmp <= 0;
    }
    return cmp >= 0;
}
std::string flag(bool value) { return value ? "1" : "0"; }
U bits(Integer n) { return n.negative ? U{0} - n.magnitude : n.magnitude; }
Checked<Integer> from_bits(U low, bool negative) {
    if (negative && low == 0) {
        return growth();
    }
    return integer(negative ? U{0} - low : low, negative);
}
Checked<Integer> add(Integer a, Integer b) {
    if (a.negative == b.negative) {
        if (b.magnitude > largest - a.magnitude) {
            return growth();
        }
        return integer(a.magnitude + b.magnitude, a.negative);
    }
    if (a.magnitude >= b.magnitude) {
        return integer(a.magnitude - b.magnitude, a.negative);
    }
    return integer(b.magnitude - a.magnitude, b.negative);
}
Checked<Integer> multiply(Integer a, Integer b) {
    if (b.magnitude && a.magnitude > largest / b.magnitude) {
        return growth();
    }
    return integer(a.magnitude * b.magnitude, a.negative != b.negative);
}
Checked<Integer> arithmetic(std::string_view op, Integer a, Integer b) {
    if (op == "+") {
        return add(a, b);
    }
    if (op == "-") {
        return add(a, integer(b.magnitude, !b.negative));
    }
    if (op == "*") {
        return multiply(a, b);
    }
    if (op == "&") {
        return from_bits(bits(a) & bits(b), a.negative && b.negative);
    }
    if (op == "|") {
        return from_bits(bits(a) | bits(b), a.negative || b.negative);
    }
    if (op == "^") {
        return from_bits(bits(a) ^ bits(b), a.negative != b.negative);
    }
    if (op == "<<" || op == ">>") {
        if (b.negative) {
            return reject(ErrorCode::script, "negative shift count");
        }
        if (!a.magnitude) {
            return a;
        }
        if (op == "<<") {
            if (b.magnitude >= 64 || a.magnitude > (largest >> b.magnitude)) {
                return growth();
            }
            return integer(a.magnitude << b.magnitude, a.negative);
        }
        if (b.magnitude >= 64) {
            return integer(a.negative ? 1 : 0, a.negative);
        }
        const U quotient = a.magnitude >> b.magnitude;
        const bool remainder = (quotient << b.magnitude) != a.magnitude;
        return integer(quotient + (a.negative && remainder), a.negative);
    }
    if (op == "**") {
        if (b.negative) {
            if (!a.magnitude) {
                return reject(ErrorCode::script, "zero to a negative power");
            }
            return integer(a.magnitude == 1, a.negative && (b.magnitude & 1));
        }
        Integer product{1};
        for (U exponent = b.magnitude; exponent; exponent >>= 1) {
            if (exponent & 1) {
                auto next = multiply(product, a);
                if (!next) {
                    return next;
                }
                product = *next;
            }
            if (exponent > 1) {
                auto square = multiply(a, a);
                if (!square) {
                    return square;
                }
                a = *square;
            }
        }
        return product;
    }
    if (op != "/" && op != "%") {
        return reject(ErrorCode::unsupported, "unsupported integer operator");
    }
    if (!b.magnitude) {
        return reject(ErrorCode::script, "integer division by zero");
    }
    const U remainder = a.magnitude % b.magnitude;
    const bool adjust = remainder && a.negative != b.negative;
    if (op == "%") {
        return integer(adjust ? b.magnitude - remainder : remainder, b.negative);
    }
    return integer(a.magnitude / b.magnitude + adjust, a.negative != b.negative);
}
Checked<Integer> to_integer(Number n, bool round) {
    if (const auto* i = std::get_if<Integer>(&n)) {
        return *i;
    }
    const double d = round ? std::round(std::get<double>(n)) : std::trunc(std::get<double>(n));
    if (!std::isfinite(d)) {
        return reject(ErrorCode::script, "cannot convert non-finite value to integer");
    }
    if (std::abs(d) >= 0x1p64) {
        return growth();
    }
    return integer(static_cast<U>(std::abs(d)), d < 0);
}
} // namespace

// @req REQ-0006 REQ-0008 REQ-0021
bool boolean_literal(std::string_view v) { return boolean_word(v).has_value(); }
Checked<void> check_numeric_literal(std::string_view value) {
    auto n = read_number(value);
    if (!n) {
        return std::unexpected(n.error());
    }
    if (!*n) {
        return reject(ErrorCode::syntax, "invalid numeric literal");
    }
    return {};
}
Checked<bool> truth(std::string_view value) {
    auto n = read_number(value);
    if (!n) {
        return std::unexpected(n.error());
    }
    if (*n && !nan(**n)) {
        return real(**n) != 0;
    }
    if (auto b = boolean_word(value)) {
        return *b;
    }
    return reject(ErrorCode::script, "expected boolean value");
}
Result expression_value(std::string value) {
    auto n = read_number(value);
    if (!n) {
        return std::unexpected(n.error());
    }
    if (*n) {
        return render(**n);
    }
    return value;
}
Result numeric_unary(std::string_view op, std::string_view value) {
    if (op == "!") {
        auto b = truth(value);
        if (!b) {
            return std::unexpected(b.error());
        }
        return flag(!*b);
    }
    auto n = require_number(value);
    if (!n) {
        return std::unexpected(n.error());
    }
    if (op == "+") {
        return render(*n);
    }
    const auto* i = std::get_if<Integer>(&*n);
    if (op == "-") {
        return i ? render(integer(i->magnitude, !i->negative)) : render(-real(*n));
    }
    if (!i) {
        return reject(ErrorCode::script, "bitwise complement requires an integer");
    }
    auto result = from_bits(~bits(*i), !i->negative);
    if (!result) {
        return std::unexpected(result.error());
    }
    return render(*result);
}
Result numeric_binary(std::string_view op, std::string_view left, std::string_view right) {
    if (op == "eq" || op == "ne" || op == "lt" || op == "le" || op == "gt" || op == "ge") {
        return flag(comparison(op, left.compare(right)));
    }
    auto x = read_number(left), y = read_number(right);
    if (!x) {
        return std::unexpected(x.error());
    }
    if (!y) {
        return std::unexpected(y.error());
    }
    if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
        if (!*x || !*y) {
            return flag(comparison(op, left.compare(right)));
        }
        if (nan(**x) || nan(**y)) {
            return flag(op == "!=");
        }
        return flag(comparison(op, compare(**x, **y)));
    }
    if (!*x || !*y) {
        return reject(ErrorCode::script, "expected numeric operands");
    }
    const auto* a = std::get_if<Integer>(&**x);
    const auto* b = std::get_if<Integer>(&**y);
    if (a && b) {
        auto result = arithmetic(op, *a, *b);
        if (!result) {
            return std::unexpected(result.error());
        }
        return render(*result);
    }
    double l = real(**x), r = real(**y);
    if (op == "+") {
        return render(l + r);
    }
    if (op == "-") {
        return render(l - r);
    }
    if (op == "*") {
        return render(l * r);
    }
    if (op == "/") {
        return render(l / r);
    }
    if (op == "**") {
        return render(std::pow(l, r));
    }
    return reject(ErrorCode::script, "operator requires integer operands");
}
Result math_function(std::string_view name, std::span<const std::string> args, RunControl& run) {
    bool extreme = name == "min" || name == "max";
    if (!(extreme || name == "abs" || name == "double" || name == "wide" || name == "round" ||
          name == "floor" || name == "ceil" || name == "sqrt" || name == "exp")) {
        return reject(ErrorCode::unsupported, "unsupported math function: " + std::string(name));
    }
    if (args.empty() || (!extreme && args.size() != 1)) {
        return reject(ErrorCode::wrong_arguments, "wrong number of math function arguments");
    }
    auto n = require_number(args.front());
    if (!n) {
        return std::unexpected(n.error());
    }
    if (nan(*n)) {
        return reject(ErrorCode::script, "floating-point domain error");
    }
    if (extreme) {
        for (const auto& arg : args.subspan(1)) {
            if (auto paid = run.consume_work(arg.size() + 1); !paid) {
                return std::unexpected(paid.error());
            }
            auto next = require_number(arg);
            if (!next) {
                return std::unexpected(next.error());
            }
            if (nan(*next)) {
                return reject(ErrorCode::script, "floating-point domain error");
            }
            int cmp = compare(*next, *n);
            if ((name == "min" && cmp < 0) || (name == "max" && cmp > 0)) {
                n = next;
            }
        }
        return render(*n);
    }
    if (name == "round" || name == "wide") {
        auto i = to_integer(*n, name == "round");
        if (!i) {
            return std::unexpected(i.error());
        }
        if (name == "round") {
            return render(*i);
        }
        const U low = bits(*i);
        return render(integer((low >> 63) ? U{0} - low : low, low >> 63));
    }
    if (name == "abs") {
        if (const auto* i = std::get_if<Integer>(&*n)) {
            return render(integer(i->magnitude));
        }
        return render(std::abs(real(*n)));
    }
    const double d = real(*n);
    if (name == "floor") {
        return render(std::floor(d));
    }
    if (name == "ceil") {
        return render(std::ceil(d));
    }
    if (name == "sqrt") {
        return render(std::sqrt(d));
    }
    if (name == "exp") {
        return render(std::exp(d));
    }
    return render(d);
}
} // namespace mcutcl::detail
