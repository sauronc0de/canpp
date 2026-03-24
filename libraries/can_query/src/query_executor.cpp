#include "can_query/query_executor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace can_query
{
namespace
{
std::string normalizeForCaseInsensitiveCompare(std::string_view value)
{
  std::string normalizedValue(value);
  std::transform(normalizedValue.begin(), normalizedValue.end(), normalizedValue.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return normalizedValue;
}

bool isEmptyFilter(const can_core::FilterExpr &filterExpr)
{
  return !filterExpr.predicate.has_value() && filterExpr.children.empty();
}

std::optional<double> toDouble(const can_core::FilterValue &filterValue)
{
  return std::visit(
    [](const auto &value) -> std::optional<double>
    {
      using ValueType = std::decay_t<decltype(value)>;
      if constexpr(std::is_same_v<ValueType, std::monostate>)
      {
        return std::nullopt;
      }
      else if constexpr(std::is_same_v<ValueType, bool>)
      {
        return value ? 1.0 : 0.0;
      }
      else if constexpr(std::is_same_v<ValueType, std::uint64_t> || std::is_same_v<ValueType, std::int64_t> ||
                        std::is_same_v<ValueType, double>)
      {
        return static_cast<double>(value);
      }
      else
      {
        return std::nullopt;
      }
    },
    filterValue);
}

std::optional<std::string_view> toStringView(const can_core::FilterValue &filterValue)
{
  if(const auto *value = std::get_if<std::string>(&filterValue))
  {
    return *value;
  }

  return std::nullopt;
}

bool compareNumeric(double leftValue, double rightValue, can_core::FilterOperator filterOperator)
{
  switch(filterOperator)
  {
  case can_core::FilterOperator::Equal:
    return leftValue == rightValue;
  case can_core::FilterOperator::NotEqual:
    return leftValue != rightValue;
  case can_core::FilterOperator::Less:
    return leftValue < rightValue;
  case can_core::FilterOperator::LessOrEqual:
    return leftValue <= rightValue;
  case can_core::FilterOperator::Greater:
    return leftValue > rightValue;
  case can_core::FilterOperator::GreaterOrEqual:
    return leftValue >= rightValue;
  case can_core::FilterOperator::Contains:
    return false;
  }

  return false;
}

bool compareString(std::string_view leftValue, std::string_view rightValue, can_core::FilterOperator filterOperator)
{
  const std::string normalizedLeftValue = normalizeForCaseInsensitiveCompare(leftValue);
  const std::string normalizedRightValue = normalizeForCaseInsensitiveCompare(rightValue);

  switch(filterOperator)
  {
  case can_core::FilterOperator::Equal:
    return normalizedLeftValue == normalizedRightValue;
  case can_core::FilterOperator::NotEqual:
    return normalizedLeftValue != normalizedRightValue;
  case can_core::FilterOperator::Contains:
    return normalizedLeftValue.find(normalizedRightValue) != std::string::npos;
  case can_core::FilterOperator::Less:
  case can_core::FilterOperator::LessOrEqual:
  case can_core::FilterOperator::Greater:
  case can_core::FilterOperator::GreaterOrEqual:
    return false;
  }

  return false;
}

bool evaluateRawPredicate(const can_core::Predicate &predicate, const can_core::CanEvent &canEvent)
{
  switch(predicate.field)
  {
  case can_core::FilterField::TimestampNs:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
      compareNumeric(static_cast<double>(canEvent.timestampNs), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::CanId:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
      compareNumeric(static_cast<double>(canEvent.canId), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::Channel:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
      compareNumeric(static_cast<double>(canEvent.channel), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::FrameType:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() &&
      compareNumeric(static_cast<double>(static_cast<std::uint8_t>(canEvent.frameType)), *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::MessageName:
  case can_core::FilterField::SignalName:
  case can_core::FilterField::SignalValue:
    return true;
  }

  return false;
}

bool evaluateRawFilter(const can_core::FilterExpr &filterExpr, const can_core::CanEvent &canEvent)
{
  if(isEmptyFilter(filterExpr))
  {
    return true;
  }

  if(filterExpr.predicate.has_value())
  {
    return evaluateRawPredicate(*filterExpr.predicate, canEvent);
  }

  if(filterExpr.logicalOperator == can_core::LogicalOperator::Not)
  {
    if(filterExpr.children.empty())
    {
      return true;
    }

    return !evaluateRawFilter(filterExpr.children.front(), canEvent);
  }

  const bool initialValue = filterExpr.logicalOperator == can_core::LogicalOperator::And;
  bool result = initialValue;
  for(const can_core::FilterExpr &child : filterExpr.children)
  {
    const bool childValue = evaluateRawFilter(child, canEvent);
    if(filterExpr.logicalOperator == can_core::LogicalOperator::And)
    {
      result = result && childValue;
      if(!result)
      {
        return false;
      }
    }
    else
    {
      result = result || childValue;
      if(result)
      {
        return true;
      }
    }
  }

  return result;
}

bool matchesSignalName(std::string_view signalName, const can_decode::DecodedMessage &decodedMessage, can_core::FilterOperator filterOperator)
{
  for(const can_decode::DecodedSignal &decodedSignal : decodedMessage.signals)
  {
    if(compareString(decodedSignal.name, signalName, filterOperator))
    {
      return true;
    }
  }

  return false;
}

bool matchesSignalValue(double signalValue, const can_decode::DecodedMessage &decodedMessage, can_core::FilterOperator filterOperator)
{
  for(const can_decode::DecodedSignal &decodedSignal : decodedMessage.signals)
  {
    const auto decodedValue = std::visit(
      [](const auto &value) -> double
      {
        return static_cast<double>(value);
      },
      decodedSignal.value);

    if(compareNumeric(decodedValue, signalValue, filterOperator))
    {
      return true;
    }
  }

  return false;
}

bool evaluateDecodedPredicate(const can_core::Predicate &predicate, const can_decode::DecodedMessage &decodedMessage)
{
  switch(predicate.field)
  {
  case can_core::FilterField::MessageName:
  {
    const auto predicateValue = toStringView(predicate.value);
    return predicateValue.has_value() && compareString(decodedMessage.messageName, *predicateValue, predicate.filterOperator);
  }
  case can_core::FilterField::SignalName:
  {
    const auto predicateValue = toStringView(predicate.value);
    return predicateValue.has_value() && matchesSignalName(*predicateValue, decodedMessage, predicate.filterOperator);
  }
  case can_core::FilterField::SignalValue:
  {
    const auto predicateValue = toDouble(predicate.value);
    return predicateValue.has_value() && matchesSignalValue(*predicateValue, decodedMessage, predicate.filterOperator);
  }
  case can_core::FilterField::TimestampNs:
  case can_core::FilterField::CanId:
  case can_core::FilterField::Channel:
  case can_core::FilterField::FrameType:
    return true;
  }

  return false;
}

bool evaluateDecodedFilter(const can_core::FilterExpr &filterExpr, const can_decode::DecodedMessage &decodedMessage)
{
  if(isEmptyFilter(filterExpr))
  {
    return true;
  }

  if(filterExpr.predicate.has_value())
  {
    return evaluateDecodedPredicate(*filterExpr.predicate, decodedMessage);
  }

  if(filterExpr.logicalOperator == can_core::LogicalOperator::Not)
  {
    if(filterExpr.children.empty())
    {
      return true;
    }

    return !evaluateDecodedFilter(filterExpr.children.front(), decodedMessage);
  }

  const bool initialValue = filterExpr.logicalOperator == can_core::LogicalOperator::And;
  bool result = initialValue;
  for(const can_core::FilterExpr &child : filterExpr.children)
  {
    const bool childValue = evaluateDecodedFilter(child, decodedMessage);
    if(filterExpr.logicalOperator == can_core::LogicalOperator::And)
    {
      result = result && childValue;
      if(!result)
      {
        return false;
      }
    }
    else
    {
      result = result || childValue;
      if(result)
      {
        return true;
      }
    }
  }

  return result;
}
} // namespace

CompiledQuery QueryPlanner::compile(const can_core::QuerySpec &querySpec) const
{
  CompiledQuery compiledQuery;
  compiledQuery.querySpec = querySpec;
  compiledQuery.requiresDecode = can_core::requiresDecode(querySpec);
  return compiledQuery;
}

QueryExecutor::QueryExecutor(const can_decode::Decoder *decoder)
  : decoder_(decoder)
{
}

void QueryExecutor::setDecoder(const can_decode::Decoder *decoder)
{
  decoder_ = decoder;
}

QuerySummary QueryExecutor::execute(
  const CompiledQuery &compiledQuery,
  can_reader_api::ITraceReader &traceReader,
  IResultSink &resultSink,
  const QueryExecutionOptions &queryExecutionOptions) const
{
  QuerySummary querySummary;
  const auto publishProgress = [&]()
  {
    if(!queryExecutionOptions.progressCallback)
    {
      return;
    }

    const can_core::TraceMetadata traceMetadata = traceReader.metadata();
    queryExecutionOptions.progressCallback(
      {querySummary.scannedEvents, querySummary.matchedEvents, traceMetadata.consumedSizeBytes, traceMetadata.sourceSizeBytes});
  };

  std::vector<can_core::CanEvent> eventBuffer(queryExecutionOptions.chunkSize);
  std::uint64_t ordinal = 0;

  while(true)
  {
    if(queryExecutionOptions.shouldCancel != nullptr && queryExecutionOptions.shouldCancel->load())
    {
      querySummary.wasCancelled = true;
      publishProgress();
      return querySummary;
    }

    can_reader_api::ReadResult readResult = traceReader.readChunk(eventBuffer);
    if(readResult.hasError())
    {
      querySummary.errorInfo = readResult.errorInfo;
      publishProgress();
      return querySummary;
    }

    for(std::size_t index = 0; index < readResult.eventCount; ++index)
    {
      if(queryExecutionOptions.shouldCancel != nullptr && queryExecutionOptions.shouldCancel->load())
      {
        querySummary.wasCancelled = true;
        publishProgress();
        return querySummary;
      }

      if(queryExecutionOptions.endOrdinal.has_value() && ordinal > *queryExecutionOptions.endOrdinal)
      {
        publishProgress();
        return querySummary;
      }

      if(queryExecutionOptions.startOrdinal.has_value() && ordinal < *queryExecutionOptions.startOrdinal)
      {
        ++ordinal;
        continue;
      }

      ++querySummary.scannedEvents;
      const can_core::CanEvent &canEvent = eventBuffer[index];
      if(!evaluateRawFilter(compiledQuery.querySpec.rawFilter, canEvent))
      {
        ++ordinal;
        continue;
      }

      QueryMatch queryMatch;
      queryMatch.ordinal = ordinal;
      queryMatch.canEvent = canEvent;

      bool isMatch = true;
      if(compiledQuery.requiresDecode)
      {
        if(decoder_ == nullptr)
        {
          querySummary.errorInfo.code = can_core::ErrorCode::DecodeFailure;
          querySummary.errorInfo.message = "Decoder is required but not configured";
          publishProgress();
          return querySummary;
        }

        const can_decode::DecodeResult decodeResult = decoder_->decode(canEvent);
        if(decodeResult.hasError())
        {
          ++ordinal;
          continue;
        }

        if(compiledQuery.querySpec.decodedFilter.has_value())
        {
          isMatch = evaluateDecodedFilter(*compiledQuery.querySpec.decodedFilter, decodeResult.decodedMessage);
        }

        if(isMatch && (queryExecutionOptions.shouldDecodeMatches || compiledQuery.querySpec.shouldDecode))
        {
          queryMatch.decodedMessage = decodeResult.decodedMessage;
        }
      }

      if(isMatch)
      {
        ++querySummary.matchedEvents;
        resultSink.onMatch(queryMatch);
        if(queryExecutionOptions.shouldStopAtFirstMatch)
        {
          publishProgress();
          return querySummary;
        }
        if(queryExecutionOptions.maxMatches.has_value() &&
          querySummary.matchedEvents >= *queryExecutionOptions.maxMatches)
        {
          publishProgress();
          return querySummary;
        }
      }

      ++ordinal;
    }

    publishProgress();

    if(readResult.isEndOfStream)
    {
      publishProgress();
      return querySummary;
    }
  }
}

std::vector<can_core::CanEvent> ContextResolver::resolve(
  const std::vector<can_core::CanEvent> &events,
  const can_core::MatchReference &matchReference,
  const can_core::ContextRequest &contextRequest) const
{
  if(events.empty() || !contextRequest.isEnabled || matchReference.ordinal >= events.size())
  {
    return {};
  }

  const std::size_t beginIndex = matchReference.ordinal > contextRequest.beforeCount
    ? static_cast<std::size_t>(matchReference.ordinal - contextRequest.beforeCount)
    : 0;
  const std::size_t endIndex = std::min(
    events.size(),
    static_cast<std::size_t>(matchReference.ordinal) + contextRequest.afterCount + 1U);

  return std::vector<can_core::CanEvent>(events.begin() + static_cast<std::ptrdiff_t>(beginIndex),
    events.begin() + static_cast<std::ptrdiff_t>(endIndex));
}
} // namespace can_query
