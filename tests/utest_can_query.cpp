#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

#include "can_dbc/database.hpp"
#include "can_decode/decoder.hpp"
#include "can_query/query_executor.hpp"

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

can_dbc::Database makeDatabase()
{
  can_dbc::Database database;

  can_dbc::MessageDefinition vehicleStatus;
  vehicleStatus.canId = 0x123U;
  vehicleStatus.name = "VehicleStatus";
  vehicleStatus.dlc = 8U;
  vehicleStatus.signalDefinitions.push_back(
    {"VehicleSpeed", 0U, 16U, true, false, 1.0, 0.0, 0.0, 0.0, "km/h", can_dbc::SignalValueType::UnsignedInteger});
  database.addMessage(vehicleStatus);

  can_dbc::MessageDefinition otherStatus;
  otherStatus.canId = 0x456U;
  otherStatus.name = "OtherStatus";
  otherStatus.dlc = 8U;
  otherStatus.signalDefinitions.push_back(
    {"Counter", 0U, 8U, true, false, 1.0, 0.0, 0.0, 0.0, "", can_dbc::SignalValueType::UnsignedInteger});
  database.addMessage(otherStatus);

  return database;
}

class VectorTraceReader : public can_reader_api::ITraceReader
{
public:
  explicit VectorTraceReader(std::vector<can_core::CanEvent> events)
    : events_(std::move(events))
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
    const std::size_t availableEvents = events_.size() - cursor_;
    const std::size_t copiedEvents = std::min(outputBuffer.size(), availableEvents);
    std::copy_n(events_.begin() + static_cast<std::ptrdiff_t>(cursor_), copiedEvents, outputBuffer.begin());
    cursor_ += copiedEvents;
    readResult.eventCount = copiedEvents;
    readResult.isEndOfStream = cursor_ >= events_.size();
    return readResult;
  }

  [[nodiscard]] can_core::TraceMetadata metadata() const override
  {
    can_core::TraceMetadata metadata;
    metadata.eventCount = events_.size();
    if(!events_.empty())
    {
      metadata.startTimestampNs = events_.front().timestampNs;
      metadata.endTimestampNs = events_.back().timestampNs;
    }

    return metadata;
  }

  [[nodiscard]] can_reader_api::ReaderCapabilities capabilities() const override
  {
    can_reader_api::ReaderCapabilities capabilities;
    capabilities.formatName = "vector";
    return capabilities;
  }

  void close() override
  {
    cursor_ = events_.size();
  }

private:
  std::vector<can_core::CanEvent> events_;
  std::size_t cursor_ = 0U;
};

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

TEST_CASE("can_query planner tracks when decoded evaluation is required")
{
  can_query::QueryPlanner queryPlanner;
  can_core::QuerySpec querySpec;

  auto compiledQuery = queryPlanner.compile(querySpec);
  CHECK_FALSE(compiledQuery.requiresDecode);

  querySpec.decodedFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Equal, std::string("VehicleStatus")});
  compiledQuery = queryPlanner.compile(querySpec);
  CHECK(compiledQuery.requiresDecode);
}

TEST_CASE("can_query executes raw predicates in streaming order")
{
  can_query::QueryPlanner queryPlanner;
  can_core::QuerySpec querySpec;
  querySpec.rawFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::CanId, can_core::FilterOperator::Equal, std::uint64_t{0x123U}});

  VectorTraceReader traceReader({
    makeEvent(100U, 0x123U, {0x01U}),
    makeEvent(200U, 0x456U, {0x02U}),
    makeEvent(300U, 0x123U, {0x03U}),
  });
  CollectingSink resultSink;
  can_query::QueryExecutor queryExecutor;
  can_query::QueryExecutionOptions queryExecutionOptions;
  queryExecutionOptions.chunkSize = 2U;

  const can_query::QuerySummary querySummary = queryExecutor.execute(
    queryPlanner.compile(querySpec),
    traceReader,
    resultSink,
    queryExecutionOptions);

  REQUIRE_FALSE(querySummary.hasError());
  CHECK(querySummary.scannedEvents == 3U);
  CHECK(querySummary.matchedEvents == 2U);
  REQUIRE(resultSink.matches.size() == 2U);
  CHECK(resultSink.matches[0].ordinal == 0U);
  CHECK(resultSink.matches[1].ordinal == 2U);
  CHECK(resultSink.matches[1].canEvent.payload[0] == 0x03U);
}

TEST_CASE("can_query uses the decoder for decoded predicates and projected matches")
{
  const can_dbc::Database database = makeDatabase();
  const can_decode::Decoder decoder(&database);

  can_core::QuerySpec querySpec;
  querySpec.decodedFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Equal, std::string("VehicleStatus")});
  querySpec.shouldDecode = true;

  VectorTraceReader traceReader({
    makeEvent(100U, 0x123U, {0x2AU, 0x00U}),
    makeEvent(200U, 0x456U, {0x10U}),
  });
  CollectingSink resultSink;
  can_query::QueryExecutor queryExecutor(&decoder);
  can_query::QueryExecutionOptions queryExecutionOptions;
  queryExecutionOptions.chunkSize = 1U;
  queryExecutionOptions.shouldDecodeMatches = true;

  const can_query::QuerySummary querySummary = queryExecutor.execute(
    can_query::QueryPlanner().compile(querySpec),
    traceReader,
    resultSink,
    queryExecutionOptions);

  REQUIRE_FALSE(querySummary.hasError());
  CHECK(querySummary.scannedEvents == 2U);
  CHECK(querySummary.matchedEvents == 1U);
  REQUIRE(resultSink.matches.size() == 1U);
  REQUIRE(resultSink.matches.front().decodedMessage.has_value());
  CHECK(resultSink.matches.front().decodedMessage->messageName == "VehicleStatus");
}

TEST_CASE("can_query fails fast when decode is required without a configured decoder")
{
  can_core::QuerySpec querySpec;
  querySpec.decodedFilter = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::MessageName, can_core::FilterOperator::Equal, std::string("VehicleStatus")});

  VectorTraceReader traceReader({makeEvent(100U, 0x123U, {0x01U})});
  CollectingSink resultSink;
  can_query::QueryExecutor queryExecutor;

  const can_query::QuerySummary querySummary = queryExecutor.execute(
    can_query::QueryPlanner().compile(querySpec),
    traceReader,
    resultSink);

  REQUIRE(querySummary.hasError());
  CHECK(querySummary.errorInfo.code == can_core::ErrorCode::DecodeFailure);
}

TEST_CASE("can_query resolves context windows around a match")
{
  const std::vector<can_core::CanEvent> events{
    makeEvent(100U, 0x100U, {0x01U}),
    makeEvent(200U, 0x101U, {0x02U}),
    makeEvent(300U, 0x102U, {0x03U}),
    makeEvent(400U, 0x103U, {0x04U}),
    makeEvent(500U, 0x104U, {0x05U}),
  };
  const can_core::MatchReference matchReference{2U, 0U};
  const can_core::ContextRequest contextRequest{1U, 2U, true};

  const std::vector<can_core::CanEvent> contextWindow =
    can_query::ContextResolver().resolve(events, matchReference, contextRequest);

  REQUIRE(contextWindow.size() == 4U);
  CHECK(contextWindow.front().canId == 0x101U);
  CHECK(contextWindow.back().canId == 0x104U);
}
