#include "can_readers_text/text_trace_reader.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <regex>
#include <sstream>
#include <string_view>
#include <vector>

namespace can_readers_text
{
namespace
{
constexpr double kNanosecondsPerSecond = 1000000000.0;

std::string toLower(std::string_view value)
{
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

std::string trim(std::string_view value)
{
  const std::size_t firstNonWhitespace = value.find_first_not_of(" \t\r\n");
  if(firstNonWhitespace == std::string_view::npos)
  {
    return {};
  }

  const std::size_t lastNonWhitespace = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(firstNonWhitespace, lastNonWhitespace - firstNonWhitespace + 1U));
}

std::vector<std::string> split(std::string_view line, char separator)
{
  std::vector<std::string> tokens;
  std::size_t start = 0;
  while(start <= line.size())
  {
    const std::size_t end = line.find(separator, start);
    if(end == std::string_view::npos)
    {
      tokens.push_back(trim(line.substr(start)));
      break;
    }

    tokens.push_back(trim(line.substr(start, end - start)));
    start = end + 1U;
  }

  return tokens;
}

bool parseUnsigned64(std::string_view text, std::uint64_t &value)
{
  const std::string trimmedText = trim(text);
  if(trimmedText.empty())
  {
    return false;
  }

  const auto result = std::from_chars(trimmedText.data(), trimmedText.data() + trimmedText.size(), value, 10);
  return result.ec == std::errc{} && result.ptr == trimmedText.data() + trimmedText.size();
}

bool parseUnsigned32Hex(std::string_view text, std::uint32_t &value)
{
  const std::string trimmedText = trim(text);
  if(trimmedText.empty())
  {
    return false;
  }

  const auto result = std::from_chars(trimmedText.data(), trimmedText.data() + trimmedText.size(), value, 16);
  return result.ec == std::errc{} && result.ptr == trimmedText.data() + trimmedText.size();
}

bool parseUnsigned8Hex(std::string_view text, std::uint8_t &value)
{
  std::uint32_t intermediateValue = 0;
  const bool parsed = parseUnsigned32Hex(text, intermediateValue);
  if(!parsed || intermediateValue > std::numeric_limits<std::uint8_t>::max())
  {
    return false;
  }

  value = static_cast<std::uint8_t>(intermediateValue);
  return true;
}

bool parseTimestampNs(std::string_view text, std::uint64_t &timestampNs)
{
  const std::string trimmedText = trim(text);
  if(trimmedText.empty())
  {
    return false;
  }

  char *endPointer = nullptr;
  const double seconds = std::strtod(trimmedText.c_str(), &endPointer);
  if(endPointer != trimmedText.c_str() + trimmedText.size())
  {
    return false;
  }

  timestampNs = static_cast<std::uint64_t>(seconds * kNanosecondsPerSecond);
  return true;
}

bool parsePayloadBytes(std::string_view payloadText, std::array<std::uint8_t, 64> &payload, std::uint8_t &dlc)
{
  payload.fill(0);

  const std::string normalizedPayload = trim(payloadText);
  if(normalizedPayload.empty())
  {
    dlc = 0;
    return true;
  }

  if(normalizedPayload.find(' ') != std::string::npos)
  {
    const std::vector<std::string> tokens = split(normalizedPayload, ' ');
    if(tokens.size() > payload.size())
    {
      return false;
    }

    for(std::size_t index = 0; index < tokens.size(); ++index)
    {
      if(tokens[index].empty())
      {
        continue;
      }

      if(!parseUnsigned8Hex(tokens[index], payload[index]))
      {
        return false;
      }
    }

    dlc = static_cast<std::uint8_t>(tokens.size());
    return true;
  }

  if(normalizedPayload.size() % 2U != 0 || (normalizedPayload.size() / 2U) > payload.size())
  {
    return false;
  }

  for(std::size_t index = 0; index < normalizedPayload.size() / 2U; ++index)
  {
    if(!parseUnsigned8Hex(normalizedPayload.substr(index * 2U, 2U), payload[index]))
    {
      return false;
    }
  }

  dlc = static_cast<std::uint8_t>(normalizedPayload.size() / 2U);
  return true;
}
} // namespace

bool TextLineReader::open(const std::string &path)
{
  inputFile_.open(path);
  return inputFile_.is_open();
}

bool TextLineReader::readLine(std::string &line)
{
  return static_cast<bool>(std::getline(inputFile_, line));
}

void TextLineReader::close()
{
  inputFile_.close();
}

bool TextLineReader::isOpen() const
{
  return inputFile_.is_open();
}

bool TextTraceReaderBase::open(
  const can_reader_api::SourceDescriptor &sourceDescriptor,
  const can_reader_api::ReaderOptions &readerOptions)
{
  close();
  if(!canParseExtension(sourceDescriptor.extension))
  {
    return false;
  }

  if(!textLineReader_.open(sourceDescriptor.path))
  {
    return false;
  }

  readerOptions_ = readerOptions;
  traceMetadata_ = {};
  traceMetadata_.sourcePath = sourceDescriptor.path;
  traceMetadata_.sourceFormat = formatName();
  currentLineNumber_ = 0;
  isOpen_ = true;
  return true;
}

can_reader_api::ReadResult TextTraceReaderBase::readChunk(std::span<can_core::CanEvent> outputBuffer)
{
  can_reader_api::ReadResult readResult;
  if(!isOpen_)
  {
    readResult.errorInfo.code = can_core::ErrorCode::IoFailure;
    readResult.errorInfo.message = "Reader is not open";
    return readResult;
  }

  std::size_t outputIndex = 0;
  std::string line;
  while(outputIndex < outputBuffer.size() && textLineReader_.readLine(line))
  {
    ++currentLineNumber_;
    const std::string trimmedLine = trim(line);
    if(trimmedLine.empty())
    {
      continue;
    }

    if(shouldSkipLine(trimmedLine))
    {
      continue;
    }

    ParsedTextRecord parsedTextRecord;
    if(!parseLine(trimmedLine, parsedTextRecord))
    {
      if(readerOptions_.shouldValidateStrictly)
      {
        return makeParseError(currentLineNumber_, "Unable to parse text trace line");
      }

      continue;
    }

    outputBuffer[outputIndex++] = makeCanEvent(parsedTextRecord);
    ++traceMetadata_.eventCount;
    if(traceMetadata_.eventCount == 1U)
    {
      traceMetadata_.startTimestampNs = parsedTextRecord.timestampNs;
    }
    traceMetadata_.endTimestampNs = parsedTextRecord.timestampNs;
  }

  readResult.eventCount = outputIndex;
  readResult.isEndOfStream = !textLineReader_.isOpen() || outputIndex < outputBuffer.size();
  if(readResult.isEndOfStream && textLineReader_.isOpen())
  {
    textLineReader_.close();
    isOpen_ = false;
  }

  return readResult;
}

bool TextTraceReaderBase::shouldSkipLine(const std::string &line) const
{
  (void)line;
  return false;
}

can_core::TraceMetadata TextTraceReaderBase::metadata() const
{
  return traceMetadata_;
}

can_reader_api::ReaderCapabilities TextTraceReaderBase::capabilities() const
{
  can_reader_api::ReaderCapabilities readerCapabilities;
  readerCapabilities.formatName = formatName();
  readerCapabilities.supportsRandomAccess = false;
  readerCapabilities.supportsStreaming = true;
  readerCapabilities.supportsCanFd = true;
  return readerCapabilities;
}

void TextTraceReaderBase::close()
{
  textLineReader_.close();
  isOpen_ = false;
}

can_reader_api::ReadResult TextTraceReaderBase::makeParseError(std::size_t lineNumber, const std::string &message) const
{
  can_reader_api::ReadResult readResult;
  readResult.errorInfo.code = can_core::ErrorCode::ParseFailure;
  readResult.errorInfo.message = message;
  readResult.errorInfo.line = lineNumber;
  return readResult;
}

can_core::CanEvent TextTraceReaderBase::makeCanEvent(const ParsedTextRecord &parsedTextRecord)
{
  can_core::CanEvent canEvent;
  canEvent.timestampNs = parsedTextRecord.timestampNs;
  canEvent.canId = parsedTextRecord.canId;
  canEvent.dlc = parsedTextRecord.dlc;
  canEvent.channel = parsedTextRecord.channel;
  canEvent.frameType = parsedTextRecord.frameType;
  canEvent.payload = parsedTextRecord.payload;
  return canEvent;
}

bool CandumpReader::canParseExtension(std::string_view extension) const
{
  const std::string normalizedExtension = toLower(extension);
  return normalizedExtension == ".log" || normalizedExtension == ".candump" || normalizedExtension == ".txt";
}

bool CandumpReader::parseLine(const std::string &line, ParsedTextRecord &parsedTextRecord) const
{
  static const std::regex kCandumpRegex(
    R"candump(^\(([0-9]+(?:\.[0-9]+)?)\)\s+([A-Za-z0-9_]+)\s+([0-9A-Fa-f]+)#([0-9A-Fa-f]*)$)candump");

  std::smatch match;
  if(!std::regex_match(line, match, kCandumpRegex))
  {
    return false;
  }

  if(!parseTimestampNs(match[1].str(), parsedTextRecord.timestampNs))
  {
    return false;
  }

  const std::string channelToken = match[2].str();
  if(channelToken.size() > 3U && std::isdigit(static_cast<unsigned char>(channelToken.back())))
  {
    parsedTextRecord.channel = static_cast<std::uint8_t>(channelToken.back() - '0');
  }

  if(!parseUnsigned32Hex(match[3].str(), parsedTextRecord.canId) || !can_core::isValidCanId(parsedTextRecord.canId))
  {
    return false;
  }

  return parsePayloadBytes(match[4].str(), parsedTextRecord.payload, parsedTextRecord.dlc);
}

std::string CandumpReader::formatName() const
{
  return "candump";
}

bool CsvTraceReader::canParseExtension(std::string_view extension) const
{
  return toLower(extension) == ".csv";
}

bool CsvTraceReader::parseLine(const std::string &line, ParsedTextRecord &parsedTextRecord) const
{
  const std::vector<std::string> tokens = split(line, ',');
  if(tokens.size() < 5U)
  {
    return false;
  }

  std::uint64_t channelValue = 0;
  if(!parseTimestampNs(tokens[0], parsedTextRecord.timestampNs) || !parseUnsigned64(tokens[1], channelValue) ||
    !parseUnsigned32Hex(tokens[2], parsedTextRecord.canId) || !parsePayloadBytes(tokens[4], parsedTextRecord.payload, parsedTextRecord.dlc))
  {
    return false;
  }

  parsedTextRecord.channel = static_cast<std::uint8_t>(channelValue);
  if(tokens.size() > 5U && toLower(tokens[5]) == "fd")
  {
    parsedTextRecord.frameType = can_core::FrameType::CanFd;
  }

  return can_core::isValidCanId(parsedTextRecord.canId);
}

std::string CsvTraceReader::formatName() const
{
  return "csv";
}

bool CsvTraceReader::shouldSkipLine(const std::string &line) const
{
  const std::vector<std::string> tokens = split(line, ',');
  return !tokens.empty() && toLower(tokens.front()) == "timestamp";
}

bool AscTraceReader::canParseExtension(std::string_view extension) const
{
  return toLower(extension) == ".asc";
}

bool AscTraceReader::parseLine(const std::string &line, ParsedTextRecord &parsedTextRecord) const
{
  const std::vector<std::string> tokens = split(line, ' ');
  if(tokens.size() < 6U)
  {
    return false;
  }

  if(!std::isdigit(static_cast<unsigned char>(tokens[0].front())))
  {
    return false;
  }

  std::uint64_t channelValue = 0;
  std::uint64_t dlcValue = 0;
  if(!parseTimestampNs(tokens[0], parsedTextRecord.timestampNs) || !parseUnsigned64(tokens[1], channelValue) ||
    !parseUnsigned32Hex(tokens[2], parsedTextRecord.canId) || !parseUnsigned64(tokens[5], dlcValue))
  {
    return false;
  }

  if(dlcValue > 64U)
  {
    return false;
  }

  parsedTextRecord.channel = static_cast<std::uint8_t>(channelValue);
  parsedTextRecord.dlc = static_cast<std::uint8_t>(dlcValue);
  parsedTextRecord.payload.fill(0);

  const std::size_t dataStartIndex = 6;
  const std::size_t dataEndIndex = std::min(tokens.size(), dataStartIndex + dlcValue);
  for(std::size_t index = dataStartIndex; index < dataEndIndex; ++index)
  {
    std::uint8_t byteValue = 0;
    if(!parseUnsigned8Hex(tokens[index], byteValue))
    {
      return false;
    }

    parsedTextRecord.payload[index - dataStartIndex] = byteValue;
  }

  return can_core::isValidCanId(parsedTextRecord.canId);
}

std::string AscTraceReader::formatName() const
{
  return "asc";
}

bool AscTraceReader::shouldSkipLine(const std::string &line) const
{
  if(line.empty())
  {
    return true;
  }

  const unsigned char firstCharacter = static_cast<unsigned char>(line.front());
  return !std::isdigit(firstCharacter);
}

bool CandumpReaderFactory::canOpen(const can_reader_api::SourceDescriptor &sourceDescriptor) const
{
  CandumpReader candumpReader;
  return candumpReader.canParseExtension(sourceDescriptor.extension);
}

std::unique_ptr<can_reader_api::ITraceReader> CandumpReaderFactory::create() const
{
  return std::make_unique<CandumpReader>();
}

std::string CandumpReaderFactory::formatName() const
{
  return "candump";
}

bool CsvTraceReaderFactory::canOpen(const can_reader_api::SourceDescriptor &sourceDescriptor) const
{
  CsvTraceReader csvTraceReader;
  return csvTraceReader.canParseExtension(sourceDescriptor.extension);
}

std::unique_ptr<can_reader_api::ITraceReader> CsvTraceReaderFactory::create() const
{
  return std::make_unique<CsvTraceReader>();
}

std::string CsvTraceReaderFactory::formatName() const
{
  return "csv";
}

bool AscTraceReaderFactory::canOpen(const can_reader_api::SourceDescriptor &sourceDescriptor) const
{
  AscTraceReader ascTraceReader;
  return ascTraceReader.canParseExtension(sourceDescriptor.extension);
}

std::unique_ptr<can_reader_api::ITraceReader> AscTraceReaderFactory::create() const
{
  return std::make_unique<AscTraceReader>();
}

std::string AscTraceReaderFactory::formatName() const
{
  return "asc";
}
} // namespace can_readers_text
