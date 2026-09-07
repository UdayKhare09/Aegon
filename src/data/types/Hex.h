#pragma once

#include <cstdint>
#include <cstddef>
#include <array>

namespace aegon::data::types::detail {

inline constexpr char HEX_DIGITS_LOWER[16] = {
    '0', '1', '2', '3', '4', '5', '6', '7',
    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
};

constexpr auto make_hex_decode_table() {
    std::array<uint8_t, 256> table{};
    table.fill(0xFF);
    for (uint8_t i = 0; i <= 9; ++i) {
        table[static_cast<size_t>('0' + i)] = i;
    }
    for (uint8_t i = 0; i < 6; ++i) {
        table[static_cast<size_t>('a' + i)] = static_cast<uint8_t>(10 + i);
        table[static_cast<size_t>('A' + i)] = static_cast<uint8_t>(10 + i);
    }
    return table;
}

inline constexpr auto HEX_DECODE_TABLE = make_hex_decode_table();

} // namespace aegon::data::types::detail
