#include "can_decode/decoder.hpp"

#include <bit>
#include <cstring>
#include <limits>
#include <optional>

namespace can_decode
{
namespace
{
std::uint64_t extractLittleEndianRawValue(const can_core::CanEvent &canEvent, const can_dbc::SignalDefinition &signalDefinition)
{
  std::uint64_t rawValue = 0;
  const std::size_t availableBits = static_cast<std::size_t>(canEvent.dlc) * 8;
  const std::size_t bitLength = signalDefinition.bitLength;

  for(std::size_t bitIndex = 0; bitIndex < bitLength; ++bitIndex)
  {
    const std::size_t sourceBit = static_cast<std::size_t>(signalDefinition.startBit) + bitIndex;
    if(sourceBit >= availableBits)
    {
      break;
    }

    const std::size_t byteIndex = sourceBit / 8;
    const std::size_t bitInByte = sourceBit % 8;
    const std::uint8_t bitValue = static_cast<std::uint8_t>((canEvent.payload[byteIndex] >> bitInByte) & 0x1U);
    rawValue |= (static_cast<std::uint64_t>(bitValue) << bitIndex);
  }

  return rawValue;
}

std::uint64_t extractBigEndianRawValue(const can_core::CanEvent &canEvent, const can_dbc::SignalDefinition &signalDefinition)
{
  std::uint64_t rawValue = 0;
  const std::size_t availableBits = static_cast<std::size_t>(canEvent.dlc) * 8;
  const std::size_t bitLength = signalDefinition.bitLength;

  for(std::size_t bitIndex = 0; bitIndex < bitLength; ++bitIndex)
  {
    const std::size_t sourceBit = static_cast<std::size_t>(signalDefinition.startBit) + bitIndex;
    if(sourceBit >= availableBits)
    {
      break;
    }

    const std::size_t byteIndex = sourceBit / 8;
    const std::size_t bitInByte = 7U - (sourceBit % 8U);
    const std::uint8_t bitValue = static_cast<std::uint8_t>((canEvent.payload[byteIndex] >> bitInByte) & 0x1U);
    rawValue = (rawValue << 1U) | static_cast<std::uint64_t>(bitValue);
  }

  return rawValue;
}

std::uint64_t extractRawValue(const can_core::CanEvent &canEvent, const can_dbc::SignalDefinition &signalDefinition)
{
  if(signalDefinition.isLittleEndian)
  {
    return extractLittleEndianRawValue(canEvent, signalDefinition);
  }

  return extractBigEndianRawValue(canEvent, signalDefinition);
}

std::int64_t signExtend(std::uint64_t rawValue, std::uint16_t bitLength)
{
  if(bitLength == 0 || bitLength >= 64)
  {
    return static_cast<std::int64_t>(rawValue);
  }

  const std::uint64_t signBit = 1ULL << (bitLength - 1U);
  const std::uint64_t valueMask = (1ULL << bitLength) - 1ULL;
  rawValue &= valueMask;
  if((rawValue & signBit) == 0)
  {
    return static_cast<std::int64_t>(rawValue);
  }

  const std::uint64_t extensionMask = ~valueMask;
  return static_cast<std::int64_t>(rawValue | extensionMask);
}

DecodedSignalValue convertValue(std::uint64_t rawValue, const can_dbc::SignalDefinition &signalDefinition)
{
  if(signalDefinition.valueType == can_dbc::SignalValueType::Float32 && signalDefinition.bitLength == 32)
  {
    const std::uint32_t rawFloat = static_cast<std::uint32_t>(rawValue);
    return std::bit_cast<float>(rawFloat);
  }

  if(signalDefinition.valueType == can_dbc::SignalValueType::Float64 && signalDefinition.bitLength == 64)
  {
    return std::bit_cast<double>(rawValue);
  }

  if(signalDefinition.isSigned)
  {
    const double scaledValue = static_cast<double>(signExtend(rawValue, signalDefinition.bitLength)) * signalDefinition.scale +
      signalDefinition.offset;
    return scaledValue;
  }

  const double scaledValue = static_cast<double>(rawValue) * signalDefinition.scale + signalDefinition.offset;
  return scaledValue;
}

std::optional<std::uint64_t> resolveMultiplexerValue(
  const can_core::CanEvent &canEvent,
  const can_dbc::MessageDefinition &messageDefinition)
{
  for(const can_dbc::SignalDefinition &signalDefinition : messageDefinition.signalDefinitions)
  {
    if(signalDefinition.isMultiplexer)
    {
      return extractRawValue(canEvent, signalDefinition);
    }
  }

  return std::nullopt;
}
} // namespace

Decoder::Decoder(const can_dbc::Database *database)
  : database_(database)
{
}

void Decoder::setDatabase(const can_dbc::Database *database)
{
  database_ = database;
}

bool Decoder::canDecode(const can_core::CanEvent &canEvent) const
{
  return database_ != nullptr && database_->findMessageByCanId(canEvent.canId) != nullptr;
}

DecodeResult Decoder::decode(const can_core::CanEvent &canEvent) const
{
  DecodeResult decodeResult;
  if(database_ == nullptr)
  {
    decodeResult.errorInfo.code = can_core::ErrorCode::DecodeFailure;
    decodeResult.errorInfo.message = "No DBC database is loaded";
    return decodeResult;
  }

  const can_dbc::MessageDefinition *messageDefinition = database_->findMessageByCanId(canEvent.canId);
  if(messageDefinition == nullptr)
  {
    decodeResult.errorInfo.code = can_core::ErrorCode::DecodeFailure;
    decodeResult.errorInfo.message = "No DBC message definition matches the CAN ID";
    return decodeResult;
  }

  decodeResult.canDecode = true;
  decodeResult.decodedMessage.messageName = messageDefinition->name;
  decodeResult.decodedMessage.canId = messageDefinition->canId;
  decodeResult.decodedMessage.signals.reserve(messageDefinition->signalDefinitions.size());
  const std::optional<std::uint64_t> multiplexerValue = resolveMultiplexerValue(canEvent, *messageDefinition);

  for(const can_dbc::SignalDefinition &signalDefinition : messageDefinition->signalDefinitions)
  {
    if(signalDefinition.multiplexValue.has_value())
    {
      if(!multiplexerValue.has_value() || *multiplexerValue != *signalDefinition.multiplexValue)
      {
        continue;
      }
    }

    DecodedSignal decodedSignal;
    decodedSignal.name = signalDefinition.name;
    decodedSignal.unit = signalDefinition.unit;
    decodedSignal.value = convertValue(extractRawValue(canEvent, signalDefinition), signalDefinition);
    decodeResult.decodedMessage.signals.push_back(decodedSignal);
  }

  return decodeResult;
}
} // namespace can_decode
