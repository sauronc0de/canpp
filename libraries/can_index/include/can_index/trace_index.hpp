#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "can_core/core_types.hpp"

namespace can_index
{
struct ChunkLocation
{
  std::uint64_t chunkId = 0;
  std::uint64_t fileOffset = 0;
  std::uint32_t eventOffset = 0;
};

struct TimeRangeLookup
{
  std::uint64_t startChunkId = 0;
  std::uint64_t endChunkId = 0;
  bool hasMatch = false;
};

class TraceIndex
{
public:
  void addEvent(const can_core::CanEvent &canEvent, std::uint64_t ordinal, std::uint64_t fileOffset = 0);

  [[nodiscard]] TimeRangeLookup findTimeRange(std::uint64_t startTimestampNs, std::uint64_t endTimestampNs) const;
  [[nodiscard]] std::vector<ChunkLocation> findCanIdCandidates(std::uint32_t canId) const;
  [[nodiscard]] std::optional<ChunkLocation> findOrdinal(std::uint64_t ordinal) const;
  [[nodiscard]] bool isEmpty() const;

private:
  std::vector<std::pair<std::uint64_t, ChunkLocation>> timestampToLocation_;
  std::unordered_map<std::uint32_t, std::vector<ChunkLocation>> canIdToLocations_;
  std::unordered_map<std::uint64_t, ChunkLocation> ordinalToLocation_;
};

class IndexBuilder
{
public:
  [[nodiscard]] TraceIndex build(const std::vector<can_core::CanEvent> &events) const;
};
} // namespace can_index

