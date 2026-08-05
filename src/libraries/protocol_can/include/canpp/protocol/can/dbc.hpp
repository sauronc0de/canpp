#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace canpp::protocol::can {

enum class DbcByteOrder { intel, motorola };

struct DbcSignal {
    std::string name;
    std::uint16_t start_bit{};
    std::uint16_t bit_length{};
    DbcByteOrder byte_order{DbcByteOrder::intel};
    bool is_signed{};
    double factor{1.0};
    double offset{};
    double minimum{};
    double maximum{};
    std::string unit;
    bool multiplexed{};
    bool multiplexer{};
    std::optional<std::uint64_t> multiplexer_value;
    std::unordered_map<std::uint64_t, std::string> value_descriptions;
};

struct DbcMessage {
    std::uint32_t id{};
    bool extended{};
    std::string name;
    std::uint8_t size{};
    std::vector<DbcSignal> signals;
};

struct DbcSignalValue {
    std::string name;
    std::uint64_t raw_value{};
    double value{};
    std::string unit;
    std::optional<std::string> description;
};

struct SignalKey {
    std::uint32_t can_id{};
    bool extended{};
    std::string message_name;
    std::string signal_name;

    friend bool operator==(const SignalKey&, const SignalKey&) = default;
};

struct SignalDescriptor {
    SignalKey key;
    std::string unit;
    double minimum{};
    double maximum{};
};

class DbcDatabase {
public:
    bool load(const std::filesystem::path& path, std::string& error);

    [[nodiscard]] const DbcMessage* find_message(std::uint32_t id, bool extended) const;
    [[nodiscard]] const DbcSignal* find_signal(std::uint32_t id,
                                                bool extended,
                                                const std::string& signal_name) const;
    [[nodiscard]] std::vector<DbcSignalValue> decode(std::uint32_t id,
                                                      bool extended,
                                                      std::span<const std::uint8_t> data) const;
    [[nodiscard]] bool has_signal(std::uint32_t id,
                                  bool extended,
                                  const std::string& signal_name,
                                  std::span<const std::uint8_t> data) const;
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::vector<std::string> message_names() const;
    [[nodiscard]] std::vector<std::string> signal_names() const;
    [[nodiscard]] std::vector<SignalDescriptor> signal_catalog() const;
    [[nodiscard]] bool empty() const noexcept { return messages_.empty(); }

private:
    std::filesystem::path path_;
    std::vector<DbcMessage> messages_;
};

} // namespace canpp::protocol::can
