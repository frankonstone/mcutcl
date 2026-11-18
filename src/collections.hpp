#pragma once

#include <mcutcl/interpreter.hpp>
#include <cstdint>

namespace mcutcl::detail {
std::expected<std::int64_t, Error> list_index(std::string_view text);
}
