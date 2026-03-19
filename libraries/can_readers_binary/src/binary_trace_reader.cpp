#include "can_readers_binary/binary_trace_reader.hpp"

#include <algorithm>

namespace can_readers_binary
{
namespace
{
std::string toLower(std::string_view value)
{
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}
} // namespace

BinaryCursor::BinaryCursor(std::vector<std::byte> data)
  : data_(std::move(data))
{
}

bool BinaryCursor::isValid() const
{
  return position_ <= data_.size();
}

std::size_t BinaryCursor::remaining() const
{
  return position_ <= data_.size() ? data_.size() - position_ : 0;
}

bool BinaryCursor::advance(std::size_t byteCount)
{
  if(position_ + byteCount > data_.size())
  {
    return false;
  }

  position_ += byteCount;
  return true;
}

bool UnsupportedBinaryReader::open(
  const can_reader_api::SourceDescriptor &sourceDescriptor,
  const can_reader_api::ReaderOptions &readerOptions)
{
  (void)readerOptions;
  if(!canParseExtension(sourceDescriptor.extension))
  {
    return false;
  }

  traceMetadata_ = {};
  traceMetadata_.sourcePath = sourceDescriptor.path;
  traceMetadata_.sourceFormat = formatName();
  isOpen_ = true;
  return true;
}

can_reader_api::ReadResult UnsupportedBinaryReader::readChunk(std::span<can_core::CanEvent> outputBuffer)
{
  (void)outputBuffer;
  can_reader_api::ReadResult readResult;
  if(!isOpen_)
  {
    readResult.errorInfo.code = can_core::ErrorCode::IoFailure;
    readResult.errorInfo.message = "Binary reader is not open";
    return readResult;
  }

  readResult.errorInfo.code = can_core::ErrorCode::UnsupportedFormat;
  readResult.errorInfo.message = "Binary reader is recognized but not implemented yet";
  return readResult;
}

can_core::TraceMetadata UnsupportedBinaryReader::metadata() const
{
  return traceMetadata_;
}

can_reader_api::ReaderCapabilities UnsupportedBinaryReader::capabilities() const
{
  can_reader_api::ReaderCapabilities readerCapabilities;
  readerCapabilities.formatName = formatName();
  readerCapabilities.supportsRandomAccess = true;
  readerCapabilities.supportsStreaming = false;
  readerCapabilities.supportsCanFd = true;
  return readerCapabilities;
}

void UnsupportedBinaryReader::close()
{
  isOpen_ = false;
}

bool BlfReader::canParseExtension(std::string_view extension) const
{
  return toLower(extension) == ".blf";
}

std::string BlfReader::formatName() const
{
  return "blf";
}

bool Mf4Reader::canParseExtension(std::string_view extension) const
{
  const std::string normalizedExtension = toLower(extension);
  return normalizedExtension == ".mf4" || normalizedExtension == ".mdf4";
}

std::string Mf4Reader::formatName() const
{
  return "mf4";
}

bool TrcReader::canParseExtension(std::string_view extension) const
{
  return toLower(extension) == ".trc";
}

std::string TrcReader::formatName() const
{
  return "trc";
}
} // namespace can_readers_binary

