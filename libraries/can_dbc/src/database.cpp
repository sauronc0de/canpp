#include "can_dbc/database.hpp"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <regex>
#include <sstream>
#include <utility>

namespace can_dbc
{
namespace
{
constexpr int kMessageCanIdIndex = 1;
constexpr int kMessageNameIndex = 2;
constexpr int kMessageDlcIndex = 3;
constexpr int kSignalNameIndex = 1;
constexpr int kSignalStartBitIndex = 2;
constexpr int kSignalBitLengthIndex = 3;
constexpr int kSignalEndianIndex = 4;
constexpr int kSignalSignedIndex = 5;
constexpr int kSignalScaleIndex = 6;
constexpr int kSignalOffsetIndex = 7;
constexpr int kSignalMinimumIndex = 8;
constexpr int kSignalMaximumIndex = 9;
constexpr int kSignalUnitIndex = 10;

const std::regex kMessageRegex(R"(^BO_\s+(\d+)\s+([A-Za-z0-9_]+)\s*:\s*(\d+)\s+.*$)");
const std::regex kSignalRegex(
  R"dbc(^SG_\s+([A-Za-z0-9_]+)\s*:\s*(\d+)\|(\d+)@([01])([+-])\s+\(([+-]?[0-9]*\.?[0-9]+),([+-]?[0-9]*\.?[0-9]+)\)\s+\[([+-]?[0-9]*\.?[0-9]+)\|([+-]?[0-9]*\.?[0-9]+)\]\s+"([^"]*)".*$)dbc");

std::optional<MessageDefinition> parseMessageDefinition(const std::string &line)
{
  std::smatch match;
  if(!std::regex_match(line, match, kMessageRegex))
  {
    return std::nullopt;
  }

  MessageDefinition messageDefinition;
  messageDefinition.canId = static_cast<std::uint32_t>(std::stoul(match[kMessageCanIdIndex].str()));
  messageDefinition.name = match[kMessageNameIndex].str();
  messageDefinition.dlc = static_cast<std::uint8_t>(std::stoul(match[kMessageDlcIndex].str()));
  return messageDefinition;
}

std::optional<SignalDefinition> parseSignalDefinition(const std::string &line)
{
  std::smatch match;
  if(!std::regex_match(line, match, kSignalRegex))
  {
    return std::nullopt;
  }

  SignalDefinition signalDefinition;
  signalDefinition.name = match[kSignalNameIndex].str();
  signalDefinition.startBit = static_cast<std::uint16_t>(std::stoul(match[kSignalStartBitIndex].str()));
  signalDefinition.bitLength = static_cast<std::uint16_t>(std::stoul(match[kSignalBitLengthIndex].str()));
  signalDefinition.isLittleEndian = match[kSignalEndianIndex].str() == "1";
  signalDefinition.isSigned = match[kSignalSignedIndex].str() == "-";
  signalDefinition.scale = std::stod(match[kSignalScaleIndex].str());
  signalDefinition.offset = std::stod(match[kSignalOffsetIndex].str());
  signalDefinition.minimum = std::stod(match[kSignalMinimumIndex].str());
  signalDefinition.maximum = std::stod(match[kSignalMaximumIndex].str());
  signalDefinition.unit = match[kSignalUnitIndex].str();
  signalDefinition.valueType = signalDefinition.isSigned ? SignalValueType::SignedInteger : SignalValueType::UnsignedInteger;
  return signalDefinition;
}

std::string trim(const std::string &value)
{
  const std::size_t firstNonWhitespace = value.find_first_not_of(" \t\r\n");
  if(firstNonWhitespace == std::string::npos)
  {
    return {};
  }

  const std::size_t lastNonWhitespace = value.find_last_not_of(" \t\r\n");
  return value.substr(firstNonWhitespace, lastNonWhitespace - firstNonWhitespace + 1);
}
} // namespace

void Database::addMessage(MessageDefinition messageDefinition)
{
  const std::size_t index = messageDefinitions_.size();
  canIdToIndex_[messageDefinition.canId] = index;
  nameToIndex_[messageDefinition.name] = index;
  messageDefinitions_.push_back(std::move(messageDefinition));
}

const MessageDefinition *Database::findMessageByCanId(std::uint32_t canId) const
{
  const auto iterator = canIdToIndex_.find(canId);
  if(iterator == canIdToIndex_.end())
  {
    return nullptr;
  }

  return &messageDefinitions_[iterator->second];
}

const MessageDefinition *Database::findMessageByName(std::string_view name) const
{
  const auto iterator = nameToIndex_.find(std::string(name));
  if(iterator == nameToIndex_.end())
  {
    return nullptr;
  }

  return &messageDefinitions_[iterator->second];
}

const std::vector<MessageDefinition> &Database::messageDefinitions() const
{
  return messageDefinitions_;
}

bool Database::isEmpty() const
{
  return messageDefinitions_.empty();
}

LoadResult DbcLoader::loadFromFile(const std::string &path) const
{
  std::ifstream inputFile(path);
  if(!inputFile.is_open())
  {
    LoadResult loadResult;
    loadResult.errorInfo.code = can_core::ErrorCode::IoFailure;
    loadResult.errorInfo.message = "Unable to open DBC file: " + path;
    return loadResult;
  }

  std::ostringstream buffer;
  buffer << inputFile.rdbuf();
  return loadFromText(buffer.str());
}

LoadResult DbcLoader::loadFromText(std::string_view dbcText) const
{
  LoadResult loadResult;
  std::istringstream input{std::string(dbcText)};
  std::string line;
  MessageDefinition *currentMessageDefinition = nullptr;
  std::size_t lineNumber = 0;

  while(std::getline(input, line))
  {
    ++lineNumber;
    const std::string trimmedLine = trim(line);
    if(trimmedLine.empty())
    {
      continue;
    }

    if(trimmedLine.rfind("BO_", 0) == 0)
    {
      const auto messageDefinition = parseMessageDefinition(trimmedLine);
      if(!messageDefinition.has_value())
      {
        loadResult.errorInfo.code = can_core::ErrorCode::ParseFailure;
        loadResult.errorInfo.message = "Invalid DBC message definition";
        loadResult.errorInfo.line = lineNumber;
        return loadResult;
      }

      loadResult.database.addMessage(*messageDefinition);
      currentMessageDefinition = const_cast<MessageDefinition *>(
        loadResult.database.findMessageByCanId(messageDefinition->canId));
      continue;
    }

    if(trimmedLine.rfind("SG_", 0) == 0)
    {
      if(currentMessageDefinition == nullptr)
      {
        loadResult.errorInfo.code = can_core::ErrorCode::ParseFailure;
        loadResult.errorInfo.message = "Signal definition found before any message definition";
        loadResult.errorInfo.line = lineNumber;
        return loadResult;
      }

      const auto signalDefinition = parseSignalDefinition(trimmedLine);
      if(!signalDefinition.has_value())
      {
        loadResult.errorInfo.code = can_core::ErrorCode::ParseFailure;
        loadResult.errorInfo.message = "Invalid DBC signal definition";
        loadResult.errorInfo.line = lineNumber;
        return loadResult;
      }

      currentMessageDefinition->signalDefinitions.push_back(*signalDefinition);
    }
  }

  return loadResult;
}
} // namespace can_dbc
