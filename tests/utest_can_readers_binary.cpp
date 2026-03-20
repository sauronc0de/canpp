#include <array>

#include <doctest/doctest.h>

#include "can_readers_binary/binary_trace_reader.hpp"

TEST_CASE("can_readers_binary binary cursor enforces bounds-safe advancement")
{
  const std::vector<std::byte> bytes{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
  can_readers_binary::BinaryCursor binaryCursor(bytes);

  REQUIRE(binaryCursor.isValid());
  CHECK(binaryCursor.remaining() == 3U);
  REQUIRE(binaryCursor.advance(2U));
  CHECK(binaryCursor.remaining() == 1U);
  CHECK_FALSE(binaryCursor.advance(2U));
  CHECK(binaryCursor.remaining() == 1U);
}

TEST_CASE("can_readers_binary recognized readers expose unsupported-format contract until implemented")
{
  std::array<can_core::CanEvent, 1> outputBuffer{};

  SUBCASE("blf")
  {
    can_readers_binary::BlfReader reader;
    can_reader_api::SourceDescriptor sourceDescriptor{"trace.blf", ".blf", true};
    REQUIRE(reader.open(sourceDescriptor, {}));
    CHECK(reader.metadata().sourceFormat == "blf");
    const auto readResult = reader.readChunk(outputBuffer);
    REQUIRE(readResult.hasError());
    CHECK(readResult.errorInfo.code == can_core::ErrorCode::UnsupportedFormat);
  }

  SUBCASE("mf4")
  {
    can_readers_binary::Mf4ReaderFactory factory;
    can_reader_api::SourceDescriptor sourceDescriptor{"trace.mf4", ".mf4", true};
    REQUIRE(factory.canOpen(sourceDescriptor));
    auto reader = factory.create();
    REQUIRE(reader->open(sourceDescriptor, {}));
    CHECK(reader->capabilities().supportsRandomAccess);
    CHECK_FALSE(reader->capabilities().supportsStreaming);
  }

  SUBCASE("trc wrong extension")
  {
    can_readers_binary::TrcReader reader;
    can_reader_api::SourceDescriptor sourceDescriptor{"trace.csv", ".csv", true};
    CHECK_FALSE(reader.open(sourceDescriptor, {}));
  }
}
