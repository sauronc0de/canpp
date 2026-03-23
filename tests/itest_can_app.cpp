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

TEST_CASE("can_app accepts a real-world-style DBC with header keywords scientific notation and multiplexed signals")
{
  const test_support::ScopedTempFile traceFile(
    "app_real_like_trace",
    ".asc",
    "date Mon Mar 2 10:39:49.909 pm 2026\n"
    "base hex  timestamps absolute\n"
    "internal events logged\n"
    "34.846777 10 12DD54D6x Rx d 8 A0 25 80 01 00 00 00 00 Length = 0 BitCount = 0 ID = 316495062x\n");
  const test_support::ScopedTempFile dbcFile(
    "app_real_like",
    ".dbc",
    "VERSION \"copied-real-world-use-cases\"\n"
    "NS_ :\n"
    "  NS_DESC_\n"
    "  SIG_VALTYPE_\n"
    "  BO_TX_BU_\n"
    "\n"
    "BO_ 2621455131 ARC_HUD_Req_FD: 64 ICC_IO_Node\n"
    " SG_ ARC_HUD_Req_FD_Data : 0|512@1+ (1,0) [0|1.34078079299426E+154] \"\" HUD\n"
    "\n"
    "BO_ 967 Motor_26: 8 ICAS1_X_Gateway\n"
    " SG_ MO_Kuehlerluefter_MUX M : 0|1@1+ (1,0) [0|1] \"\" Vector__XXX\n"
    " SG_ MO_Kuehlerluefter_1 m0 : 1|7@1+ (1,0) [0|100] \"Unit_PerCent\" Vector__XXX\n"
    " SG_ MO_Kuehlerluefter_2 m1 : 1|7@1+ (1,0) [0|100] \"Unit_PerCent\" Vector__XXX\n");

  can_app::RunOptions runOptions;
  runOptions.tracePath = traceFile.string();
  runOptions.dbcPath = dbcFile.string();
  runOptions.canIdFilter = 0x12DD54D6U;

  std::vector<can_app::QueryResultRow> rows;
  const can_app::RunSummary runSummary = can_app::CanApp().run(
    runOptions,
    [&rows](const can_app::QueryResultRow &queryResultRow)
    {
      rows.push_back(queryResultRow);
    });

  REQUIRE_FALSE(runSummary.hasError());
  CHECK(runSummary.scannedEvents == 1U);
  CHECK(runSummary.matchedEvents == 1U);
  REQUIRE(rows.size() == 1U);
  CHECK(rows.front().canEvent.canId == 0x12DD54D6U);
  CHECK_FALSE(rows.front().decodedMessage.has_value());
}

TEST_CASE("can_app executes combined raw and decoded filter expressions")
{
  const test_support::ScopedTempFile traceFile(
    "app_query_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,123,2,2A 00,CAN\n"
    "0.002,1,123,2,05 00,FD\n"
    "0.003,0,456,1,10,CAN\n"
    "0.004,1,123,2,00 00,CAN\n");
  const test_support::ScopedTempFile dbcFile(
    "app_query_decode",
    ".dbc",
    "BO_ 291 VehicleStatus: 8 Vector__XXX\n"
    " SG_ VehicleSpeed : 0|16@1+ (1,0) [0|65535] \"km/h\" Vector__XXX\n");

  can_core::FilterExpr timestampPredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::TimestampNs, can_core::FilterOperator::Greater, std::uint64_t{1500000U}});
  can_core::FilterExpr channelPredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::Channel, can_core::FilterOperator::Equal, std::uint64_t{1U}});
  can_core::FilterExpr frameTypePredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::FrameType, can_core::FilterOperator::Equal, std::uint64_t{static_cast<std::uint8_t>(can_core::FrameType::CanFd)}});

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
  can_core::FilterExpr decodedZeroPredicate = can_core::FilterExpr::makePredicate(
    {can_core::FilterField::SignalValue, can_core::FilterOperator::Equal, 0.0});
  can_core::FilterExpr decodedNotFilter;
  decodedNotFilter.logicalOperator = can_core::LogicalOperator::Not;
  decodedNotFilter.children = {decodedZeroPredicate};

  can_core::FilterExpr decodedFilter;
  decodedFilter.logicalOperator = can_core::LogicalOperator::And;
  decodedFilter.children = {decodedNamePredicate, decodedSignalPredicate, decodedNotFilter};

  can_app::RunOptions runOptions;
  runOptions.tracePath = traceFile.string();
  runOptions.dbcPath = dbcFile.string();
  runOptions.rawFilter = rawFilter;
  runOptions.decodedFilter = decodedFilter;
  runOptions.shouldDecodeMatches = true;

  std::vector<can_app::QueryResultRow> rows;
  const can_app::RunSummary runSummary = can_app::CanApp().run(
    runOptions,
    [&rows](const can_app::QueryResultRow &queryResultRow)
    {
      rows.push_back(queryResultRow);
    });

  REQUIRE_FALSE(runSummary.hasError());
  CHECK(runSummary.scannedEvents == 4U);
  CHECK(runSummary.matchedEvents == 1U);
  REQUIRE(rows.size() == 1U);
  REQUIRE(rows.front().decodedMessage.has_value());
  CHECK(rows.front().decodedMessage->messageName == "VehicleStatus");
  CHECK(rows.front().ordinal == 1U);
}

TEST_CASE("can_app keeps decoded message and signal names valid after the DBC loader goes out of scope")
{
  const test_support::ScopedTempFile traceFile(
    "app_lifetime_trace",
    ".csv",
    "timestamp,channel,can_id,dlc,payload,frame_type\n"
    "0.001,0,1440,2,2A 00,CAN\n");
  const test_support::ScopedTempFile dbcFile(
    "app_lifetime_decode",
    ".dbc",
    "BO_ 1440 RLS_01: 8 Vector__XXX\n"
    " SG_ LS_Helligkeit_IR : 0|16@1+ (1,0) [0|65535] \"\" Vector__XXX\n");

  can_app::RunOptions runOptions;
  runOptions.tracePath = traceFile.string();
  runOptions.dbcPath = dbcFile.string();
  runOptions.shouldDecodeMatches = true;

  std::vector<can_app::QueryResultRow> rows;
  const can_app::RunSummary runSummary = can_app::CanApp().run(
    runOptions,
    [&rows](const can_app::QueryResultRow &queryResultRow)
    {
      rows.push_back(queryResultRow);
    });

  REQUIRE_FALSE(runSummary.hasError());
  REQUIRE(rows.size() == 1U);
  REQUIRE(rows.front().decodedMessage.has_value());
  CHECK(rows.front().decodedMessage->messageName == "RLS_01");
  REQUIRE(rows.front().decodedMessage->signals.size() == 1U);
  CHECK(rows.front().decodedMessage->signals.front().name == "LS_Helligkeit_IR");
}
