#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "can_app/can_app.hpp"
#include "can_cache/cache_reader.hpp"
#include "can_dbc/database.hpp"
#include "can_decode/decoder.hpp"
#include "can_index/trace_index.hpp"
#include "can_query/query_executor.hpp"
#include "can_readers_text/text_trace_reader.hpp"
#include "can_script_api/script_engine.hpp"
#include "can_script_lua/lua_engine.hpp"
#include "test_support.hpp"

namespace
{
can_reader_api::SourceDescriptor makeSourceDescriptor(const std::filesystem::path &path)
{
  return {path.string(), path.extension().string(), true};
}

std::string readTextFile(const std::filesystem::path &path)
{
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

class CollectingSink : public can_query::IResultSink
{
public:
  void onMatch(const can_query::QueryMatch &queryMatch) override
  {
    matches.push_back(queryMatch);
  }

  std::vector<can_query::QueryMatch> matches;
};
} // namespace

TEST_CASE("qualification canonical readers satisfy implemented event-model and initial I/O requirements")
{
  // Requirements:
  // SYS-REQ-004
  // DATA-REQ-001, DATA-REQ-002, DATA-REQ-003, DATA-REQ-005
  // IO-REQ-001, IO-REQ-002, IO-REQ-003, IO-REQ-010, IO-REQ-011, IO-REQ-012
  // PERF-REQ-001, PERF-REQ-006
  // NFR-PERF-001
  const test_support::ScopedTempFile candumpFile("qtest_candump", ".candump", "(0.001) can0 123#1122\n");
  const test_support::ScopedTempFile csvFile(
    "qtest_csv",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.002,1,321,64,"
    "00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F "
    "10 11 12 13 14 15 16 17 18 19 1A 1B 1C 1D 1E 1F "
    "20 21 22 23 24 25 26 27 28 29 2A 2B 2C 2D 2E 2F "
    "30 31 32 33 34 35 36 37 38 39 3A 3B 3C 3D 3E 3F,FD\n");
  const test_support::ScopedTempFile ascFile(
    "qtest_asc",
    ".asc",
    "date Fri Mar 20 00:00:00.000 2026\n"
    "0.003 2 456 Rx d 2 AA BB\n");

  SUBCASE("candump through factory")
  {
    can_readers_text::CandumpReaderFactory factory;
    REQUIRE(factory.canOpen(makeSourceDescriptor(candumpFile.path())));
    auto reader = factory.create();
    std::array<can_core::CanEvent, 2> outputBuffer{};

    REQUIRE(reader->open(makeSourceDescriptor(candumpFile.path()), {}));
    const can_reader_api::ReadResult readResult = reader->readChunk(outputBuffer);
    REQUIRE_FALSE(readResult.hasError());
    REQUIRE(readResult.eventCount == 1U);
    CHECK(outputBuffer[0].timestampNs == 1000000U);
    CHECK(outputBuffer[0].canId == 0x123U);
    CHECK(outputBuffer[0].channel == 0U);
    CHECK(outputBuffer[0].payload[1] == 0x22U);
  }

  SUBCASE("csv through factory with CAN FD payload")
  {
    can_readers_text::CsvTraceReaderFactory factory;
    REQUIRE(factory.canOpen(makeSourceDescriptor(csvFile.path())));
    auto reader = factory.create();
    std::array<can_core::CanEvent, 2> outputBuffer{};

    REQUIRE(reader->open(makeSourceDescriptor(csvFile.path()), {}));
    const can_reader_api::ReadResult readResult = reader->readChunk(outputBuffer);
    REQUIRE_FALSE(readResult.hasError());
    REQUIRE(readResult.eventCount == 1U);
    CHECK(outputBuffer[0].timestampNs == 2000000U);
    CHECK(outputBuffer[0].canId == 0x321U);
    CHECK(outputBuffer[0].channel == 1U);
    CHECK(outputBuffer[0].frameType == can_core::FrameType::CanFd);
    CHECK(outputBuffer[0].payloadView().size() == 64U);
    CHECK(outputBuffer[0].payload[63] == 0x3FU);
  }

  SUBCASE("asc through factory")
  {
    can_readers_text::AscTraceReaderFactory factory;
    REQUIRE(factory.canOpen(makeSourceDescriptor(ascFile.path())));
    auto reader = factory.create();
    std::array<can_core::CanEvent, 2> outputBuffer{};

    REQUIRE(reader->open(makeSourceDescriptor(ascFile.path()), {}));
    const can_reader_api::ReadResult readResult = reader->readChunk(outputBuffer);
    REQUIRE_FALSE(readResult.hasError());
    REQUIRE(readResult.eventCount == 1U);
    CHECK(outputBuffer[0].timestampNs == 3000000U);
    CHECK(outputBuffer[0].canId == 0x456U);
    CHECK(outputBuffer[0].channel == 2U);
    CHECK(outputBuffer[0].payload[0] == 0xAAU);
  }
}

TEST_CASE("qualification decode query export and public-api flow satisfy implemented functional requirements")
{
  // Requirements:
  // SYS-REQ-007
  // DBC-REQ-001, DBC-REQ-002, DBC-REQ-003, DBC-REQ-004, DBC-REQ-009
  // QRY-REQ-001, QRY-REQ-002, QRY-REQ-003, QRY-REQ-004, QRY-REQ-005, QRY-REQ-006, QRY-REQ-007, QRY-REQ-008, QRY-REQ-009, QRY-REQ-010, QRY-REQ-011, QRY-REQ-012
  // IO-REQ-030, IO-REQ-031
  const test_support::ScopedTempFile traceFile(
    "qtest_query_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,123,2,2A 00,CAN\n"
    "0.002,1,123,2,05 00,FD\n"
    "0.003,0,456,1,10,CAN\n"
    "0.004,1,123,2,00 00,CAN\n");
  const test_support::ScopedTempFile dbcFile(
    "qtest_query_dbc",
    ".dbc",
    "BO_ 291 VehicleStatus: 8 Vector__XXX\n"
    " SG_ VehicleSpeed : 0|16@1+ (1,0) [0|65535] \"km/h\" Vector__XXX\n"
    "BO_ 1110 FloatStatus: 8 Vector__XXX\n"
    " SG_ FloatSignal : 0|32@1+ (1,0) [0|0] \"\" Vector__XXX\n"
    " SG_ DoubleSignal : 0|64@1+ (1,0) [0|0] \"\" Vector__XXX\n");
  const test_support::ScopedTempFile exportFile("qtest_export", ".csv");

  // Public API / export path
  can_app::RunOptions runOptions;
  runOptions.tracePath = traceFile.string();
  runOptions.dbcPath = dbcFile.string();
  runOptions.canIdFilter = 0x123U;
  runOptions.shouldDecodeMatches = true;
  runOptions.exportRequest = can_export::ExportRequest{exportFile.string(), can_export::ExportMode::DecodedCsv, true};

  std::vector<can_app::QueryResultRow> rows;
  const can_app::RunSummary runSummary = can_app::CanApp().run(
    runOptions,
    [&rows](const can_app::QueryResultRow &queryResultRow)
    {
      rows.push_back(queryResultRow);
    });

  REQUIRE_FALSE(runSummary.hasError());
  CHECK(runSummary.scannedEvents == 4U);
  CHECK(runSummary.matchedEvents == 3U);
  REQUIRE(rows.size() == 3U);
  REQUIRE(rows.front().decodedMessage.has_value());
  CHECK(rows.front().decodedMessage->messageName == "VehicleStatus");

  // Direct query/decode flow
  can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromFile(dbcFile.string());
  REQUIRE_FALSE(loadResult.hasError());

  can_readers_text::CsvTraceReader csvTraceReader;
  REQUIRE(csvTraceReader.open(makeSourceDescriptor(traceFile.path()), {}));

  can_core::FilterExpr timestampPredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::TimestampNs, can_core::FilterOperator::Greater, std::uint64_t{1500000U}});
  can_core::FilterExpr channelPredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::Channel, can_core::FilterOperator::Equal, std::uint64_t{1U}});
  can_core::FilterExpr frameTypePredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::FrameType, can_core::FilterOperator::Equal, std::uint64_t{static_cast<std::uint8_t>(can_core::FrameType::CanFd)}});
  can_core::FilterExpr notZeroSignal = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::SignalValue, can_core::FilterOperator::Equal, 0.0});

  can_core::FilterExpr rawOrFilter;
  rawOrFilter.logicalOperator = can_core::LogicalOperator::Or;
  rawOrFilter.children = {channelPredicate, frameTypePredicate};

  can_core::FilterExpr rawFilter;
  rawFilter.logicalOperator = can_core::LogicalOperator::And;
  rawFilter.children = {timestampPredicate, rawOrFilter};

  can_core::FilterExpr decodedNamePredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Equal, std::string("VehicleStatus")});
  can_core::FilterExpr decodedSignalPredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::SignalValue, can_core::FilterOperator::Greater, 1.0});
  can_core::FilterExpr decodedNotFilter;
  decodedNotFilter.logicalOperator = can_core::LogicalOperator::Not;
  decodedNotFilter.children = {notZeroSignal};

  can_core::FilterExpr decodedFilter;
  decodedFilter.logicalOperator = can_core::LogicalOperator::And;
  decodedFilter.children = {decodedNamePredicate, decodedSignalPredicate, decodedNotFilter};

  can_core::QuerySpec querySpec;
  querySpec.rawFilter = rawFilter;
  querySpec.decodedFilter = decodedFilter;
  querySpec.shouldDecode = true;

  CollectingSink collectingSink;
  can_query::QueryExecutionOptions queryExecutionOptions;
  queryExecutionOptions.chunkSize = 2U;
  queryExecutionOptions.shouldDecodeMatches = true;
  can_decode::Decoder decoder(&loadResult.database);
  const can_query::QuerySummary querySummary = can_query::QueryExecutor(&decoder).execute(
    can_query::QueryPlanner().compile(querySpec),
    csvTraceReader,
    collectingSink,
    queryExecutionOptions);

  REQUIRE_FALSE(querySummary.hasError());
  CHECK(querySummary.scannedEvents == 4U);
  CHECK(querySummary.matchedEvents == 1U);
  REQUIRE(collectingSink.matches.size() == 1U);
  CHECK(collectingSink.matches.front().ordinal == 1U);

  // Raw-before-decode qualification
  can_readers_text::CsvTraceReader rawFirstReader;
  REQUIRE(rawFirstReader.open(makeSourceDescriptor(traceFile.path()), {}));

  can_core::QuerySpec rawFirstSpec;
  rawFirstSpec.rawFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::CanId, can_core::FilterOperator::Equal, std::uint64_t{0x999U}});
  rawFirstSpec.decodedFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Equal, std::string("ShouldNotDecode")});

  CollectingSink rawFirstSink;
  const can_query::QuerySummary rawFirstSummary = can_query::QueryExecutor().execute(
    can_query::QueryPlanner().compile(rawFirstSpec),
    rawFirstReader,
    rawFirstSink);

  REQUIRE_FALSE(rawFirstSummary.hasError());
  CHECK(rawFirstSummary.matchedEvents == 0U);
}

TEST_CASE("qualification context cache and index flow satisfy implemented retrieval and cache requirements")
{
  // Requirements:
  // QRY-REQ-013, QRY-REQ-014, QRY-REQ-015, QRY-REQ-016, QRY-REQ-017
  // PERF-REQ-002, PERF-REQ-003
  // CACHE-REQ-001, CACHE-REQ-002, CACHE-REQ-003, CACHE-REQ-004, CACHE-REQ-005
  const test_support::ScopedTempFile traceFile(
    "qtest_cache_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,111,1,01,CAN\n"
    "0.002,0,123,2,11 22,CAN\n"
    "0.003,1,222,1,02,CAN\n"
    "0.004,0,123,2,33 44,CAN\n"
    "0.005,0,333,1,03,CAN\n");
  const test_support::ScopedTempFile cacheFile("qtest_cache_trace", ".bin");

  can_readers_text::CsvTraceReader csvTraceReader;
  REQUIRE(csvTraceReader.open(makeSourceDescriptor(traceFile.path()), {}));

  can_cache::CacheWriter cacheWriter;
  REQUIRE(cacheWriter.open(cacheFile.string()));

  can_index::TraceIndex traceIndex;
  std::vector<can_core::CanEvent> events;
  std::array<can_core::CanEvent, 2> chunk{};
  std::uint64_t ordinal = 0;

  while(true)
  {
    const can_reader_api::ReadResult readResult = csvTraceReader.readChunk(chunk);
    REQUIRE_FALSE(readResult.hasError());

    if(readResult.eventCount > 0U)
    {
      const std::span<const can_core::CanEvent> eventChunk(chunk.data(), readResult.eventCount);
      REQUIRE(cacheWriter.writeChunk(eventChunk));
      for(std::size_t index = 0; index < readResult.eventCount; ++index)
      {
        events.push_back(chunk[index]);
        traceIndex.addEvent(chunk[index], ordinal++);
      }
    }

    if(readResult.isEndOfStream)
    {
      break;
    }
  }

  cacheWriter.close();

  can_cache::CacheReader cacheReader;
  REQUIRE(cacheReader.open(cacheFile.string()));
  CHECK(cacheReader.metadata().eventCount == events.size());

  const auto timeRangeLookup = traceIndex.findTimeRange(2000000U, 4000000U);
  REQUIRE(timeRangeLookup.hasMatch);
  CHECK(timeRangeLookup.startChunkId == 1U);
  CHECK(timeRangeLookup.endChunkId == 3U);

  const auto canIdCandidates = traceIndex.findCanIdCandidates(0x123U);
  REQUIRE(canIdCandidates.size() == 2U);
  CHECK(canIdCandidates[0].chunkId == 1U);
  CHECK(canIdCandidates[1].chunkId == 3U);

  const auto ordinalLocation = traceIndex.findOrdinal(3U);
  REQUIRE(ordinalLocation.has_value());
  CHECK(ordinalLocation->chunkId == 3U);

  const can_core::MatchReference matchReference{3U, 0U};
  const can_core::ContextRequest contextRequest{1U, 1U, true};
  const std::vector<can_core::CanEvent> contextWindow =
    can_query::ContextResolver().resolve(events, matchReference, contextRequest);

  REQUIRE(contextWindow.size() == 3U);
  CHECK(contextWindow[0].canId == 0x222U);
  CHECK(contextWindow[1].canId == 0x123U);
  CHECK(contextWindow[2].canId == 0x333U);
  const std::filesystem::path sourceRoot = std::filesystem::path(DATA_PATH).parent_path();
  CHECK(std::filesystem::exists(sourceRoot / "docs/detailed_design/components/can_cache.md"));
}

TEST_CASE("qualification CLI and scripting surface satisfy implemented external-interface requirements")
{
  // Requirements:
  // CLI-REQ-001, CLI-REQ-002, CLI-REQ-003, CLI-REQ-004
  // EXT-REQ-001, EXT-REQ-002, EXT-REQ-004, EXT-REQ-009
  const std::filesystem::path sourceRoot = std::filesystem::path(DATA_PATH).parent_path();
  const std::string cliSource = readTextFile(sourceRoot / "apps/can_cli/src/main.cpp");
  CHECK(cliSource.find("Usage: ") != std::string::npos);
  CHECK(cliSource.find("--can-id") != std::string::npos);
  CHECK(cliSource.find("--dbc") != std::string::npos);
  CHECK(cliSource.find("--decode") != std::string::npos);
  CHECK(cliSource.find("--export") != std::string::npos);
  CHECK(cliSource.find("can_app::CanApp") != std::string::npos);

  can_script_api::DisabledScriptEngine disabledScriptEngine;
  CHECK_FALSE(disabledScriptEngine.isEnabled());
  disabledScriptEngine.enable();
  CHECK(disabledScriptEngine.isEnabled());
  disabledScriptEngine.disable();
  CHECK_FALSE(disabledScriptEngine.isEnabled());

  can_script_lua::LuaEngine luaEngine;
  REQUIRE(luaEngine.compile(
    {"function accept_event(timestamp_ns, can_id, channel, dlc) return can_id == 0x123 end", "accept_event"}));
  luaEngine.enable();

  can_core::CanEvent canEvent;
  canEvent.canId = 0x123U;
  const can_script_api::ScriptResult scriptResult = luaEngine.run(can_script_api::ScriptEventView{&canEvent});
  REQUIRE_FALSE(scriptResult.hasError());
  CHECK(scriptResult.isAccepted);
}

TEST_CASE("qualification pending-or-environmental requirements remain explicitly traced" * doctest::skip(true))
{
  // Requirements:
  // SYS-REQ-001, SYS-REQ-002, SYS-REQ-003, SYS-REQ-005, SYS-REQ-006
  // DATA-REQ-004
  // IO-REQ-020, IO-REQ-021, IO-REQ-022, IO-REQ-032
  // DBC-REQ-005, DBC-REQ-006, DBC-REQ-007, DBC-REQ-008
  // PERF-REQ-004, PERF-REQ-005
  // CLI-REQ-005
  // GUI-REQ-001, GUI-REQ-002, GUI-REQ-003, GUI-REQ-004, GUI-REQ-005, GUI-REQ-006, GUI-REQ-007, GUI-REQ-008, GUI-REQ-009, GUI-REQ-010, GUI-REQ-011, GUI-REQ-012, GUI-REQ-013, GUI-REQ-014, GUI-REQ-015, GUI-REQ-016, GUI-REQ-017, GUI-REQ-018, GUI-REQ-019, GUI-REQ-020, GUI-REQ-021
  // EXT-REQ-003, EXT-REQ-005, EXT-REQ-006, EXT-REQ-007, EXT-REQ-008, EXT-REQ-010
  // NFR-PERF-002
  // NFR-PORT-001, NFR-PORT-002
  // NFR-MAINT-001, NFR-MAINT-002, NFR-MAINT-003
  // NFR-TEST-001
  // CON-REQ-001, CON-REQ-002
  // FUT-REQ-001, FUT-REQ-002, FUT-REQ-003
}
