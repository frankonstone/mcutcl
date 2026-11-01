#pragma once
#include <string_view>
namespace mcutcl::detail {
inline bool utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80U) { continue; }
        unsigned count{};
        unsigned value{};
        unsigned minimum{};
        if (first >= 0xc2U && first <= 0xdfU) { count = 1; value = first & 31U; minimum = 0x80; }
        else if (first >= 0xe0U && first <= 0xefU) { count = 2; value = first & 15U; minimum = 0x800; }
        else if (first >= 0xf0U && first <= 0xf4U) { count = 3; value = first & 7U; minimum = 0x10000; }
        else { return false; }
        if (count > text.size() - i) { return false; }
        for (unsigned n = 0; n < count; ++n) {
            const auto c = static_cast<unsigned char>(text[i++]);
            if ((c & 0xc0U) != 0x80U) { return false; }
            value = (value << 6U) | (c & 63U);
        }
        if (value < minimum || value > 0x10ffffU || (value >= 0xd800U && value <= 0xdfffU)) { return false; }
    }
    return true;
}
}
