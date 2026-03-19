#include "can_cache/cache_reader.hpp"

namespace can_cache
{
namespace
{
std::uint64_t computeChunkByteSize(std::span<const can_core::CanEvent> chunk)
{
  return sizeof(CacheChunkHeader) + static_cast<std::uint64_t>(chunk.size_bytes());
}
} // namespace

bool CacheWriter::open(const std::string &path)
{
  close();
  outputFile_.open(path, std::ios::binary);
  if(!outputFile_.is_open())
  {
    return false;
  }

  header_ = {};
  directoryEntries_.clear();
  outputFile_.write(reinterpret_cast<const char *>(&header_), sizeof(header_));
  isOpen_ = static_cast<bool>(outputFile_);
  return isOpen_;
}

bool CacheWriter::writeChunk(std::span<const can_core::CanEvent> chunk)
{
  if(!isOpen_)
  {
    return false;
  }

  CacheChunkHeader chunkHeader;
  chunkHeader.chunkId = header_.chunkCount;
  chunkHeader.eventCount = static_cast<std::uint32_t>(chunk.size());
  if(!chunk.empty())
  {
    chunkHeader.firstTimestampNs = chunk.front().timestampNs;
    chunkHeader.lastTimestampNs = chunk.back().timestampNs;
  }

  CacheDirectoryEntry cacheDirectoryEntry;
  cacheDirectoryEntry.chunkId = chunkHeader.chunkId;
  cacheDirectoryEntry.fileOffset = static_cast<std::uint64_t>(outputFile_.tellp());
  cacheDirectoryEntry.byteSize = computeChunkByteSize(chunk);

  outputFile_.write(reinterpret_cast<const char *>(&chunkHeader), sizeof(chunkHeader));
  outputFile_.write(reinterpret_cast<const char *>(chunk.data()), static_cast<std::streamsize>(chunk.size_bytes()));
  if(!outputFile_)
  {
    return false;
  }

  directoryEntries_.push_back(cacheDirectoryEntry);
  ++header_.chunkCount;
  return true;
}

void CacheWriter::close()
{
  if(!outputFile_.is_open())
  {
    isOpen_ = false;
    return;
  }

  header_.directoryOffset = static_cast<std::uint64_t>(outputFile_.tellp());
  for(const CacheDirectoryEntry &cacheDirectoryEntry : directoryEntries_)
  {
    outputFile_.write(reinterpret_cast<const char *>(&cacheDirectoryEntry), sizeof(cacheDirectoryEntry));
  }

  outputFile_.seekp(0);
  outputFile_.write(reinterpret_cast<const char *>(&header_), sizeof(header_));
  outputFile_.close();
  isOpen_ = false;
}

bool CacheReader::open(const std::string &path)
{
  close();
  inputFile_.open(path, std::ios::binary);
  if(!inputFile_.is_open())
  {
    return false;
  }

  inputFile_.read(reinterpret_cast<char *>(&header_), sizeof(header_));
  if(!inputFile_ || header_.magic != 0x43414E50)
  {
    close();
    return false;
  }

  directoryEntries_.resize(static_cast<std::size_t>(header_.chunkCount));
  inputFile_.seekg(static_cast<std::streamoff>(header_.directoryOffset));
  inputFile_.read(reinterpret_cast<char *>(directoryEntries_.data()),
    static_cast<std::streamsize>(directoryEntries_.size() * sizeof(CacheDirectoryEntry)));
  if(!inputFile_)
  {
    close();
    return false;
  }

  traceMetadata_ = {};
  traceMetadata_.sourceFormat = "can_cache";
  traceMetadata_.eventCount = 0;
  for(const CacheDirectoryEntry &cacheDirectoryEntry : directoryEntries_)
  {
    inputFile_.seekg(static_cast<std::streamoff>(cacheDirectoryEntry.fileOffset));
    CacheChunkHeader chunkHeader;
    inputFile_.read(reinterpret_cast<char *>(&chunkHeader), sizeof(chunkHeader));
    traceMetadata_.eventCount += chunkHeader.eventCount;
    if(cacheDirectoryEntry.chunkId == 0)
    {
      traceMetadata_.startTimestampNs = chunkHeader.firstTimestampNs;
    }
    traceMetadata_.endTimestampNs = chunkHeader.lastTimestampNs;
  }

  isOpen_ = true;
  return true;
}

std::size_t CacheReader::readChunk(std::uint64_t chunkId, std::span<can_core::CanEvent> outputBuffer) const
{
  if(!isOpen_ || chunkId >= directoryEntries_.size())
  {
    return 0;
  }

  const CacheDirectoryEntry &cacheDirectoryEntry = directoryEntries_[static_cast<std::size_t>(chunkId)];
  auto &inputFile = const_cast<std::ifstream &>(inputFile_);
  inputFile.seekg(static_cast<std::streamoff>(cacheDirectoryEntry.fileOffset));

  CacheChunkHeader chunkHeader;
  inputFile.read(reinterpret_cast<char *>(&chunkHeader), sizeof(chunkHeader));
  const std::size_t eventsToRead = std::min<std::size_t>(chunkHeader.eventCount, outputBuffer.size());
  inputFile.read(reinterpret_cast<char *>(outputBuffer.data()),
    static_cast<std::streamsize>(eventsToRead * sizeof(can_core::CanEvent)));
  return inputFile ? eventsToRead : 0;
}

can_core::TraceMetadata CacheReader::metadata() const
{
  return traceMetadata_;
}

void CacheReader::close()
{
  if(inputFile_.is_open())
  {
    inputFile_.close();
  }

  directoryEntries_.clear();
  isOpen_ = false;
}
} // namespace can_cache

