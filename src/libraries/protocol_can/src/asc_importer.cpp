#include "canpp/protocol/can/asc_importer.hpp"

#include "canpp/protocol/can/can_record.hpp"
#include "canpp/trace/binary_trace.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace canpp::protocol::can {
namespace {

std::vector<std::string> split(const std::string& line) {
    std::istringstream input(line);
    std::vector<std::string> tokens;
    for (std::string token; input >> token;) {
        tokens.push_back(std::move(token));
    }
    return tokens;
}

template <typename Integer>
bool parse_integer(const std::string& text, Integer& value, int base) {
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, base);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

std::optional<Frame> parse_line(const std::string& line) {
    const auto tokens = split(line);
    if (tokens.size() < 12U || tokens[1] != "CANFD") {
        return std::nullopt;
    }

    Frame frame;
    try {
        const double seconds = std::stod(tokens[0]);
        if (seconds < 0.0) {
            return std::nullopt;
        }
        frame.timestamp_ns = static_cast<std::uint64_t>(std::llround(seconds * 1'000'000'000.0));
    } catch (...) {
        return std::nullopt;
    }

    unsigned int channel{};
    if (!parse_integer(tokens[2], channel, 10) || channel > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    frame.stream_id = static_cast<std::uint32_t>(channel);
    if (tokens[3] == "Rx") {
        frame.direction = trace::Direction::rx;
    } else if (tokens[3] == "Tx") {
        frame.direction = trace::Direction::tx;
    } else {
        return std::nullopt;
    }

    std::string id_text = tokens[4];
    if (!id_text.empty() && (id_text.back() == 'x' || id_text.back() == 'X')) {
        frame.extended = true;
        id_text.pop_back();
    }
    if (!parse_integer(id_text, frame.can_id, 16)) {
        return std::nullopt;
    }
    frame.message_name = tokens[5];
    frame.fd = true;
    frame.bit_rate_switch = tokens[6] == "1";

    unsigned int dlc{};
    unsigned int length{};
    if (!parse_integer(tokens[8], dlc, 16) || !parse_integer(tokens[9], length, 10) ||
        dlc > 255U || length > 64U || tokens.size() < 10U + length) {
        return std::nullopt;
    }
    frame.dlc = static_cast<std::uint8_t>(dlc);
    frame.data.reserve(length);
    for (std::size_t index = 0; index < length; ++index) {
        unsigned int byte{};
        if (!parse_integer(tokens[10U + index], byte, 16) || byte > 255U) {
            return std::nullopt;
        }
        frame.data.push_back(static_cast<std::uint8_t>(byte));
    }
    return frame;
}

} // namespace

bool import_asc(const std::filesystem::path& input,
                const std::filesystem::path& output,
                ImportStats& stats,
                std::string& error) {
    stats = {};
    std::ifstream source(input);
    if (!source) {
        error = "Cannot open ASC file: " + input.string();
        return false;
    }
    trace::BinaryTraceWriter writer(output);
    if (!writer.good()) {
        error = writer.error();
        return false;
    }

    for (std::string line; std::getline(source, line);) {
        auto frame = parse_line(line);
        if (!frame) {
            ++stats.skipped;
            continue;
        }
        if (!writer.append(encode(*frame))) {
            error = writer.error();
            return false;
        }
        ++stats.imported;
    }
    if (!writer.finalize()) {
        error = writer.error();
        return false;
    }
    return true;
}

} // namespace canpp::protocol::can
