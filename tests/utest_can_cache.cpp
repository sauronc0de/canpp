#include <array>
#include <cstdint>
#include <fstream>

#include <doctest/doctest.h>

#include "can_cache/cache_reader.hpp"
#include "test_support.hpp"

namespace
{
can_core::CanEvent makeEvent(std::uint64_t timestampNs, std::uint32_t canId, std::initializer_list<std::uint8_t> bytes)
{
  can_core::CanEvent canEvent;
  canEvent.timestampNs = timestampNs;
  canEvent.canId = canId;
  canEvent.dlc = static_cast<std::uint8_t>(bytes.size());

  std::size_t index = 0;
  for(const std::uint8_t byteValue : bytes)
  {
    canEvent.payload[index++] = byteValue;
  }

  return canEvent;
}
} // namespace

TEST_CASE("can_cache round-trips chunked events and metadata")
{
  const test_support::ScopedTempFile cacheFile("cache_roundtrip", ".bin");
  const std::array<can_core::CanEvent, 2> firstChunk{
    makeEvent(100U, 0x100U, {0x11U}),
    makeEvent(200U, 0x101U, {0x22U, 0x33U}),
  };
  const std::array<can_core::CanEvent, 1> secondChunk{
    makeEvent(300U, 0x102U, {0x44U}),
  };

  can_cache::CacheWriter cacheWriter;
  REQUIRE(cacheWriter.open(cacheFile.string()));
  REQUIRE(cacheWriter.writeChunk(firstChunk));
  REQUIRE(cacheWriter.writeChunk(secondChunk));
  cacheWriter.close();

  can_cache::CacheReader cacheReader;
  REQUIRE(cacheReader.open(cacheFile.string()));

  const can_core::TraceMetadata metadata = cacheReader.metadata();
  CHECK(metadata.sourceFormat == "can_cache");
  CHECK(metadata.eventCount == 3U);
  CHECK(metadata.startTimestampNs == 100U);
  CHECK(metadata.endTimestampNs == 300U);

  std::array<can_core::CanEvent, 2> outputBuffer{};
  REQUIRE(cacheReader.readChunk(0U, outputBuffer) == 2U);
  CHECK(outputBuffer[0].canId == 0x100U);
  CHECK(outputBuffer[1].payload[1] == 0x33U);

  outputBuffer = {};
  REQUIRE(cacheReader.readChunk(1U, outputBuffer) == 1U);
  CHECK(outputBuffer[0].timestampNs == 300U);
  CHECK(outputBuffer[0].canId == 0x102U);
}

TEST_CASE("can_cache rejects files with an invalid header")
{
  const test_support::ScopedTempFile cacheFile("cache_invalid", ".bin");
  {
    std::ofstream output(cacheFile.path(), std::ios::binary | std::ios::trunc);
    const std::uint32_t invalidMagic = 0x0U;
    output.write(reinterpret_cast<const char *>(&invalidMagic), sizeof(invalidMagic));
  }

  can_cache::CacheReader cacheReader;
  CHECK_FALSE(cacheReader.open(cacheFile.string()));
}
