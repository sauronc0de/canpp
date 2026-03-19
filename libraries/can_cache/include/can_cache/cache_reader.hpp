#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "can_core/core_types.hpp"

namespace can_cache
{
struct CacheFileHeader
{
  std::uint32_t magic = 0x43414E50;
  std::uint16_t majorVersion = 1;
  std::uint16_t minorVersion = 0;
  std::uint64_t chunkCount = 0;
  std::uint64_t directoryOffset = 0;
};

struct CacheChunkHeader
{
  std::uint64_t chunkId = 0;
  std::uint32_t eventCount = 0;
  std::uint64_t firstTimestampNs = 0;
  std::uint64_t lastTimestampNs = 0;
};

struct CacheDirectoryEntry
{
  std::uint64_t chunkId = 0;
  std::uint64_t fileOffset = 0;
  std::uint64_t byteSize = 0;
};

class CacheWriter
{
public:
  bool open(const std::string &path);
  bool writeChunk(std::span<const can_core::CanEvent> chunk);
  void close();

private:
  std::ofstream outputFile_;
  CacheFileHeader header_;
  std::vector<CacheDirectoryEntry> directoryEntries_;
  bool isOpen_ = false;
};

class CacheReader
{
public:
  bool open(const std::string &path);
  std::size_t readChunk(std::uint64_t chunkId, std::span<can_core::CanEvent> outputBuffer) const;
  [[nodiscard]] can_core::TraceMetadata metadata() const;
  void close();

private:
  std::ifstream inputFile_;
  CacheFileHeader header_;
  std::vector<CacheDirectoryEntry> directoryEntries_;
  can_core::TraceMetadata traceMetadata_;
  bool isOpen_ = false;
};
} // namespace can_cache

