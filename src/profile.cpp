#include <mcutcl/profile.hpp>
#include <algorithm>

namespace mcutcl {
// @req REQ-0019 REQ-0020 REQ-0024
RuntimeProfile runtime_profile() noexcept {
    static constexpr std::string_view capabilities[] = {"mcutcl.invoke/1", "mcutcl.run-control/1",
                                                        "mcutcl.catch/1", "mcutcl.fixed-proc/1",
                                                        "mcutcl.automation/1"};
    return {MCUTCL_VERSION, "mcutcl.tcl9-bootstrap/" MCUTCL_VERSION, capabilities};
}

std::expected<void, Error>
RuntimeProfile::require(std::span<const std::string_view> identities) const {
    for (const auto id : identities) {
        if (id != language && std::ranges::find(capabilities, id) == capabilities.end()) {
            return std::unexpected(
                Error{ErrorCode::unsupported,
                      std::string(language) + " does not provide " + std::string(id),
                      {}});
        }
    }
    return {};
}
}
