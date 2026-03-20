#include <bit>
#include <cstdint>
#include <optional>
#include <string_view>

#include <doctest/doctest.h>

#include "can_core/can_event.hpp"
#include "can_core/error_info.hpp"
#include "can_dbc/database.hpp"
#include "can_decode/decoder.hpp"

namespace
{
can_core::CanEvent makeEvent(std::uint32_t canId, std::initializer_list<std::uint8_t> bytes)
{
  can_core::CanEvent canEvent;
  canEvent.canId = canId;
  canEvent.dlc = static_cast<std::uint8_t>(bytes.size());

  std::size_t index = 0;
  for(const std::uint8_t byteValue : bytes)
  {
    canEvent.payload[index++] = byteValue;
  }

  return canEvent;
}

const can_decode::DecodedSignal *findSignal(const can_decode::DecodedMessage &decodedMessage, std::string_view signalName)
{
  for(const auto &decodedSignal : decodedMessage.signals)
  {
    if(decodedSignal.name == signalName)
    {
      return &decodedSignal;
    }
  }

  return nullptr;
}

can_dbc::Database makeDecodeDatabase()
{
  can_dbc::Database database;

  can_dbc::MessageDefinition scaledMessage;
  scaledMessage.canId = 0x123U;
  scaledMessage.name = "VehicleStatus";
  scaledMessage.dlc = 8U;
  scaledMessage.signalDefinitions.push_back(
    {"VehicleSpeed", false, std::nullopt, 0U, 16U, true, false, 0.5, 1.0, 0.0, 0.0, "km/h", can_dbc::SignalValueType::UnsignedInteger});
  scaledMessage.signalDefinitions.push_back(
    {"Gear", false, std::nullopt, 16U, 8U, true, true, 1.0, 0.0, 0.0, 0.0, "", can_dbc::SignalValueType::SignedInteger});
  database.addMessage(scaledMessage);

  can_dbc::MessageDefinition bigEndianMessage;
  bigEndianMessage.canId = 0x456U;
  bigEndianMessage.name = "BigEndianStatus";
  bigEndianMessage.dlc = 8U;
  bigEndianMessage.signalDefinitions.push_back(
    {"BigCounter", false, std::nullopt, 0U, 12U, false, false, 1.0, 0.0, 0.0, 0.0, "", can_dbc::SignalValueType::UnsignedInteger});
  database.addMessage(bigEndianMessage);

  can_dbc::MessageDefinition floatMessage;
  floatMessage.canId = 0x789U;
  floatMessage.name = "FloatStatus";
  floatMessage.dlc = 8U;
  floatMessage.signalDefinitions.push_back(
    {"FloatValue", false, std::nullopt, 0U, 32U, true, false, 1.0, 0.0, 0.0, 0.0, "", can_dbc::SignalValueType::Float32});
  database.addMessage(floatMessage);

  can_dbc::MessageDefinition multiplexedMessage;
  multiplexedMessage.canId = 967U;
  multiplexedMessage.name = "Motor_26";
  multiplexedMessage.dlc = 8U;
  multiplexedMessage.signalDefinitions.push_back(
    {"MO_Kuehlerluefter_MUX", true, std::nullopt, 0U, 1U, true, false, 1.0, 0.0, 0.0, 1.0, "", can_dbc::SignalValueType::UnsignedInteger});
  multiplexedMessage.signalDefinitions.push_back(
    {"MO_Kuehlerluefter_1", false, std::uint64_t{0U}, 1U, 7U, true, false, 1.0, 0.0, 0.0, 100.0, "Unit_PerCent", can_dbc::SignalValueType::UnsignedInteger});
  multiplexedMessage.signalDefinitions.push_back(
    {"MO_Kuehlerluefter_2", false, std::uint64_t{1U}, 1U, 7U, true, false, 1.0, 0.0, 0.0, 100.0, "Unit_PerCent", can_dbc::SignalValueType::UnsignedInteger});
  database.addMessage(multiplexedMessage);

  return database;
}
} // namespace

TEST_CASE("can_decode requires a configured database before decoding")
{
  const can_decode::Decoder decoder;
  const can_decode::DecodeResult decodeResult = decoder.decode(makeEvent(0x123U, {0x00U}));

  REQUIRE(decodeResult.hasError());
  CHECK(decodeResult.errorInfo.code == can_core::ErrorCode::DecodeFailure);
  CHECK_FALSE(decodeResult.canDecode);
}

TEST_CASE("can_decode reports unsupported CAN IDs")
{
  const can_dbc::Database database = makeDecodeDatabase();
  const can_decode::Decoder decoder(&database);

  CHECK_FALSE(decoder.canDecode(makeEvent(0x321U, {0x00U})));

  const can_decode::DecodeResult decodeResult = decoder.decode(makeEvent(0x321U, {0x00U}));
  REQUIRE(decodeResult.hasError());
  CHECK(decodeResult.errorInfo.code == can_core::ErrorCode::DecodeFailure);
}

TEST_CASE("can_decode applies scaling, signed conversion, and endian-specific extraction")
{
  const can_dbc::Database database = makeDecodeDatabase();
  const can_decode::Decoder decoder(&database);

  SUBCASE("little-endian integer signals use scale and sign extension")
  {
    const can_decode::DecodeResult decodeResult = decoder.decode(makeEvent(0x123U, {0x34U, 0x12U, 0xFEU}));

    REQUIRE_FALSE(decodeResult.hasError());
    REQUIRE(decodeResult.canDecode);
    REQUIRE(decodeResult.decodedMessage.signals.size() == 2U);

    const can_decode::DecodedSignal *vehicleSpeed = findSignal(decodeResult.decodedMessage, "VehicleSpeed");
    const can_decode::DecodedSignal *gear = findSignal(decodeResult.decodedMessage, "Gear");
    REQUIRE(vehicleSpeed != nullptr);
    REQUIRE(gear != nullptr);

    CHECK(std::get<double>(vehicleSpeed->value) == doctest::Approx(2331.0));
    CHECK(std::get<double>(gear->value) == doctest::Approx(-2.0));
  }

  SUBCASE("big-endian signals preserve most-significant-bit ordering")
  {
    const can_decode::DecodeResult decodeResult = decoder.decode(makeEvent(0x456U, {0xABU, 0xC0U}));

    REQUIRE_FALSE(decodeResult.hasError());
    REQUIRE(decodeResult.decodedMessage.signals.size() == 1U);
    CHECK(std::get<double>(decodeResult.decodedMessage.signals.front().value) == doctest::Approx(0xABCU));
  }
}

TEST_CASE("can_decode supports IEEE-754 float32 decoding")
{
  const can_dbc::Database database = makeDecodeDatabase();
  const can_decode::Decoder decoder(&database);

  const std::uint32_t rawFloat = std::bit_cast<std::uint32_t>(1.5F);
  const can_decode::DecodeResult decodeResult = decoder.decode(makeEvent(
    0x789U,
    {
      static_cast<std::uint8_t>(rawFloat & 0xFFU),
      static_cast<std::uint8_t>((rawFloat >> 8U) & 0xFFU),
      static_cast<std::uint8_t>((rawFloat >> 16U) & 0xFFU),
      static_cast<std::uint8_t>((rawFloat >> 24U) & 0xFFU),
    }));

  REQUIRE_FALSE(decodeResult.hasError());
  REQUIRE(decodeResult.decodedMessage.signals.size() == 1U);
  CHECK(std::get<float>(decodeResult.decodedMessage.signals.front().value) == doctest::Approx(1.5F));
}

TEST_CASE("can_decode applies DBC multiplexing and exposes only the active branch")
{
  const can_dbc::Database database = makeDecodeDatabase();
  const can_decode::Decoder decoder(&database);

  SUBCASE("multiplexer value 0 decodes only branch m0")
  {
    const can_decode::DecodeResult decodeResult = decoder.decode(makeEvent(967U, {0x54U}));

    REQUIRE_FALSE(decodeResult.hasError());
    REQUIRE(decodeResult.decodedMessage.signals.size() == 2U);
    const can_decode::DecodedSignal *multiplexer = findSignal(decodeResult.decodedMessage, "MO_Kuehlerluefter_MUX");
    const can_decode::DecodedSignal *branch0 = findSignal(decodeResult.decodedMessage, "MO_Kuehlerluefter_1");
    const can_decode::DecodedSignal *branch1 = findSignal(decodeResult.decodedMessage, "MO_Kuehlerluefter_2");
    REQUIRE(multiplexer != nullptr);
    REQUIRE(branch0 != nullptr);
    CHECK(branch1 == nullptr);
    CHECK(std::get<double>(multiplexer->value) == doctest::Approx(0.0));
    CHECK(std::get<double>(branch0->value) == doctest::Approx(42.0));
  }

  SUBCASE("multiplexer value 1 decodes only branch m1")
  {
    const can_decode::DecodeResult decodeResult = decoder.decode(makeEvent(967U, {0x55U}));

    REQUIRE_FALSE(decodeResult.hasError());
    REQUIRE(decodeResult.decodedMessage.signals.size() == 2U);
    const can_decode::DecodedSignal *multiplexer = findSignal(decodeResult.decodedMessage, "MO_Kuehlerluefter_MUX");
    const can_decode::DecodedSignal *branch0 = findSignal(decodeResult.decodedMessage, "MO_Kuehlerluefter_1");
    const can_decode::DecodedSignal *branch1 = findSignal(decodeResult.decodedMessage, "MO_Kuehlerluefter_2");
    REQUIRE(multiplexer != nullptr);
    REQUIRE(branch1 != nullptr);
    CHECK(branch0 == nullptr);
    CHECK(std::get<double>(multiplexer->value) == doctest::Approx(1.0));
    CHECK(std::get<double>(branch1->value) == doctest::Approx(42.0));
  }
}
