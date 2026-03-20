#include <string>

#include <doctest/doctest.h>

#include "can_core/error_info.hpp"
#include "can_dbc/database.hpp"

TEST_CASE("can_dbc loads messages, signals, and lookup indices from DBC text")
{
  static constexpr std::string_view kDbcText = R"dbc(
BO_ 291 VehicleStatus: 8 Vector__XXX
 SG_ VehicleSpeed : 0|16@1+ (0.1,1) [0|250] "km/h" Vector__XXX
 SG_ Gear : 16|8@1- (1,-2) [-2|5] "" Vector__XXX
)dbc";

  const can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromText(kDbcText);

  REQUIRE_FALSE(loadResult.hasError());
  REQUIRE(loadResult.database.messageDefinitions().size() == 1U);

  const can_dbc::MessageDefinition *messageByCanId = loadResult.database.findMessageByCanId(291U);
  const can_dbc::MessageDefinition *messageByName = loadResult.database.findMessageByName("VehicleStatus");
  REQUIRE(messageByCanId != nullptr);
  REQUIRE(messageByName != nullptr);
  CHECK(messageByCanId == messageByName);
  CHECK(messageByCanId->dlc == 8U);
  REQUIRE(messageByCanId->signalDefinitions.size() == 2U);

  const can_dbc::SignalDefinition &vehicleSpeed = messageByCanId->signalDefinitions[0];
  CHECK(vehicleSpeed.name == "VehicleSpeed");
  CHECK(vehicleSpeed.startBit == 0U);
  CHECK(vehicleSpeed.bitLength == 16U);
  CHECK(vehicleSpeed.isLittleEndian);
  CHECK_FALSE(vehicleSpeed.isSigned);
  CHECK(vehicleSpeed.scale == doctest::Approx(0.1));
  CHECK(vehicleSpeed.offset == doctest::Approx(1.0));
  CHECK(vehicleSpeed.unit == "km/h");

  const can_dbc::SignalDefinition &gear = messageByCanId->signalDefinitions[1];
  CHECK(gear.isSigned);
  CHECK(gear.valueType == can_dbc::SignalValueType::SignedInteger);
}

TEST_CASE("can_dbc reports parse failures with line information")
{
  static constexpr std::string_view kInvalidDbcText =
    R"dbc(SG_ VehicleSpeed : 0|16@1+ (1,0) [0|100] "km/h" Vector__XXX)dbc";

  const can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromText(kInvalidDbcText);

  REQUIRE(loadResult.hasError());
  CHECK(loadResult.errorInfo.code == can_core::ErrorCode::ParseFailure);
  CHECK(loadResult.errorInfo.line == 1U);
  CHECK(loadResult.errorInfo.message == "Signal definition found before any message definition");
}

TEST_CASE("can_dbc ignores NS header keywords that share BO_/SG_ prefixes")
{
  static constexpr std::string_view kDbcText = R"dbc(
NS_ :
  NS_DESC_
  SG_VALTYPE_
  BO_TX_BU_

BO_ 291 VehicleStatus: 8 Vector__XXX
 SG_ VehicleSpeed : 0|16@1+ (1,0) [0|100] "km/h" Vector__XXX
)dbc";

  const can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromText(kDbcText);

  REQUIRE_FALSE(loadResult.hasError());
  REQUIRE(loadResult.database.messageDefinitions().size() == 1U);
  const can_dbc::MessageDefinition *messageByCanId = loadResult.database.findMessageByCanId(291U);
  REQUIRE(messageByCanId != nullptr);
  CHECK(messageByCanId->name == "VehicleStatus");
  REQUIRE(messageByCanId->signalDefinitions.size() == 1U);
  CHECK(messageByCanId->signalDefinitions.front().name == "VehicleSpeed");
}

TEST_CASE("can_dbc accepts scientific-notation numeric fields used by real DBC exports")
{
  static constexpr std::string_view kDbcText = R"dbc(
BO_ 2621455131 ARC_HUD_Req_FD: 64 ICC_IO_Node
 SG_ ARC_HUD_Req_FD_Data : 0|512@1+ (1,0) [0|1.34078079299426E+154] "" HUD
)dbc";

  const can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromText(kDbcText);

  REQUIRE_FALSE(loadResult.hasError());
  REQUIRE(loadResult.database.messageDefinitions().size() == 1U);
  const can_dbc::MessageDefinition *messageByCanId = loadResult.database.findMessageByCanId(2621455131U);
  REQUIRE(messageByCanId != nullptr);
  REQUIRE(messageByCanId->signalDefinitions.size() == 1U);
  CHECK(messageByCanId->signalDefinitions.front().bitLength == 512U);
  CHECK(messageByCanId->signalDefinitions.front().maximum == doctest::Approx(1.34078079299426E+154));
}

TEST_CASE("can_dbc parses standard multiplexed signal syntax used by real DBC exports")
{
  static constexpr std::string_view kDbcText = R"dbc(
BO_ 967 Motor_26: 8 ICAS1_X_Gateway
 SG_ MO_Kuehlerluefter_MUX M : 0|1@1+ (1,0) [0|1] "" Vector__XXX
 SG_ MO_Kuehlerluefter_1 m0 : 1|7@1+ (1,0) [0|100] "Unit_PerCent" Vector__XXX
 SG_ MO_Kuehlerluefter_2 m1 : 1|7@1+ (1,0) [0|100] "Unit_PerCent" Vector__XXX
)dbc";

  const can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromText(kDbcText);

  REQUIRE_FALSE(loadResult.hasError());
  const can_dbc::MessageDefinition *messageByCanId = loadResult.database.findMessageByCanId(967U);
  REQUIRE(messageByCanId != nullptr);
  REQUIRE(messageByCanId->signalDefinitions.size() == 3U);
  CHECK(messageByCanId->signalDefinitions[0].isMultiplexer);
  CHECK_FALSE(messageByCanId->signalDefinitions[0].multiplexValue.has_value());
  REQUIRE(messageByCanId->signalDefinitions[1].multiplexValue.has_value());
  CHECK(*messageByCanId->signalDefinitions[1].multiplexValue == 0U);
  REQUIRE(messageByCanId->signalDefinitions[2].multiplexValue.has_value());
  CHECK(*messageByCanId->signalDefinitions[2].multiplexValue == 1U);
}

TEST_CASE("can_dbc loadFromFile surfaces missing-file errors")
{
  const can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromFile("/tmp/does_not_exist_canpp.dbc");

  REQUIRE(loadResult.hasError());
  CHECK(loadResult.errorInfo.code == can_core::ErrorCode::IoFailure);
  CHECK(loadResult.database.isEmpty());
}
