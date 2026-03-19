#include "log.hpp"
#include "can_app/can_app.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <variant>

namespace
{
void printUsage(const char *programName)
{
  std::cout << "Usage: " << programName
            << " <trace-path> [--dbc <dbc-path>] [--can-id <id>] [--decode]"
            << " [--export <csv-path>] [--export-decoded]\n";
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

    if(argument == "--decode")
    {
      runOptions.shouldDecodeMatches = true;
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
  return 0;
}
