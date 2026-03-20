#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "can_core/error_info.hpp"

namespace can_dbc
{
enum class SignalValueType
{
  UnsignedInteger,
  SignedInteger,
  Float32,
  Float64,
};

struct SignalDefinition
{
  std::string name;
  bool isMultiplexer = false;
  std::optional<std::uint64_t> multiplexValue;
  std::uint16_t startBit = 0;
  std::uint16_t bitLength = 0;
  bool isLittleEndian = true;
  bool isSigned = false;
  double scale = 1.0;
  double offset = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  std::string unit;
  SignalValueType valueType = SignalValueType::UnsignedInteger;
};

struct MessageDefinition
{
  std::uint32_t canId = 0;
  std::string name;
  std::uint8_t dlc = 0;
  std::vector<SignalDefinition> signalDefinitions;
};

class Database
{
public:
  Database() = default;

  void addMessage(MessageDefinition messageDefinition);
  [[nodiscard]] const MessageDefinition *findMessageByCanId(std::uint32_t canId) const;
  [[nodiscard]] const MessageDefinition *findMessageByName(std::string_view name) const;
  [[nodiscard]] const std::vector<MessageDefinition> &messageDefinitions() const;
  [[nodiscard]] bool isEmpty() const;

private:
  std::vector<MessageDefinition> messageDefinitions_;
  std::unordered_map<std::uint32_t, std::size_t> canIdToIndex_;
  std::unordered_map<std::string, std::size_t> nameToIndex_;
};

struct LoadResult
{
  Database database;
  can_core::ErrorInfo errorInfo;

  [[nodiscard]] bool hasError() const
  {
    return errorInfo.hasError();
  }
};

class DbcLoader
{
public:
  [[nodiscard]] LoadResult loadFromFile(const std::string &path) const;
  [[nodiscard]] LoadResult loadFromText(std::string_view dbcText) const;
};
} // namespace can_dbc
