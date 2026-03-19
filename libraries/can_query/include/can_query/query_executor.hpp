#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "can_core/core_types.hpp"
#include "can_decode/decoder.hpp"
#include "can_reader_api/trace_reader.hpp"

namespace can_query
{
struct QueryExecutionOptions
{
  std::size_t chunkSize = 4096;
  bool shouldStopAtFirstMatch = false;
  bool shouldDecodeMatches = false;
};

struct CompiledQuery
{
  can_core::QuerySpec querySpec;
  bool requiresDecode = false;
};

struct QueryMatch
{
  std::uint64_t ordinal = 0;
  can_core::CanEvent canEvent;
  std::optional<can_decode::DecodedMessage> decodedMessage;
};

struct QuerySummary
{
  std::uint64_t scannedEvents = 0;
  std::uint64_t matchedEvents = 0;
  can_core::ErrorInfo errorInfo;

  [[nodiscard]] bool hasError() const
  {
    return errorInfo.hasError();
  }
};

class IResultSink
{
public:
  virtual ~IResultSink() = default;
  virtual void onMatch(const QueryMatch &queryMatch) = 0;
};

class QueryPlanner
{
public:
  [[nodiscard]] CompiledQuery compile(const can_core::QuerySpec &querySpec) const;
};

class QueryExecutor
{
public:
  explicit QueryExecutor(const can_decode::Decoder *decoder = nullptr);

  void setDecoder(const can_decode::Decoder *decoder);
  [[nodiscard]] QuerySummary execute(
    const CompiledQuery &compiledQuery,
    can_reader_api::ITraceReader &traceReader,
    IResultSink &resultSink,
    const QueryExecutionOptions &queryExecutionOptions = {}) const;

private:
  const can_decode::Decoder *decoder_ = nullptr;
};

class ContextResolver
{
public:
  [[nodiscard]] std::vector<can_core::CanEvent> resolve(
    const std::vector<can_core::CanEvent> &events,
    const can_core::MatchReference &matchReference,
    const can_core::ContextRequest &contextRequest) const;
};
} // namespace can_query
