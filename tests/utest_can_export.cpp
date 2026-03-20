#include <fstream>
#include <string>

#include <doctest/doctest.h>

#include "can_export/exporter.hpp"
#include "test_support.hpp"

namespace
{
std::string readFile(const test_support::ScopedTempFile &file)
{
  std::ifstream input(file.path(), std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

can_core::CanEvent makeEvent(std::uint64_t timestampNs, std::uint32_t canId, std::uint8_t channel, std::initializer_list<std::uint8_t> bytes)
{
  can_core::CanEvent canEvent;
  canEvent.timestampNs = timestampNs;
  canEvent.canId = canId;
  canEvent.channel = channel;
  canEvent.dlc = static_cast<std::uint8_t>(bytes.size());

  std::size_t index = 0;
  for(const std::uint8_t byteValue : bytes)
  {
    canEvent.payload[index++] = byteValue;
  }

  return canEvent;
}
} // namespace

TEST_CASE("can_export streams raw CSV rows with stable schema and payload formatting")
{
  const test_support::ScopedTempFile exportFile("export_raw", ".csv");

  can_export::Exporter exporter;
  REQUIRE(exporter.open({exportFile.string(), can_export::ExportMode::RawCsv, true}));

  auto firstEvent = makeEvent(100U, 0x123U, 1U, {0x0AU, 0xBCU, 0x00U});
  auto secondEvent = makeEvent(200U, 0x7FFU, 2U, {0xFFU});
  secondEvent.frameType = can_core::FrameType::CanFd;

  REQUIRE(exporter.writeRaw(firstEvent));
  REQUIRE(exporter.writeRaw(secondEvent));
  const auto summary = exporter.close();

  REQUIRE_FALSE(summary.hasError());
  CHECK(summary.writtenRows == 2U);

  const std::string text = readFile(exportFile);
  CHECK(text ==
    "timestamp_ns,channel,can_id,dlc,payload,frame_type\n"
    "100,1,123,3,0A BC 00,CAN\n"
    "200,2,7FF,1,FF,FD\n");
}

TEST_CASE("can_export writes one decoded CSV row per signal using streamed variant values")
{
  const test_support::ScopedTempFile exportFile("export_decoded", ".csv");

  can_export::Exporter exporter;
  REQUIRE(exporter.open({exportFile.string(), can_export::ExportMode::DecodedCsv, true}));

  const std::string messageName = "VehicleStatus";
  const std::string speedName = "VehicleSpeed";
  const std::string gearName = "Gear";
  const std::string speedUnit = "km/h";

  can_decode::DecodedMessage decodedMessage;
  decodedMessage.messageName = messageName;
  decodedMessage.canId = 0x456U;
  decodedMessage.signals.push_back({speedName, 42.5, speedUnit});
  decodedMessage.signals.push_back({gearName, std::int64_t{-1}, ""});

  const auto canEvent = makeEvent(123456U, 0x456U, 0U, {0x2AU, 0x00U});

  REQUIRE(exporter.writeDecoded(decodedMessage, canEvent));
  const auto summary = exporter.close();

  REQUIRE_FALSE(summary.hasError());
  CHECK(summary.writtenRows == 2U);

  const std::string text = readFile(exportFile);
  CHECK(text ==
    "timestamp_ns,can_id,message_name,signal_name,signal_value,unit\n"
    "123456,456,VehicleStatus,VehicleSpeed,42.5,km/h\n"
    "123456,456,VehicleStatus,Gear,-1,\n");
}
