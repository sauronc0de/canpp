#include "canpp/protocol/can/dbc.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <regex>
#include <utility>

namespace canpp::protocol::can {
namespace {

constexpr std::uint32_t extended_marker = 0x80000000U;
constexpr std::uint32_t can_id_mask = 0x1FFFFFFFU;

std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1U);
}

template <typename Integer>
bool parse_integer(const std::string& text, Integer& value) {
    if (text.empty()) {
        return false;
    }
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse_real(const std::string& text, double& value) {
    try {
        std::size_t used = 0;
        value = std::stod(text, &used);
        return used == text.size() && std::isfinite(value);
    } catch (...) {
        return false;
    }
}

std::uint32_t normalized_id(std::uint32_t id) { return id & can_id_mask; }

std::uint64_t message_key(std::uint32_t id, bool extended) {
    return (static_cast<std::uint64_t>(normalized_id(id)) << 1U) | (extended ? 1U : 0U);
}

bool has_keyword(const std::string& text, const char* keyword) {
    const auto length = std::char_traits<char>::length(keyword);
    return text.size() > length && text.compare(0, length, keyword) == 0 &&
           (text[length] == ' ' || text[length] == '\t');
}

std::optional<std::size_t> bit_position(const DbcSignal& signal, std::size_t bit) {
    std::size_t position = signal.start_bit;
    if (signal.byte_order == DbcByteOrder::intel) {
        position += bit;
    } else {
        for (std::size_t index = 0; index < bit; ++index) {
            if (position % 8U == 0U) {
                position += 15U;
            } else {
                --position;
            }
        }
    }
    return position;
}

std::optional<std::uint64_t> raw_value(const DbcSignal& signal,
                                        std::span<const std::uint8_t> data) {
    if (signal.bit_length == 0U || signal.bit_length > 64U) {
        return std::nullopt;
    }
    std::uint64_t raw = 0;
    for (std::size_t bit = 0; bit < signal.bit_length; ++bit) {
        const auto position = bit_position(signal, bit);
        if (!position || position.value() / 8U >= data.size()) {
            return std::nullopt;
        }
        if ((data[position.value() / 8U] & (std::uint8_t{1U} << (position.value() % 8U))) != 0U) {
            const auto raw_bit = signal.byte_order == DbcByteOrder::intel ? bit : signal.bit_length - bit - 1U;
            raw |= std::uint64_t{1U} << raw_bit;
        }
    }
    return raw;
}

std::int64_t signed_value(std::uint64_t raw, std::uint16_t bit_length) {
    if (bit_length < 64U && (raw & (std::uint64_t{1U} << (bit_length - 1U))) != 0U) {
        raw |= ~std::uint64_t{0} << bit_length;
    }
    return static_cast<std::int64_t>(raw);
}

std::optional<std::uint64_t> parse_value_key(const std::string& text) {
    std::int64_t signed_value{};
    if (parse_integer(text, signed_value)) {
        return static_cast<std::uint64_t>(signed_value);
    }
    std::uint64_t unsigned_value{};
    if (parse_integer(text, unsigned_value)) {
        return unsigned_value;
    }
    return std::nullopt;
}

bool signal_is_active(const DbcMessage& message,
                     const DbcSignal& signal,
                     std::span<const std::uint8_t> data) {
    if (!signal.multiplexer_value) {
        return true;
    }
    for (const auto& candidate : message.signals) {
        if (!candidate.multiplexer) {
            continue;
        }
        const auto selector = raw_value(candidate, data);
        return selector && *selector == *signal.multiplexer_value;
    }
    return false;
}

} // namespace

bool DbcDatabase::load(const std::filesystem::path& path, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "Cannot open DBC file: " + path.string();
        return false;
    }

    DbcDatabase parsed;
    std::unordered_map<std::uint64_t, std::size_t> message_indices;
    const std::regex message_pattern(R"(^BO_\s+([0-9]+)\s+([^\s:]+)\s*:\s*([0-9]+))");
    const std::regex signal_pattern(
        R"DBC(^SG_\s+([^\s:]+)(?:\s+([^\s:]+))?\s*:\s*([0-9]+)\|([0-9]+)@([01])([+-])\s*\(([^,]+),([^\)]+)\)\s*\[([^|]+)\|([^\]]+)\]\s*"([^"]*)")DBC");
    const std::regex value_pattern(R"(^VAL_\s+([0-9]+)\s+([^\s]+)\s+(.+);\s*$)");
    const std::regex value_pair_pattern(R"DBC((-?[0-9]+)\s+"([^"]*)")DBC");

    std::size_t line_number = 0;
    for (std::string line; std::getline(input, line);) {
        ++line_number;
        const auto text = trim(line);
        if (text.empty() || text.rfind("//", 0U) == 0U) {
            continue;
        }
        std::smatch match;
        if (has_keyword(text, "BO_")) {
            if (!std::regex_search(text, match, message_pattern)) {
                error = "Malformed BO_ at line " + std::to_string(line_number);
                return false;
            }
            std::uint32_t raw_id{};
            unsigned int size{};
            if (!parse_integer(match[1].str(), raw_id) || !parse_integer(match[3].str(), size) ||
                size > 64U) {
                error = "Invalid BO_ at line " + std::to_string(line_number);
                return false;
            }
            const auto id = normalized_id(raw_id);
            const bool extended = (raw_id & extended_marker) != 0U;
            if (message_indices.contains(message_key(id, extended))) {
                error = "Duplicate or invalid BO_ at line " + std::to_string(line_number);
                return false;
            }
            DbcMessage message;
            message.id = id;
            message.extended = extended;
            message.name = match[2].str();
            message.size = static_cast<std::uint8_t>(size);
            message_indices.emplace(message_key(id, extended), parsed.messages_.size());
            parsed.messages_.push_back(std::move(message));
            continue;
        }
        if (has_keyword(text, "SG_")) {
            if (parsed.messages_.empty() || !std::regex_search(text, match, signal_pattern)) {
                error = "Malformed SG_ at line " + std::to_string(line_number);
                return false;
            }
            auto& message = parsed.messages_.back();
            const auto signal_name = match[1].str();
            for (const auto& signal : message.signals) {
                if (signal.name == signal_name) {
                    error = "Duplicate SG_ at line " + std::to_string(line_number);
                    return false;
                }
            }
            unsigned int start{};
            unsigned int length{};
            unsigned int byte_order{};
            if (!parse_integer(match[3].str(), start) || !parse_integer(match[4].str(), length) ||
                !parse_integer(match[5].str(), byte_order) || start > 2047U || length == 0U) {
                error = "Invalid SG_ at line " + std::to_string(line_number);
                return false;
            }
            DbcSignal signal;
            signal.name = signal_name;
            signal.start_bit = static_cast<std::uint16_t>(start);
            signal.bit_length = length > 64U ? 65U : static_cast<std::uint16_t>(length);
            signal.byte_order = byte_order == 1U ? DbcByteOrder::intel : DbcByteOrder::motorola;
            signal.is_signed = match[6].str() == "-";
            signal.multiplexed = match[2].matched;
            if (!parse_real(trim(match[7].str()), signal.factor) ||
                !parse_real(trim(match[8].str()), signal.offset) ||
                !parse_real(trim(match[9].str()), signal.minimum) ||
                !parse_real(trim(match[10].str()), signal.maximum)) {
                error = "Invalid SG_ numeric value at line " + std::to_string(line_number);
                return false;
            }
            signal.unit = match[11].str();
            if (match[2].matched) {
                const auto multiplexer = match[2].str();
                if (multiplexer == "M") {
                    signal.multiplexer = true;
                } else if (multiplexer.size() > 1U && multiplexer.front() == 'm') {
                    std::uint64_t selector{};
                    if (!parse_integer(multiplexer.substr(1U), selector)) {
                        error = "Invalid SG_ multiplexer at line " + std::to_string(line_number);
                        return false;
                    }
                    signal.multiplexer_value = selector;
                } else {
                    error = "Invalid SG_ multiplexer at line " + std::to_string(line_number);
                    return false;
                }
            }
            message.signals.push_back(std::move(signal));
            continue;
        }
        if (has_keyword(text, "VAL_")) {
            if (!std::regex_search(text, match, value_pattern)) {
                error = "Malformed VAL_ at line " + std::to_string(line_number);
                return false;
            }
            std::uint32_t raw_id{};
            if (!parse_integer(match[1].str(), raw_id)) {
                error = "Invalid VAL_ at line " + std::to_string(line_number);
                return false;
            }
            const auto message_it = message_indices.find(message_key(raw_id, (raw_id & extended_marker) != 0U));
            if (message_it == message_indices.end()) {
                error = "VAL_ references unknown message at line " + std::to_string(line_number);
                return false;
            }
            auto& signals = parsed.messages_[message_it->second].signals;
            const auto signal_it = std::find_if(signals.begin(), signals.end(), [&match](const DbcSignal& signal) {
                return signal.name == match[2].str();
            });
            if (signal_it == signals.end()) {
                error = "VAL_ references unknown signal at line " + std::to_string(line_number);
                return false;
            }
            auto begin = std::sregex_iterator(match[3].first, match[3].second, value_pair_pattern);
            const auto end = std::sregex_iterator();
            if (begin == end) {
                error = "Malformed VAL_ at line " + std::to_string(line_number);
                return false;
            }
            std::size_t consumed = 0;
            for (auto pair = begin; pair != end; ++pair) {
                const auto gap = match[3].str().substr(consumed, static_cast<std::size_t>(pair->position()) - consumed);
                if (gap.find_first_not_of(" \t\r") != std::string::npos) {
                    error = "Malformed VAL_ at line " + std::to_string(line_number);
                    return false;
                }
                const auto value = parse_value_key((*pair)[1].str());
                if (!value) {
                    error = "Invalid VAL_ value at line " + std::to_string(line_number);
                    return false;
                }
                signal_it->value_descriptions[*value] = (*pair)[2].str();
                consumed = static_cast<std::size_t>(pair->position()) + static_cast<std::size_t>(pair->length());
            }
            const auto trailing = match[3].str().substr(consumed);
            if (trailing.find_first_not_of(" \t\r") != std::string::npos) {
                error = "Malformed VAL_ at line " + std::to_string(line_number);
                return false;
            }
        }
    }
    if (parsed.messages_.empty()) {
        error = "DBC file contains no messages";
        return false;
    }
    parsed.path_ = path;
    *this = std::move(parsed);
    return true;
}

const DbcMessage* DbcDatabase::find_message(std::uint32_t id, bool extended) const {
    const auto normalized = normalized_id(id);
    const bool effective_extended = extended || (id & extended_marker) != 0U;
    for (const auto& message : messages_) {
        if (message.id == normalized && message.extended == effective_extended) {
            return &message;
        }
    }
    return nullptr;
}

const DbcSignal* DbcDatabase::find_signal(std::uint32_t id,
                                           bool extended,
                                           const std::string& signal_name) const {
    const auto* message = find_message(id, extended);
    if (message == nullptr) {
        return nullptr;
    }
    const auto signal = std::find_if(message->signals.begin(), message->signals.end(),
                                     [&signal_name](const DbcSignal& candidate) {
                                         return candidate.name == signal_name;
                                     });
    return signal == message->signals.end() ? nullptr : &*signal;
}

std::vector<DbcSignalValue> DbcDatabase::decode(std::uint32_t id,
                                                 bool extended,
                                                 std::span<const std::uint8_t> data) const {
    std::vector<DbcSignalValue> values;
    const auto* message = find_message(id, extended);
    if (message == nullptr || data.size() < message->size) {
        return values;
    }

    std::optional<std::uint64_t> selector_value;
    for (const auto& signal : message->signals) {
        if (!signal.multiplexer) {
            continue;
        }
        selector_value = raw_value(signal, data);
        break;
    }
    for (const auto& signal : message->signals) {
        if (signal.multiplexer_value &&
            (!selector_value || *selector_value != *signal.multiplexer_value)) {
            continue;
        }
        if (signal.bit_length > 64U) {
            continue;
        }
        const auto raw = raw_value(signal, data);
        if (!raw) {
            continue;
        }
        const auto enum_key = signal.is_signed
                                  ? static_cast<std::uint64_t>(signed_value(*raw, signal.bit_length))
                                  : *raw;
        DbcSignalValue value;
        value.name = signal.name;
        value.raw_value = *raw;
        value.value = signal.is_signed
                          ? static_cast<double>(signed_value(*raw, signal.bit_length)) * signal.factor +
                                signal.offset
                          : static_cast<double>(*raw) * signal.factor + signal.offset;
        value.unit = signal.unit;
        if (const auto description = signal.value_descriptions.find(enum_key);
            description != signal.value_descriptions.end()) {
            value.description = description->second;
        }
        values.push_back(std::move(value));
    }
    return values;
}

bool DbcDatabase::has_signal(std::uint32_t id,
                             bool extended,
                             const std::string& signal_name,
                             std::span<const std::uint8_t> data) const {
    const auto* message = find_message(id, extended);
    const auto* signal = find_signal(id, extended, signal_name);
    if (message == nullptr || signal == nullptr || data.size() < message->size) {
        return false;
    }
    const auto message_data = data.first(message->size);
    if (!signal_is_active(*message, *signal, message_data)) {
        return false;
    }
    // A signal can be present in the DBC but still be undecodable from this
    // payload (for example, a signal wider than 64 bits or crossing its end).
    return raw_value(*signal, message_data).has_value();
}

std::vector<std::string> DbcDatabase::message_names() const {
    std::vector<std::string> names;
    names.reserve(messages_.size());
    for (const auto& message : messages_) {
        if (std::find(names.begin(), names.end(), message.name) == names.end()) {
            names.push_back(message.name);
        }
    }
    return names;
}

std::vector<std::string> DbcDatabase::signal_names() const {
    std::vector<std::string> names;
    for (const auto& message : messages_) {
        for (const auto& signal : message.signals) {
            if (std::find(names.begin(), names.end(), signal.name) == names.end()) {
                names.push_back(signal.name);
            }
        }
    }
    return names;
}

} // namespace canpp::protocol::can
