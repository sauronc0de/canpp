#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace can_core
{
enum class FrameType : std::uint8_t
{
  Can20 = 0,
  CanFd = 1,
};

struct CanEvent
{
  std::uint64_t timestampNs = 0;
  std::uint32_t canId = 0;
  std::uint8_t dlc = 0;
  std::uint8_t channel = 0;
  FrameType frameType = FrameType::Can20;
  std::array<std::uint8_t, 64> payload{};

  [[nodiscard]] bool hasPayload() const
  {
    return dlc != 0;
  }

  [[nodiscard]] std::span<const std::uint8_t> payloadView() const
  {
    return std::span<const std::uint8_t>(payload.data(), dlc);
  }
};

[[nodiscard]] bool isValidCanId(std::uint32_t canId);
} // namespace can_core
