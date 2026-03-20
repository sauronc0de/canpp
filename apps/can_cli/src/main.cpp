#include "log.hpp"
#include "can_app/can_app.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "can_query/query_executor.hpp"
#include "can_readers_text/text_trace_reader.hpp"

namespace
{
struct FilterClause
{
  can_core::FilterField field = can_core::FilterField::CanId;
  can_core::FilterOperator filterOperator = can_core::FilterOperator::Equal;
  can_core::FilterValue value;
};

void printUsage(const char *programName)
{
  std::cout << "Usage: " << programName
            << " <trace-path> [--dbc <dbc-path>] [--can-id <id>] [--decode]"
            << " [--where <field> <op> <value>] [--where-or <field> <op> <value>] [--where-not <field> <op> <value>]"
            << " [--decoded-where <field> <op> <value>] [--decoded-where-or <field> <op> <value>] [--decoded-where-not <field> <op> <value>]"
            << " [--context-ordinal <n>] [--context-before <n>] [--context-after <n>]"
            << " [--export <csv-path>] [--export-decoded]\n"
            << '\n'
            << "Raw query fields:\n"
            << "  timestamp_ns   Event timestamp in nanoseconds\n"
            << "  can_id         CAN identifier, decimal or hex (example: 0x5A0)\n"
            << "  channel        Bus/channel number\n"
            << "  frame_type     can | can20 | fd | canfd\n"
            << '\n'
            << "Decoded query fields (require --dbc, and matches are printed decoded with --decode):\n"
            << "  message_name   DBC message name (example: RLS_01)\n"
            << "  signal_name    DBC signal name (example: LS_Helligkeit_FW)\n"
            << "  signal_value   Decoded numeric signal value\n"
            << '\n'
            << "Operators:\n"
            << "  eq ne lt le gt ge contains\n"
            << '\n'
            << "Logical composition:\n"
            << "  --where / --decoded-where        adds an AND clause\n"
            << "  --where-or / --decoded-where-or  groups OR clauses together\n"
            << "  --where-not / --decoded-where-not negates one clause\n"
            << '\n'
            << "Context retrieval:\n"
            << "  --context-ordinal <n>  selects one event by original trace position\n"
            << "  --context-before <n>   prints n events before that ordinal\n"
            << "  --context-after <n>    prints n events after that ordinal\n"
            << "  Ordinal is zero-based and refers to the full trace, not the filtered match index.\n"
            << '\n'
            << "Examples:\n"
            << "  Filter by CAN ID:\n"
            << "    " << programName << " trace.asc --can-id 0x5A0\n"
            << "  Filter by decoded message name:\n"
            << "    " << programName << " trace.asc --dbc matrix.dbc --decoded-where message_name eq RLS_01 --decode\n"
            << "  Filter by specific signal value:\n"
            << "    " << programName << " trace.asc --dbc matrix.dbc --decoded-where signal_name eq LS_Helligkeit_FW --decoded-where signal_value eq 0 --decode\n"
            << "  Show 3 events before and after ordinal 45172:\n"
            << "    " << programName << " trace.asc --context-ordinal 45172 --context-before 3 --context-after 3\n";
}

std::optional<std::uint32_t> parseCanId(const std::string &value)
{
  char *endPointer = nullptr;
  const unsigned long parsedValue = std::strtoul(value.c_str(), &endPointer, 0);
  if(endPointer != value.c_str() + value.size())
  {
    return std::nullopt;
  }

  return static_cast<std::uint32_t>(parsedValue);
}

std::optional<std::uint64_t> parseUnsignedInteger(const std::string &value)
{
  char *endPointer = nullptr;
  const unsigned long long parsedValue = std::strtoull(value.c_str(), &endPointer, 0);
  if(endPointer != value.c_str() + value.size())
  {
    return std::nullopt;
  }

  return static_cast<std::uint64_t>(parsedValue);
}

std::optional<double> parseDoubleValue(const std::string &value)
{
  char *endPointer = nullptr;
  const double parsedValue = std::strtod(value.c_str(), &endPointer);
  if(endPointer != value.c_str() + value.size())
  {
    return std::nullopt;
  }

  return parsedValue;
}

std::optional<can_core::FilterField> parseFilterField(const std::string &value)
{
  if(value == "timestamp_ns")
  {
    return can_core::FilterField::TimestampNs;
  }
  if(value == "can_id")
  {
    return can_core::FilterField::CanId;
  }
  if(value == "channel")
  {
    return can_core::FilterField::Channel;
  }
  if(value == "frame_type")
  {
    return can_core::FilterField::FrameType;
  }
  if(value == "message_name")
  {
    return can_core::FilterField::MessageName;
  }
  if(value == "signal_name")
  {
    return can_core::FilterField::SignalName;
  }
  if(value == "signal_value")
  {
    return can_core::FilterField::SignalValue;
  }

  return std::nullopt;
}

std::optional<can_core::FilterOperator> parseFilterOperator(const std::string &value)
{
  if(value == "eq")
  {
    return can_core::FilterOperator::Equal;
  }
  if(value == "ne")
  {
    return can_core::FilterOperator::NotEqual;
  }
  if(value == "lt")
  {
    return can_core::FilterOperator::Less;
  }
  if(value == "le")
  {
    return can_core::FilterOperator::LessOrEqual;
  }
  if(value == "gt")
  {
    return can_core::FilterOperator::Greater;
  }
  if(value == "ge")
  {
    return can_core::FilterOperator::GreaterOrEqual;
  }
  if(value == "contains")
  {
    return can_core::FilterOperator::Contains;
  }

  return std::nullopt;
}

std::optional<can_core::FilterValue> parseFilterValue(can_core::FilterField filterField, const std::string &value)
{
  switch(filterField)
  {
  case can_core::FilterField::TimestampNs:
  case can_core::FilterField::CanId:
  case can_core::FilterField::Channel:
  {
    return parseUnsignedInteger(value);
  }
  case can_core::FilterField::FrameType:
  {
    if(value == "can" || value == "can20")
    {
      return static_cast<std::uint64_t>(static_cast<std::uint8_t>(can_core::FrameType::Can20));
    }
    if(value == "fd" || value == "canfd")
    {
      return static_cast<std::uint64_t>(static_cast<std::uint8_t>(can_core::FrameType::CanFd));
    }
    return std::nullopt;
  }
  case can_core::FilterField::MessageName:
  case can_core::FilterField::SignalName:
  {
    return value;
  }
  case can_core::FilterField::SignalValue:
  {
    return parseDoubleValue(value);
  }
  }

  return std::nullopt;
}

std::optional<FilterClause> parseClause(const std::string &fieldText, const std::string &operatorText, const std::string &valueText)
{
  const auto field = parseFilterField(fieldText);
  const auto filterOperator = parseFilterOperator(operatorText);
  if(!field.has_value() || !filterOperator.has_value())
  {
    return std::nullopt;
  }

  const auto parsedValue = parseFilterValue(*field, valueText);
  if(!parsedValue.has_value())
  {
    return std::nullopt;
  }

  FilterClause filterClause;
  filterClause.field = *field;
  filterClause.filterOperator = *filterOperator;
  filterClause.value = *parsedValue;
  return filterClause;
}

can_core::FilterExpr makePredicateExpr(const FilterClause &filterClause)
{
  can_core::Predicate predicate;
  predicate.field = filterClause.field;
  predicate.filterOperator = filterClause.filterOperator;
  predicate.value = filterClause.value;
  return can_core::FilterExpr::makePredicate(std::move(predicate));
}

std::optional<can_core::FilterExpr> buildFilterExpr(
  const std::vector<FilterClause> &andClauses,
  const std::vector<FilterClause> &orClauses,
  const std::vector<FilterClause> &notClauses)
{
  std::vector<can_core::FilterExpr> children;
  for(const FilterClause &filterClause : andClauses)
  {
    children.push_back(makePredicateExpr(filterClause));
  }

  if(!orClauses.empty())
  {
    can_core::FilterExpr orExpr;
    orExpr.logicalOperator = can_core::LogicalOperator::Or;
    for(const FilterClause &filterClause : orClauses)
    {
      orExpr.children.push_back(makePredicateExpr(filterClause));
    }
    children.push_back(std::move(orExpr));
  }

  for(const FilterClause &filterClause : notClauses)
  {
    can_core::FilterExpr notExpr;
    notExpr.logicalOperator = can_core::LogicalOperator::Not;
    notExpr.children.push_back(makePredicateExpr(filterClause));
    children.push_back(std::move(notExpr));
  }

  if(children.empty())
  {
    return std::nullopt;
  }

  if(children.size() == 1U)
  {
    return children.front();
  }

  can_core::FilterExpr filterExpr;
  filterExpr.logicalOperator = can_core::LogicalOperator::And;
  filterExpr.children = std::move(children);
  return filterExpr;
}

std::unique_ptr<can_reader_api::ITraceReader> createReader(const std::string &tracePath)
{
  can_reader_api::SourceDescriptor sourceDescriptor;
  sourceDescriptor.path = tracePath;
  const std::size_t extensionSeparator = tracePath.find_last_of('.');
  if(extensionSeparator != std::string::npos)
  {
    sourceDescriptor.extension = tracePath.substr(extensionSeparator);
  }

  const can_readers_text::CandumpReaderFactory candumpReaderFactory;
  if(candumpReaderFactory.canOpen(sourceDescriptor))
  {
    return candumpReaderFactory.create();
  }

  const can_readers_text::CsvTraceReaderFactory csvTraceReaderFactory;
  if(csvTraceReaderFactory.canOpen(sourceDescriptor))
  {
    return csvTraceReaderFactory.create();
  }

  const can_readers_text::AscTraceReaderFactory ascTraceReaderFactory;
  if(ascTraceReaderFactory.canOpen(sourceDescriptor))
  {
    return ascTraceReaderFactory.create();
  }

  return nullptr;
}

void printMatch(const can_app::QueryResultRow &queryResultRow)
{
  std::cout << "[" << queryResultRow.ordinal << "] "
            << "ts=" << queryResultRow.canEvent.timestampNs
            << " can_id=0x" << std::hex << std::uppercase << queryResultRow.canEvent.canId << std::dec
            << " dlc=" << static_cast<unsigned int>(queryResultRow.canEvent.dlc)
            << " ch=" << static_cast<unsigned int>(queryResultRow.canEvent.channel)
            << " data=";

  for(std::uint8_t index = 0; index < queryResultRow.canEvent.dlc; ++index)
  {
    std::cout << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<unsigned int>(queryResultRow.canEvent.payload[index]);
    if(index + 1U < queryResultRow.canEvent.dlc)
    {
      std::cout << ' ';
    }
  }

  std::cout << std::dec << std::setfill(' ') << '\n';

  if(queryResultRow.decodedMessage.has_value())
  {
    std::cout << "  decoded: " << queryResultRow.decodedMessage->messageName << '\n';
    for(const can_decode::DecodedSignal &decodedSignal : queryResultRow.decodedMessage->signals)
    {
      std::cout << "    " << decodedSignal.name << " = ";
      std::visit(
        [](const auto &value)
        {
          std::cout << value;
        },
        decodedSignal.value);
      if(!decodedSignal.unit.empty())
      {
        std::cout << ' ' << decodedSignal.unit;
      }
      std::cout << '\n';
    }
  }
}
} // namespace

int main(int argc, char **argv)
{
  engine::logger.start({.queue_capacity = 8192, .color = true});
  engine::logger.set_level_mask(0xFF);
  LOG_INFO("Can CLI started");

  if(argc < 2)
  {
    printUsage(argv[0]);
    return 1;
  }

  can_app::RunOptions runOptions;
  runOptions.tracePath = argv[1];
  std::vector<FilterClause> rawAndClauses;
  std::vector<FilterClause> rawOrClauses;
  std::vector<FilterClause> rawNotClauses;
  std::vector<FilterClause> decodedAndClauses;
  std::vector<FilterClause> decodedOrClauses;
  std::vector<FilterClause> decodedNotClauses;
  std::optional<std::uint64_t> contextOrdinal;
  std::size_t contextBefore = 0U;
  std::size_t contextAfter = 0U;

  for(int argumentIndex = 2; argumentIndex < argc; ++argumentIndex)
  {
    const std::string argument = argv[argumentIndex];
    if(argument == "--dbc" && argumentIndex + 1 < argc)
    {
      runOptions.dbcPath = argv[++argumentIndex];
      continue;
    }

    if(argument == "--can-id" && argumentIndex + 1 < argc)
    {
      const auto canId = parseCanId(argv[++argumentIndex]);
      if(!canId.has_value())
      {
        std::cerr << "Invalid CAN ID value\n";
        return 1;
      }

      runOptions.canIdFilter = canId;
      continue;
    }

    const auto parseClauseArgument = [&](std::vector<FilterClause> &target) -> bool
    {
      if(argumentIndex + 3 >= argc)
      {
        return false;
      }

      const auto filterClause = parseClause(argv[argumentIndex + 1], argv[argumentIndex + 2], argv[argumentIndex + 3]);
      if(!filterClause.has_value())
      {
        return false;
      }

      target.push_back(*filterClause);
      argumentIndex += 3;
      return true;
    };

    if(argument == "--where")
    {
      if(!parseClauseArgument(rawAndClauses))
      {
        std::cerr << "Invalid --where clause\n";
        return 1;
      }
      continue;
    }

    if(argument == "--where-or")
    {
      if(!parseClauseArgument(rawOrClauses))
      {
        std::cerr << "Invalid --where-or clause\n";
        return 1;
      }
      continue;
    }

    if(argument == "--where-not")
    {
      if(!parseClauseArgument(rawNotClauses))
      {
        std::cerr << "Invalid --where-not clause\n";
        return 1;
      }
      continue;
    }

    if(argument == "--decoded-where")
    {
      if(!parseClauseArgument(decodedAndClauses))
      {
        std::cerr << "Invalid --decoded-where clause\n";
        return 1;
      }
      continue;
    }

    if(argument == "--decoded-where-or")
    {
      if(!parseClauseArgument(decodedOrClauses))
      {
        std::cerr << "Invalid --decoded-where-or clause\n";
        return 1;
      }
      continue;
    }

    if(argument == "--decoded-where-not")
    {
      if(!parseClauseArgument(decodedNotClauses))
      {
        std::cerr << "Invalid --decoded-where-not clause\n";
        return 1;
      }
      continue;
    }

    if(argument == "--decode")
    {
      runOptions.shouldDecodeMatches = true;
      continue;
    }

    if(argument == "--context-ordinal" && argumentIndex + 1 < argc)
    {
      const auto parsedValue = parseUnsignedInteger(argv[++argumentIndex]);
      if(!parsedValue.has_value())
      {
        std::cerr << "Invalid context ordinal value\n";
        return 1;
      }

      contextOrdinal = *parsedValue;
      continue;
    }

    if(argument == "--context-before" && argumentIndex + 1 < argc)
    {
      const auto parsedValue = parseUnsignedInteger(argv[++argumentIndex]);
      if(!parsedValue.has_value())
      {
        std::cerr << "Invalid context before value\n";
        return 1;
      }

      contextBefore = static_cast<std::size_t>(*parsedValue);
      continue;
    }

    if(argument == "--context-after" && argumentIndex + 1 < argc)
    {
      const auto parsedValue = parseUnsignedInteger(argv[++argumentIndex]);
      if(!parsedValue.has_value())
      {
        std::cerr << "Invalid context after value\n";
        return 1;
      }

      contextAfter = static_cast<std::size_t>(*parsedValue);
      continue;
    }

    if(argument == "--export" && argumentIndex + 1 < argc)
    {
      can_export::ExportRequest exportRequest;
      exportRequest.outputPath = argv[++argumentIndex];
      exportRequest.exportMode = can_export::ExportMode::RawCsv;
      runOptions.exportRequest = exportRequest;
      continue;
    }

    if(argument == "--export-decoded")
    {
      if(!runOptions.exportRequest.has_value())
      {
        std::cerr << "--export-decoded requires --export <path>\n";
        return 1;
      }

      runOptions.exportRequest->exportMode = can_export::ExportMode::DecodedCsv;
      runOptions.shouldDecodeMatches = true;
      continue;
    }

    std::cerr << "Unknown argument: " << argument << '\n';
    printUsage(argv[0]);
    return 1;
  }

  runOptions.rawFilter = buildFilterExpr(rawAndClauses, rawOrClauses, rawNotClauses);
  runOptions.decodedFilter = buildFilterExpr(decodedAndClauses, decodedOrClauses, decodedNotClauses);
  if(runOptions.decodedFilter.has_value())
  {
    runOptions.shouldDecodeMatches = true;
  }

  can_app::CanApp app;
  const can_app::RunSummary runSummary = app.run(runOptions, [](const can_app::QueryResultRow &queryResultRow) {
    printMatch(queryResultRow);
  });

  if(runSummary.hasError())
  {
    std::cerr << "Error: " << runSummary.errorInfo.message << '\n';
    return 1;
  }

  std::cout << "Scanned: " << runSummary.scannedEvents << ", matched: " << runSummary.matchedEvents << '\n';

  if(contextOrdinal.has_value())
  {
    auto traceReader = createReader(runOptions.tracePath);
    if(traceReader == nullptr)
    {
      std::cerr << "Error: No text reader is available for the provided trace path\n";
      return 1;
    }

    can_reader_api::SourceDescriptor sourceDescriptor;
    sourceDescriptor.path = runOptions.tracePath;
    const std::size_t extensionSeparator = runOptions.tracePath.find_last_of('.');
    if(extensionSeparator != std::string::npos)
    {
      sourceDescriptor.extension = runOptions.tracePath.substr(extensionSeparator);
    }

    if(!traceReader->open(sourceDescriptor, {}))
    {
      std::cerr << "Error: Unable to open the trace source for context retrieval\n";
      return 1;
    }

    std::vector<can_core::CanEvent> events;
    std::vector<can_core::CanEvent> outputBuffer(4096U);
    while(true)
    {
      const can_reader_api::ReadResult readResult = traceReader->readChunk(outputBuffer);
      if(readResult.hasError())
      {
        std::cerr << "Error: " << readResult.errorInfo.message << '\n';
        return 1;
      }

      events.insert(events.end(), outputBuffer.begin(), outputBuffer.begin() + static_cast<std::ptrdiff_t>(readResult.eventCount));
      if(readResult.isEndOfStream)
      {
        break;
      }
    }
    traceReader->close();

    const can_core::ContextRequest contextRequest{contextBefore, contextAfter, true};
    const std::vector<can_core::CanEvent> contextEvents =
      can_query::ContextResolver().resolve(events, can_core::MatchReference{*contextOrdinal, 0U}, contextRequest);
    if(contextEvents.empty())
    {
      std::cout << "Context: no events for ordinal " << *contextOrdinal << '\n';
      return 0;
    }

    const std::uint64_t beginOrdinal = *contextOrdinal > contextBefore ? *contextOrdinal - contextBefore : 0U;
    std::cout << "Context:\n";
    for(std::size_t index = 0; index < contextEvents.size(); ++index)
    {
      can_app::QueryResultRow queryResultRow;
      queryResultRow.ordinal = beginOrdinal + index;
      queryResultRow.canEvent = contextEvents[index];
      printMatch(queryResultRow);
    }
  }

  return 0;
}
