#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include "can_decode/decoder.hpp"
#include "can_export/exporter.hpp"
#include "can_query/query_executor.hpp"

namespace can_app
{
struct RunOptions
{
  std::string tracePath;
  std::optional<std::string> dbcPath;
  std::optional<std::uint32_t> canIdFilter;
  bool shouldDecodeMatches = false;
  std::optional<can_export::ExportRequest> exportRequest;
};

struct QueryResultRow
{
  std::uint64_t ordinal = 0;
  can_core::CanEvent canEvent;
  std::optional<can_decode::DecodedMessage> decodedMessage;
};

using QueryResultCallback = std::function<void(const QueryResultRow &)>;

struct RunSummary
{
  std::uint64_t scannedEvents = 0;
  std::uint64_t matchedEvents = 0;
  can_core::ErrorInfo errorInfo;

  [[nodiscard]] bool hasError() const
  {
    return errorInfo.hasError();
  }
};

class CanApp
{
public:
  [[nodiscard]] RunSummary run(const RunOptions &runOptions, const QueryResultCallback &queryResultCallback) const;
};
} // namespace can_app
