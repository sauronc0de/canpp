#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "can_dbc/database.hpp"
#include "can_decode/decoder.hpp"
#include "can_query/query_executor.hpp"
#include "can_readers_text/text_trace_reader.hpp"
#include "test_support.hpp"

namespace
{
can_reader_api::SourceDescriptor makeSourceDescriptor(const std::filesystem::path &path)
{
  return {path.string(), path.extension().string(), true};
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

TEST_CASE("architecture query flow preserves source order and applies raw filtering before decoded filtering")
{
  const test_support::ScopedTempFile traceFile(
    "query_flow_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,123,2,2A 00,CAN\n"
    "0.002,0,456,1,10,CAN\n"
    "0.003,1,123,2,05 00,CAN\n"
    "0.004,0,123,2,64 00,CAN\n");
  const test_support::ScopedTempFile dbcFile(
    "query_flow_dbc",
    ".dbc",
    "BO_ 291 VehicleStatus: 8 Vector__XXX\n"
    " SG_ VehicleSpeed : 0|16@1+ (1,0) [0|65535] \"km/h\" Vector__XXX\n");

  can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromFile(dbcFile.string());
  REQUIRE_FALSE(loadResult.hasError());

  can_decode::Decoder decoder(&loadResult.database);
  can_readers_text::CsvTraceReader csvTraceReader;
  REQUIRE(csvTraceReader.open(makeSourceDescriptor(traceFile.path()), {}));

  can_core::FilterExpr rawCanIdFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::CanId, can_core::FilterOperator::Equal, std::uint64_t{0x123U}});
  can_core::FilterExpr decodedNameFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Equal, std::string("VehicleStatus")});
  can_core::FilterExpr decodedValueFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::SignalValue, can_core::FilterOperator::Greater, 10.0});

  can_core::QuerySpec querySpec;
  querySpec.rawFilter.logicalOperator = can_core::LogicalOperator::And;
  querySpec.rawFilter.children = {rawCanIdFilter};
  querySpec.decodedFilter = can_core::FilterExpr{};
  querySpec.decodedFilter->logicalOperator = can_core::LogicalOperator::And;
  querySpec.decodedFilter->children = {decodedNameFilter, decodedValueFilter};
  querySpec.shouldDecode = true;

  can_query::QueryExecutionOptions queryExecutionOptions;
  queryExecutionOptions.chunkSize = 2U;
  queryExecutionOptions.shouldDecodeMatches = true;

  CollectingSink collectingSink;
  const can_query::QuerySummary querySummary = can_query::QueryExecutor(&decoder).execute(
    can_query::QueryPlanner().compile(querySpec),
    csvTraceReader,
    collectingSink,
    queryExecutionOptions);

  REQUIRE_FALSE(querySummary.hasError());
  CHECK(querySummary.scannedEvents == 4U);
  CHECK(querySummary.matchedEvents == 2U);
  REQUIRE(collectingSink.matches.size() == 2U);
  CHECK(collectingSink.matches[0].ordinal == 0U);
  CHECK(collectingSink.matches[1].ordinal == 3U);
  REQUIRE(collectingSink.matches[0].decodedMessage.has_value());
  REQUIRE(collectingSink.matches[1].decodedMessage.has_value());
  CHECK(collectingSink.matches[0].decodedMessage->messageName == "VehicleStatus");
}

TEST_CASE("architecture context flow resolves windows against original trace order")
{
  const test_support::ScopedTempFile traceFile(
    "context_flow_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,111,1,01,CAN\n"
    "0.002,0,123,2,2A 00,CAN\n"
    "0.003,0,222,1,02,CAN\n"
    "0.004,1,123,2,05 00,CAN\n"
    "0.005,0,333,1,03,CAN\n");

  can_readers_text::CsvTraceReader csvTraceReader;
  REQUIRE(csvTraceReader.open(makeSourceDescriptor(traceFile.path()), {}));

  std::vector<can_core::CanEvent> events;
  std::array<can_core::CanEvent, 2> chunk{};
  while(true)
  {
    const can_reader_api::ReadResult readResult = csvTraceReader.readChunk(chunk);
    REQUIRE_FALSE(readResult.hasError());
    for(std::size_t index = 0; index < readResult.eventCount; ++index)
    {
      events.push_back(chunk[index]);
    }

    if(readResult.isEndOfStream)
    {
      break;
    }
  }

  REQUIRE(events.size() == 5U);

  can_core::QuerySpec querySpec;
  querySpec.rawFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::CanId, can_core::FilterOperator::Equal, std::uint64_t{0x123U}});

  class VectorTraceReader : public can_reader_api::ITraceReader
  {
  public:
    explicit VectorTraceReader(const std::vector<can_core::CanEvent> &sourceEvents)
      : sourceEvents_(sourceEvents)
    {
    }

    bool open(const can_reader_api::SourceDescriptor &, const can_reader_api::ReaderOptions &) override
    {
      cursor_ = 0U;
      return true;
    }

    can_reader_api::ReadResult readChunk(std::span<can_core::CanEvent> outputBuffer) override
    {
      can_reader_api::ReadResult readResult;
      const std::size_t remaining = sourceEvents_.size() - cursor_;
      const std::size_t copied = std::min(outputBuffer.size(), remaining);
      std::copy_n(sourceEvents_.begin() + static_cast<std::ptrdiff_t>(cursor_), copied, outputBuffer.begin());
      cursor_ += copied;
      readResult.eventCount = copied;
      readResult.isEndOfStream = cursor_ >= sourceEvents_.size();
      return readResult;
    }

    [[nodiscard]] can_core::TraceMetadata metadata() const override
    {
      return {};
    }

    [[nodiscard]] can_reader_api::ReaderCapabilities capabilities() const override
    {
      return {};
    }

    void close() override
    {
      cursor_ = sourceEvents_.size();
    }

  private:
    const std::vector<can_core::CanEvent> &sourceEvents_;
    std::size_t cursor_ = 0U;
  };

  VectorTraceReader vectorTraceReader(events);
  CollectingSink collectingSink;
  const can_query::QuerySummary querySummary = can_query::QueryExecutor().execute(
    can_query::QueryPlanner().compile(querySpec),
    vectorTraceReader,
    collectingSink);

  REQUIRE_FALSE(querySummary.hasError());
  REQUIRE(collectingSink.matches.size() == 2U);
  CHECK(collectingSink.matches[0].ordinal == 1U);
  CHECK(collectingSink.matches[1].ordinal == 3U);

  const can_core::MatchReference matchReference{collectingSink.matches[1].ordinal, 0U};
  const can_core::ContextRequest contextRequest{1U, 1U, true};
  const std::vector<can_core::CanEvent> contextWindow =
    can_query::ContextResolver().resolve(events, matchReference, contextRequest);

  REQUIRE(contextWindow.size() == 3U);
  CHECK(contextWindow[0].canId == 0x222U);
  CHECK(contextWindow[1].canId == 0x123U);
  CHECK(contextWindow[2].canId == 0x333U);
}
