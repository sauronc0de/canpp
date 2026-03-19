#pragma once

#include <cstdint>
#include <string>

namespace can_core
{
struct TraceMetadata
{
  std::string sourcePath;
  std::string sourceFormat;
  std::uint64_t eventCount = 0;
  std::uint64_t startTimestampNs = 0;
  std::uint64_t endTimestampNs = 0;
};
} // namespace can_core

