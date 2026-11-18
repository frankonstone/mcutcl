#pragma once
#include <mcutcl/interpreter.hpp>

namespace mcutcl {
struct RuntimeProfile final {
    std::string_view version;
    std::string_view language;
    std::span<const std::string_view> capabilities;
    [[nodiscard]] std::expected<void, Error>
    require(std::span<const std::string_view> identities) const;
};
// The identity comes from the linked library. Views have static lifetime.
[[nodiscard]] RuntimeProfile runtime_profile() noexcept;
}
