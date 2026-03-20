#include <array>
#include <filesystem>
#include <vector>

#include <doctest/doctest.h>

#include "can_cache/cache_reader.hpp"
#include "can_index/trace_index.hpp"
#include "can_readers_text/text_trace_reader.hpp"
#include "test_support.hpp"

namespace
{
can_reader_api::SourceDescriptor makeSourceDescriptor(const std::filesystem::path &path)
{
  return {path.string(), path.extension().string(), true};
}

bool sameEvent(const can_core::CanEvent &left, const can_core::CanEvent &right)
{
  return left.timestampNs == right.timestampNs && left.canId == right.canId && left.dlc == right.dlc &&
    left.channel == right.channel && left.frameType == right.frameType && left.payload == right.payload;
}
} // namespace

TEST_CASE("architecture cache-build flow preserves streamed reader output and index lookups")
{
  const test_support::ScopedTempFile traceFile(
    "cache_index_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,123,2,11 22,CAN\n"
    "0.002,1,456,1,AA,CAN\n"
    "0.003,0,123,3,01 02 03,FD\n"
    "0.004,2,777,2,10 20,CAN\n"
    "0.005,0,123,1,FF,CAN\n");
  const test_support::ScopedTempFile cacheFile("cache_index_trace", ".bin");

  can_readers_text::CsvTraceReader csvTraceReader;
  can_reader_api::ReaderOptions readerOptions;
  readerOptions.chunkSize = 2U;
  REQUIRE(csvTraceReader.open(makeSourceDescriptor(traceFile.path()), readerOptions));

  can_cache::CacheWriter cacheWriter;
  REQUIRE(cacheWriter.open(cacheFile.string()));

  can_index::TraceIndex traceIndex;
  std::vector<can_core::CanEvent> streamedEvents;
  std::array<can_core::CanEvent, 2> outputBuffer{};
  std::uint64_t ordinal = 0;

  while(true)
  {
    const can_reader_api::ReadResult readResult = csvTraceReader.readChunk(outputBuffer);
    REQUIRE_FALSE(readResult.hasError());

    if(readResult.eventCount > 0U)
    {
      const std::span<const can_core::CanEvent> chunk(outputBuffer.data(), readResult.eventCount);
      REQUIRE(cacheWriter.writeChunk(chunk));

      for(std::size_t index = 0; index < readResult.eventCount; ++index)
      {
        streamedEvents.push_back(outputBuffer[index]);
        traceIndex.addEvent(outputBuffer[index], ordinal++);
      }
    }

    if(readResult.isEndOfStream)
    {
      break;
    }
  }

  cacheWriter.close();
  csvTraceReader.close();

  REQUIRE(streamedEvents.size() == 5U);

  can_cache::CacheReader cacheReader;
  REQUIRE(cacheReader.open(cacheFile.string()));

  const can_core::TraceMetadata metadata = cacheReader.metadata();
  CHECK(metadata.sourceFormat == "can_cache");
  CHECK(metadata.eventCount == streamedEvents.size());
  CHECK(metadata.startTimestampNs == streamedEvents.front().timestampNs);
  CHECK(metadata.endTimestampNs == streamedEvents.back().timestampNs);

  std::vector<can_core::CanEvent> cachedEvents;
  std::array<can_core::CanEvent, 2> cachedChunk{};
  for(std::uint64_t chunkId = 0; chunkId < 3U; ++chunkId)
  {
    const std::size_t eventCount = cacheReader.readChunk(chunkId, cachedChunk);
    for(std::size_t index = 0; index < eventCount; ++index)
    {
      cachedEvents.push_back(cachedChunk[index]);
    }
  }

  REQUIRE(cachedEvents.size() == streamedEvents.size());
  for(std::size_t index = 0; index < streamedEvents.size(); ++index)
  {
    CHECK(sameEvent(cachedEvents[index], streamedEvents[index]));
  }

  const can_index::TimeRangeLookup timeRangeLookup = traceIndex.findTimeRange(2000000U, 4000000U);
  REQUIRE(timeRangeLookup.hasMatch);
  CHECK(timeRangeLookup.startChunkId == 1U);
  CHECK(timeRangeLookup.endChunkId == 3U);

  const std::vector<can_index::ChunkLocation> canIdCandidates = traceIndex.findCanIdCandidates(0x123U);
  REQUIRE(canIdCandidates.size() == 3U);
  CHECK(canIdCandidates[0].chunkId == 0U);
  CHECK(canIdCandidates[1].chunkId == 2U);
  CHECK(canIdCandidates[2].chunkId == 4U);

  const auto ordinalLocation = traceIndex.findOrdinal(3U);
  REQUIRE(ordinalLocation.has_value());
  CHECK(ordinalLocation->chunkId == 3U);
}
