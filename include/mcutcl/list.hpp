#pragma once
#include <mcutcl/interpreter.hpp>
namespace mcutcl {
// UTF-8 list values are data: no variable or command substitutions are performed.
// Initial list decoding supports grouping/basic escapes and rejects numeric escapes.
[[nodiscard]] Result make_list(std::span<const std::string> elements, Limits limits = {},
    RunControl* run = nullptr);
[[nodiscard]] std::expected<std::vector<std::string>, Error> parse_list(std::string_view value,
    Limits limits = {}, RunControl* run = nullptr);
}
