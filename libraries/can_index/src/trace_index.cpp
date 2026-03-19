#include "can_index/trace_index.hpp"

#include <algorithm>

namespace can_index
{
void TraceIndex::addEvent(const can_core::CanEvent &canEvent, std::uint64_t ordinal, std::uint64_t fileOffset)
{
  ChunkLocation chunkLocation;
  chunkLocation.chunkId = ordinal;
  chunkLocation.fileOffset = fileOffset;
  chunkLocation.eventOffset = 0;

  timestampToLocation_.push_back({canEvent.timestampNs, chunkLocation});
  canIdToLocations_[canEvent.canId].push_back(chunkLocation);
  ordinalToLocation_[ordinal] = chunkLocation;
}

TimeRangeLookup TraceIndex::findTimeRange(std::uint64_t startTimestampNs, std::uint64_t endTimestampNs) const
{
  TimeRangeLookup timeRangeLookup;
  if(timestampToLocation_.empty())
  {
    return timeRangeLookup;
  }

  const auto startIterator = std::lower_bound(
    timestampToLocation_.begin(),
    timestampToLocation_.end(),
    startTimestampNs,
    [](const auto &entry, std::uint64_t timestampNs)
    {
      return entry.first < timestampNs;
    });
  const auto endIterator = std::upper_bound(
    timestampToLocation_.begin(),
    timestampToLocation_.end(),
    endTimestampNs,
    [](std::uint64_t timestampNs, const auto &entry)
    {
      return timestampNs < entry.first;
    });

  if(startIterator == timestampToLocation_.end() || startIterator == endIterator)
  {
    return timeRangeLookup;
  }

  timeRangeLookup.startChunkId = startIterator->second.chunkId;
  timeRangeLookup.endChunkId = (endIterator - 1)->second.chunkId;
  timeRangeLookup.hasMatch = true;
  return timeRangeLookup;
}

std::vector<ChunkLocation> TraceIndex::findCanIdCandidates(std::uint32_t canId) const
{
  const auto iterator = canIdToLocations_.find(canId);
  if(iterator == canIdToLocations_.end())
  {
    return {};
  }

  return iterator->second;
}

std::optional<ChunkLocation> TraceIndex::findOrdinal(std::uint64_t ordinal) const
{
  const auto iterator = ordinalToLocation_.find(ordinal);
  if(iterator == ordinalToLocation_.end())
  {
    return std::nullopt;
  }

  return iterator->second;
}

bool TraceIndex::isEmpty() const
{
  return timestampToLocation_.empty();
}

TraceIndex IndexBuilder::build(const std::vector<can_core::CanEvent> &events) const
{
  TraceIndex traceIndex;
  for(std::size_t index = 0; index < events.size(); ++index)
  {
    traceIndex.addEvent(events[index], static_cast<std::uint64_t>(index));
  }

  return traceIndex;
}
} // namespace can_index

