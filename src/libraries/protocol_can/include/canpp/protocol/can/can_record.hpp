#pragma once

#include "canpp/trace/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace canpp::protocol::can {

struct Frame {
    std::uint64_t timestamp_ns{};
    std::uint32_t stream_id{};
    std::uint32_t can_id{};
    trace::Direction direction{trace::Direction::unknown};
    std::uint8_t dlc{};
    bool extended{};
    bool fd{};
    bool bit_rate_switch{};
    std::string message_name;
    std::vector<std::uint8_t> data;
};

trace::Record encode(const Frame& frame);
std::optional<Frame> decode(const trace::Record& record);

} // namespace canpp::protocol::can
