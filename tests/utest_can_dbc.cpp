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

TEST_CASE("can_dbc loadFromFile surfaces missing-file errors")
{
  const can_dbc::DbcLoader dbcLoader;
  const can_dbc::LoadResult loadResult = dbcLoader.loadFromFile("/tmp/does_not_exist_canpp.dbc");

  REQUIRE(loadResult.hasError());
  CHECK(loadResult.errorInfo.code == can_core::ErrorCode::IoFailure);
  CHECK(loadResult.database.isEmpty());
}
