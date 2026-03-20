#include <vector>

#include <doctest/doctest.h>

#include "can_index/trace_index.hpp"

namespace
{
can_core::CanEvent makeEvent(std::uint64_t timestampNs, std::uint32_t canId)
{
  can_core::CanEvent canEvent;
  canEvent.timestampNs = timestampNs;
  canEvent.canId = canId;
  return canEvent;
}
} // namespace

TEST_CASE("can_index builds ordinal and CAN ID lookups consistently from event order")
{
  const std::vector<can_core::CanEvent> events{
    makeEvent(100U, 0x100U),
    makeEvent(150U, 0x200U),
    makeEvent(220U, 0x100U),
    makeEvent(500U, 0x300U),
  };

  const can_index::TraceIndex traceIndex = can_index::IndexBuilder().build(events);

  REQUIRE_FALSE(traceIndex.isEmpty());

  const auto candidates = traceIndex.findCanIdCandidates(0x100U);
  REQUIRE(candidates.size() == 2U);
  CHECK(candidates[0].chunkId == 0U);
  CHECK(candidates[1].chunkId == 2U);

  const auto ordinal = traceIndex.findOrdinal(3U);
  REQUIRE(ordinal.has_value());
  CHECK(ordinal->chunkId == 3U);
  CHECK(ordinal->eventOffset == 0U);
}

TEST_CASE("can_index time-range lookup narrows to the matching event span")
{
  can_index::TraceIndex traceIndex;
  traceIndex.addEvent(makeEvent(100U, 0x100U), 10U, 1000U);
  traceIndex.addEvent(makeEvent(250U, 0x200U), 11U, 2000U);
  traceIndex.addEvent(makeEvent(400U, 0x100U), 12U, 3000U);
  traceIndex.addEvent(makeEvent(900U, 0x300U), 13U, 4000U);

  const auto inRange = traceIndex.findTimeRange(200U, 500U);
  REQUIRE(inRange.hasMatch);
  CHECK(inRange.startChunkId == 11U);
  CHECK(inRange.endChunkId == 12U);

  const auto exactRange = traceIndex.findTimeRange(100U, 100U);
  REQUIRE(exactRange.hasMatch);
  CHECK(exactRange.startChunkId == 10U);
  CHECK(exactRange.endChunkId == 10U);

  const auto missingRange = traceIndex.findTimeRange(901U, 950U);
  CHECK_FALSE(missingRange.hasMatch);
}
