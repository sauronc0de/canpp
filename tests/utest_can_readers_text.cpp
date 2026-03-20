#include <array>
#include <filesystem>

#include <doctest/doctest.h>

#include "can_core/error_info.hpp"
#include "can_readers_text/text_trace_reader.hpp"
#include "test_support.hpp"

namespace
{
can_reader_api::SourceDescriptor makeSourceDescriptor(const std::filesystem::path &path)
{
  return {path.string(), path.extension().string(), true};
}
} // namespace

TEST_CASE("can_readers_text reads the sample CSV trace and preserves ordering")
{
  can_readers_text::CsvTraceReader csvTraceReader;
  can_reader_api::ReaderOptions readerOptions;
  std::array<can_core::CanEvent, 4> outputBuffer{};

  const std::filesystem::path sampleTracePath = std::filesystem::path(DATA_PATH) / "sample_trace.csv";
  REQUIRE(csvTraceReader.open(makeSourceDescriptor(sampleTracePath), readerOptions));

  const can_reader_api::ReadResult readResult = csvTraceReader.readChunk(outputBuffer);

  REQUIRE_FALSE(readResult.hasError());
  CHECK(readResult.eventCount == 3U);
  CHECK(readResult.isEndOfStream);
  CHECK(outputBuffer[0].timestampNs == 1000000U);
  CHECK(outputBuffer[0].canId == 0x123U);
  CHECK(outputBuffer[1].payload[2] == 0xCCU);
  CHECK(outputBuffer[2].channel == 1U);

  const can_core::TraceMetadata metadata = csvTraceReader.metadata();
  CHECK(metadata.sourceFormat == "csv");
  CHECK(metadata.eventCount == 3U);
  CHECK(metadata.startTimestampNs == 1000000U);
  CHECK(metadata.endTimestampNs == 3000000U);
}

TEST_CASE("can_readers_text strict CSV parsing reports malformed input")
{
  const test_support::ScopedTempFile traceFile(
    "strict_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "bad,line\n");

  can_readers_text::CsvTraceReader csvTraceReader;
  can_reader_api::ReaderOptions readerOptions;
  std::array<can_core::CanEvent, 2> outputBuffer{};

  REQUIRE(csvTraceReader.open(makeSourceDescriptor(traceFile.path()), readerOptions));
  const can_reader_api::ReadResult readResult = csvTraceReader.readChunk(outputBuffer);

  REQUIRE(readResult.hasError());
  CHECK(readResult.errorInfo.code == can_core::ErrorCode::ParseFailure);
  CHECK(readResult.errorInfo.line == 2U);
}

TEST_CASE("can_readers_text tolerant CSV parsing skips malformed records")
{
  const test_support::ScopedTempFile traceFile(
    "tolerant_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,123,2,11 22,CAN\n"
    "broken,row\n"
    "0.003,1,124,1,FF,FD\n");

  can_readers_text::CsvTraceReader csvTraceReader;
  can_reader_api::ReaderOptions readerOptions;
  readerOptions.shouldValidateStrictly = false;
  std::array<can_core::CanEvent, 3> outputBuffer{};

  REQUIRE(csvTraceReader.open(makeSourceDescriptor(traceFile.path()), readerOptions));
  const can_reader_api::ReadResult readResult = csvTraceReader.readChunk(outputBuffer);

  REQUIRE_FALSE(readResult.hasError());
  CHECK(readResult.eventCount == 2U);
  CHECK(readResult.isEndOfStream);
  CHECK(outputBuffer[1].frameType == can_core::FrameType::CanFd);
}

TEST_CASE("can_readers_text parses candump and ASC traces through reader factories")
{
  SUBCASE("candump")
  {
    const test_support::ScopedTempFile traceFile("candump_trace", ".candump", "(0.001) can0 123#1122\n");

    can_readers_text::CandumpReaderFactory factory;
    REQUIRE(factory.canOpen(makeSourceDescriptor(traceFile.path())));
    auto reader = factory.create();
    std::array<can_core::CanEvent, 2> outputBuffer{};

    REQUIRE(reader->open(makeSourceDescriptor(traceFile.path()), {}));
    const can_reader_api::ReadResult readResult = reader->readChunk(outputBuffer);

    REQUIRE_FALSE(readResult.hasError());
    CHECK(readResult.eventCount == 1U);
    CHECK(outputBuffer[0].channel == 0U);
    CHECK(outputBuffer[0].canId == 0x123U);
    CHECK(outputBuffer[0].payload[1] == 0x22U);
  }

  SUBCASE("asc")
  {
    const test_support::ScopedTempFile traceFile(
      "asc_trace",
      ".asc",
      "date Fri Mar 19 00:00:00.000 2026\n"
      "0.001 1 123 Rx d 2 11 22\n");

    can_readers_text::AscTraceReaderFactory factory;
    REQUIRE(factory.canOpen(makeSourceDescriptor(traceFile.path())));
    auto reader = factory.create();
    std::array<can_core::CanEvent, 2> outputBuffer{};

    REQUIRE(reader->open(makeSourceDescriptor(traceFile.path()), {}));
    const can_reader_api::ReadResult readResult = reader->readChunk(outputBuffer);

    REQUIRE_FALSE(readResult.hasError());
    CHECK(readResult.eventCount == 1U);
    CHECK(outputBuffer[0].channel == 1U);
    CHECK(outputBuffer[0].canId == 0x123U);
    CHECK(outputBuffer[0].dlc == 2U);
  }
}

TEST_CASE("can_readers_text parses mixed ASC content and skips unsupported non-CAN records")
{
  const test_support::ScopedTempFile traceFile(
    "mixed_asc_trace",
    ".asc",
    "date Mon Mar 2 10:39:49.909 pm 2026\n"
    "base hex  timestamps absolute\n"
    "internal events logged\n"
    "// version 18.4.0\n"
    "34.808347 1 131 Rx d 8 00 00 00 A9 00 FE DF FF Length = 234000 BitCount = 121 ID = 305\n"
    "34.808424 CANFD 15 Rx 3ec 1 0 8 8 db 00 00 00 00 00 00 00 4773 133 203000 0 42030150 42030150 20000000 20000000\n"
    "35.926432 1 AF9556Ex Rx d 8 00 20 00 00 00 00 00 00 Length = 282000 BitCount = 145 ID = 184112494x\n"
    "35.927757 CANFD 15 Rx 17331a10x 1 0 5 5 36 96 00 00 00 4329 121 203000 0 42030150 42030150 20000000 20000000\n"
    "36.075722 ETH 1 Rx 72:33330000002E027DFA0010008100800286DD60000000003811FFFD537CB8038300020000000000000010FF14000000000000000000000000002EA7F2A63D0038B44300000018000000080000802E780000000000001F00000008892A0060FE0000000000001D000000080000000000000000 Sim:0\n"
    "35.927944 10 1BFD9201x Rx d 8 80 00 00 23 29 04 30 00 Length = 0 BitCount = 0 ID = 469602817x\n");

  can_readers_text::AscTraceReader ascTraceReader;
  std::array<can_core::CanEvent, 8> outputBuffer{};

  REQUIRE(ascTraceReader.open(makeSourceDescriptor(traceFile.path()), {}));
  const can_reader_api::ReadResult readResult = ascTraceReader.readChunk(outputBuffer);

  REQUIRE_FALSE(readResult.hasError());
  REQUIRE(readResult.isEndOfStream);
  REQUIRE(readResult.eventCount == 5U);

  CHECK(outputBuffer[0].canId == 0x131U);
  CHECK(outputBuffer[0].channel == 1U);
  CHECK(outputBuffer[0].frameType == can_core::FrameType::Can20);
  CHECK(outputBuffer[1].canId == 0x3ECU);
  CHECK(outputBuffer[1].frameType == can_core::FrameType::CanFd);
  CHECK(outputBuffer[2].canId == 0x0AF9556EU);
  CHECK(outputBuffer[3].canId == 0x17331A10U);
  CHECK(outputBuffer[4].canId == 0x1BFD9201U);
  CHECK(outputBuffer[4].channel == 10U);
}
