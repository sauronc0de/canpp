#include <fstream>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "can_app/can_app.hpp"
#include "can_export/exporter.hpp"
#include "test_support.hpp"

namespace
{
std::string readFile(const test_support::ScopedTempFile &file)
{
  std::ifstream input(file.path(), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}
} // namespace

TEST_CASE("can_app executes the architecture primary flow with reader decode query and export")
{
  const test_support::ScopedTempFile traceFile(
    "app_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,123,2,2A 00,CAN\n"
    "0.002,0,456,1,10,CAN\n"
    "0.003,1,123,2,05 00,CAN\n");
  const test_support::ScopedTempFile dbcFile(
    "app_decode",
    ".dbc",
    "BO_ 291 VehicleStatus: 8 Vector__XXX\n"
    " SG_ VehicleSpeed : 0|16@1+ (1,0) [0|65535] \"km/h\" Vector__XXX\n");
  const test_support::ScopedTempFile exportFile("decoded_export", ".csv");

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
  CHECK(runSummary.scannedEvents == 3U);
  CHECK(runSummary.matchedEvents == 2U);
  REQUIRE(rows.size() == 2U);
  REQUIRE(rows[0].decodedMessage.has_value());
  REQUIRE(rows[1].decodedMessage.has_value());
  CHECK(rows[0].decodedMessage->messageName == "VehicleStatus");
  CHECK(rows[0].ordinal == 0U);
  CHECK(rows[1].ordinal == 2U);

  const std::string exportText = readFile(exportFile);
  CHECK(exportText.find("timestamp_ns,can_id,message_name,signal_name,signal_value,unit") != std::string::npos);
  CHECK(exportText.find("VehicleStatus,VehicleSpeed,42,km/h") != std::string::npos);
  CHECK(exportText.find("VehicleStatus,VehicleSpeed,5,km/h") != std::string::npos);
}

TEST_CASE("can_app executes raw-only flow and exports filtered events")
{
  const test_support::ScopedTempFile traceFile(
    "app_raw_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,123,2,11 22,CAN\n"
    "0.002,0,456,1,10,CAN\n"
    "0.003,1,123,1,FF,FD\n");
  const test_support::ScopedTempFile exportFile("raw_export", ".csv");

  can_app::RunOptions runOptions;
  runOptions.tracePath = traceFile.string();
  runOptions.canIdFilter = 0x123U;
  runOptions.exportRequest = can_export::ExportRequest{exportFile.string(), can_export::ExportMode::RawCsv, true};

  std::vector<can_app::QueryResultRow> rows;
  const can_app::RunSummary runSummary = can_app::CanApp().run(
    runOptions,
    [&rows](const can_app::QueryResultRow &queryResultRow)
    {
      rows.push_back(queryResultRow);
    });

  REQUIRE_FALSE(runSummary.hasError());
  CHECK(runSummary.scannedEvents == 3U);
  CHECK(runSummary.matchedEvents == 2U);
  REQUIRE(rows.size() == 2U);
  CHECK_FALSE(rows[0].decodedMessage.has_value());
  CHECK(rows[1].canEvent.frameType == can_core::FrameType::CanFd);

  const std::string exportText = readFile(exportFile);
  CHECK(exportText.find("timestamp_ns,channel,can_id,dlc,payload,frame_type") != std::string::npos);
  CHECK(exportText.find("1000000,0,123,2,11 22,CAN") != std::string::npos);
  CHECK(exportText.find("3000000,1,123,1,FF,FD") != std::string::npos);
}

TEST_CASE("can_app reports unsupported formats at the application boundary")
{
  can_app::RunOptions runOptions;
  runOptions.tracePath = "/tmp/trace.unsupported";

  std::vector<can_app::QueryResultRow> rows;
  const can_app::RunSummary runSummary = can_app::CanApp().run(
    runOptions,
    [&rows](const can_app::QueryResultRow &queryResultRow)
    {
      rows.push_back(queryResultRow);
    });

  REQUIRE(runSummary.hasError());
  CHECK(runSummary.errorInfo.code == can_core::ErrorCode::UnsupportedFormat);
  CHECK(rows.empty());
}
