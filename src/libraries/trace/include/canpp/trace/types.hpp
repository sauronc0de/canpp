#pragma once

#include <cstdint>
#include <vector>

namespace canpp::trace {

enum class ProtocolId : std::uint16_t {
    unknown = 0,
    can = 1,
    uart = 2,
    spi = 3,
    i2c = 4,
    lin = 5,
    ethernet = 6,
    custom = 0x8000
};

enum class Direction : std::uint8_t {
    unknown = 0,
    rx = 1,
    tx = 2,
    bidirectional = 3
};

struct Record {
    std::uint64_t timestamp_ns{};
    std::uint32_t stream_id{};
    ProtocolId protocol{ProtocolId::unknown};
    Direction direction{Direction::unknown};
    std::uint16_t flags{};
    std::vector<std::uint8_t> metadata;
    std::vector<std::uint8_t> payload;
};

} // namespace canpp::trace
