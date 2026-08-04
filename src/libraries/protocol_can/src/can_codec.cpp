#include "canpp/protocol/can/can_record.hpp"

#include <cstring>
#include <limits>

namespace canpp::protocol::can {
namespace {

#pragma pack(push, 1)
struct MetadataHeader {
    std::uint32_t can_id;
    std::uint16_t name_size;
    std::uint8_t dlc;
    std::uint8_t frame_flags;
};
#pragma pack(pop)

constexpr std::uint8_t extended_flag = 0x01U;
constexpr std::uint8_t fd_flag = 0x02U;
constexpr std::uint8_t brs_flag = 0x04U;

} // namespace

trace::Record encode(const Frame& frame) {
    trace::Record record;
    record.timestamp_ns = frame.timestamp_ns;
    record.stream_id = frame.stream_id;
    record.protocol = trace::ProtocolId::can;
    record.direction = frame.direction;
    record.payload = frame.data;

    const auto max_name = static_cast<std::size_t>(std::numeric_limits<std::uint16_t>::max());
    const auto name_size = static_cast<std::uint16_t>(std::min(frame.message_name.size(), max_name));
    MetadataHeader header{};
    header.can_id = frame.can_id;
    header.name_size = name_size;
    header.dlc = frame.dlc;
    header.frame_flags = static_cast<std::uint8_t>((frame.extended ? extended_flag : 0U) |
                                                   (frame.fd ? fd_flag : 0U) |
                                                   (frame.bit_rate_switch ? brs_flag : 0U));
    record.metadata.resize(sizeof(header) + name_size);
    std::memcpy(record.metadata.data(), &header, sizeof(header));
    std::memcpy(record.metadata.data() + sizeof(header), frame.message_name.data(), name_size);
    return record;
}

std::optional<Frame> decode(const trace::Record& record) {
    if (record.protocol != trace::ProtocolId::can || record.metadata.size() < sizeof(MetadataHeader)) {
        return std::nullopt;
    }
    MetadataHeader header{};
    std::memcpy(&header, record.metadata.data(), sizeof(header));
    if (record.metadata.size() != sizeof(header) + header.name_size || record.payload.size() > 64U) {
        return std::nullopt;
    }

    Frame frame;
    frame.timestamp_ns = record.timestamp_ns;
    frame.stream_id = record.stream_id;
    frame.can_id = header.can_id;
    frame.direction = record.direction;
    frame.dlc = header.dlc;
    frame.extended = (header.frame_flags & extended_flag) != 0U;
    frame.fd = (header.frame_flags & fd_flag) != 0U;
    frame.bit_rate_switch = (header.frame_flags & brs_flag) != 0U;
    frame.message_name.assign(
        reinterpret_cast<const char*>(record.metadata.data() + sizeof(header)), header.name_size);
    frame.data = record.payload;
    return frame;
}

} // namespace canpp::protocol::can
