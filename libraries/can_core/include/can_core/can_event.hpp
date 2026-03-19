#pragma once

#include <array>
#include <cstdint>

namespace can_core
{
enum class FrameType : std::uint8_t
{
  kCan20 = 0,
  kCanFd = 1,
};

struct CanEvent
{
  std::uint64_t timestamp_ns = 0;
  std::uint32_t can_id = 0;
  std::uint8_t dlc = 0;
  std::uint8_t channel = 0;
  FrameType frame_type = FrameType::kCan20;
  std::array<std::uint8_t, 64> payload{};
};
} // namespace can_core

