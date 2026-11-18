#pragma once

#include <mcutcl/interpreter.hpp>

namespace mcutcl::detail {
std::expected<bool, Error> truth(std::string_view value);
bool boolean_literal(std::string_view value);
std::expected<void, Error> check_numeric_literal(std::string_view value);
Result expression_value(std::string value);
Result numeric_unary(std::string_view op, std::string_view value);
Result numeric_binary(std::string_view op, std::string_view left, std::string_view right);
Result math_function(std::string_view name, std::span<const std::string> values, RunControl& run);
} // namespace mcutcl::detail
