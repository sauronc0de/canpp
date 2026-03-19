#pragma once

#include <cstdint>

namespace can_core
{
struct MatchReference
{
  std::uint64_t ordinal = 0;
  std::uint64_t sourceOffset = 0;
};
} // namespace can_core

