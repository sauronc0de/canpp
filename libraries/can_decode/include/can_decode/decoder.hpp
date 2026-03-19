#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "can_core/core_types.hpp"
#include "can_dbc/database.hpp"

namespace can_decode
{
using DecodedSignalValue = std::variant<std::int64_t, std::uint64_t, float, double>;

struct DecodedSignal
{
  std::string_view name;
  DecodedSignalValue value;
  std::string_view unit;
};

struct DecodedMessage
{
  std::string_view messageName;
  std::uint32_t canId = 0;
  std::vector<DecodedSignal> signals;
};

struct DecodeResult
{
  bool canDecode = false;
  DecodedMessage decodedMessage;
  can_core::ErrorInfo errorInfo;

  [[nodiscard]] bool hasError() const
  {
    return errorInfo.hasError();
  }
};

class Decoder
{
public:
  Decoder() = default;
  explicit Decoder(const can_dbc::Database *database);

  void setDatabase(const can_dbc::Database *database);
  [[nodiscard]] bool canDecode(const can_core::CanEvent &canEvent) const;
  [[nodiscard]] DecodeResult decode(const can_core::CanEvent &canEvent) const;

private:
  const can_dbc::Database *database_ = nullptr;
};
} // namespace can_decode
