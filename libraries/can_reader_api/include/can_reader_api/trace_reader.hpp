#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>

#include "can_core/core_types.hpp"

namespace can_reader_api
{
struct SourceDescriptor
{
  std::string path;
  std::string extension;
  bool isFile = true;
};

struct ReaderOptions
{
  std::size_t chunkSize = 4096;
  bool shouldValidateStrictly = true;
  bool shouldNormalizeTimestamps = true;
};

struct ReaderCapabilities
{
  std::string formatName;
  bool supportsRandomAccess = false;
  bool supportsStreaming = true;
  bool supportsCanFd = true;
};

struct ReadResult
{
  std::size_t eventCount = 0;
  bool isEndOfStream = false;
  can_core::ErrorInfo errorInfo;

  [[nodiscard]] bool hasError() const
  {
    return errorInfo.hasError();
  }
};

class ITraceReader
{
public:
  virtual ~ITraceReader() = default;

  virtual bool open(const SourceDescriptor &sourceDescriptor, const ReaderOptions &readerOptions) = 0;
  virtual ReadResult readChunk(std::span<can_core::CanEvent> outputBuffer) = 0;
  [[nodiscard]] virtual can_core::TraceMetadata metadata() const = 0;
  [[nodiscard]] virtual ReaderCapabilities capabilities() const = 0;
  virtual void close() = 0;
};

class ITraceReaderFactory
{
public:
  virtual ~ITraceReaderFactory() = default;

  [[nodiscard]] virtual bool canOpen(const SourceDescriptor &sourceDescriptor) const = 0;
  [[nodiscard]] virtual std::unique_ptr<ITraceReader> create() const = 0;
  [[nodiscard]] virtual std::string formatName() const = 0;
};
} // namespace can_reader_api
