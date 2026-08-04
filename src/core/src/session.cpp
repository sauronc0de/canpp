#include "canpp/core/session.hpp"

#include "canpp/protocol/can/can_record.hpp"

#include <algorithm>
#include <iomanip>
#include <numeric>
#include <ostream>
#include <utility>

namespace canpp::core {

bool Session::open(const std::filesystem::path& path, std::string& error) {
    if (!reader_.open(path, error)) {
        return false;
    }
    reset();
    return true;
}

bool Session::import_can_asc(const std::filesystem::path& input,
                             const std::filesystem::path& output,
                             protocol::can::ImportStats& stats,
                             std::string& error) {
    if (!protocol::can::import_asc(input, output, stats, error)) {
        return false;
    }
    return open(output, error);
}

void Session::reset() {
    selection_.resize(static_cast<std::size_t>(reader_.size()));
    std::iota(selection_.begin(), selection_.end(), std::uint64_t{0});
}

template <typename Predicate>
bool Session::apply_filter(Predicate&& predicate, bool original, std::string& error) {
    if (!has_trace()) {
        error = "No communication trace is open";
        return false;
    }
    std::vector<std::uint64_t> result;
    const auto count = original ? reader_.size() : static_cast<std::uint64_t>(selection_.size());
    result.reserve(static_cast<std::size_t>(count / 4U + 1U));
    for (std::uint64_t row = 0; row < count; ++row) {
        const auto source_index = original ? row : selection_[static_cast<std::size_t>(row)];
        const auto record = reader_.read(source_index);
        if (!record) {
            error = "Cannot read record " + std::to_string(source_index);
            return false;
        }
        if (std::forward<Predicate>(predicate)(*record)) {
            result.push_back(source_index);
        }
    }
    selection_ = std::move(result);
    return true;
}

bool Session::filter_protocol(trace::ProtocolId protocol, bool original, std::string& error) {
    return apply_filter([protocol](const trace::Record& record) {
        return record.protocol == protocol;
    }, original, error);
}

bool Session::filter_direction(trace::Direction direction, bool original, std::string& error) {
    return apply_filter([direction](const trace::Record& record) {
        return record.direction == direction;
    }, original, error);
}

bool Session::filter_payload_byte(std::size_t offset,
                                  std::uint8_t value,
                                  bool original,
                                  std::string& error) {
    return apply_filter([offset, value](const trace::Record& record) {
        return offset < record.payload.size() && record.payload[offset] == value;
    }, original, error);
}

bool Session::filter_can_id(std::uint32_t id, bool original, std::string& error) {
    return apply_filter([id](const trace::Record& record) {
        const auto frame = protocol::can::decode(record);
        return frame && frame->can_id == id;
    }, original, error);
}

bool Session::filter_can_name(const std::string& name, bool original, std::string& error) {
    return apply_filter([&name](const trace::Record& record) {
        const auto frame = protocol::can::decode(record);
        return frame && frame->message_name == name;
    }, original, error);
}

bool Session::save(const std::filesystem::path& output, std::string& error) const {
    if (!has_trace()) {
        error = "No communication trace is open";
        return false;
    }
    return trace::write_selection(reader_, selection_, output, error);
}

void Session::print(std::ostream& output, std::size_t limit, std::size_t offset) const {
    const auto end = std::min(selection_.size(), offset + limit);
    for (std::size_t row = offset; row < end; ++row) {
        const auto record = reader_.read(selection_[row]);
        if (!record) {
            continue;
        }
        if (const auto frame = protocol::can::decode(*record)) {
            output << std::fixed << std::setprecision(6)
                   << static_cast<double>(frame->timestamp_ns) / 1'000'000'000.0 << " CAN"
                   << (frame->fd ? "FD " : " ") << frame->stream_id << ' '
                   << (frame->direction == trace::Direction::rx ? "Rx " : "Tx ")
                   << std::hex << std::uppercase << frame->can_id
                   << (frame->extended ? "x " : " ") << std::dec
                   << frame->message_name << " [" << frame->data.size() << ']';
            for (const auto byte : frame->data) {
                output << ' ' << std::hex << std::setw(2) << std::setfill('0')
                       << static_cast<unsigned>(byte);
            }
            output << std::dec << std::setfill(' ') << '\n';
        } else {
            output << std::fixed << std::setprecision(6)
                   << static_cast<double>(record->timestamp_ns) / 1'000'000'000.0
                   << " protocol=" << static_cast<unsigned>(record->protocol)
                   << " stream=" << record->stream_id
                   << " bytes=" << record->payload.size() << '\n';
        }
    }
}

void Session::status(std::ostream& output) const {
    if (!has_trace()) {
        output << "No communication trace open\n";
        return;
    }
    output << "File: " << reader_.path() << '\n'
           << "Records: " << reader_.size() << '\n'
           << "Current selection: " << selection_.size() << '\n';
}

bool Session::has_trace() const { return reader_.is_open(); }
std::size_t Session::selection_size() const { return selection_.size(); }

} // namespace canpp::core
