#pragma once

#include <cstddef>

namespace can_core
{
struct ContextRequest
{
  std::size_t beforeCount = 0;
  std::size_t afterCount = 0;
  bool isEnabled = false;
};
} // namespace can_core

